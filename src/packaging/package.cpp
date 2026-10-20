#include <anvil/package.h>
#include <anvil/wildlands.h>
#include <anvil/storage.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <lzo/lzo1x.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace anvil::packaging {
namespace {
constexpr qint64 Chunk=2*1024*1024, MaxDelta=256LL*1024*1024, MaxArchive=512LL*1024*1024*1024;
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
void check(bool condition,const QString &s) { if(!condition)fail(s); }
void log(const Options &o,const QString &s) { if(o.progress)o.progress(s); }
template<class T> T number(const QByteArray &b,qint64 at) {
    check(at>=0&&at<=b.size()&&qint64(sizeof(T))<=b.size()-at,"Truncated package integer");
    return qFromLittleEndian<T>(b.constData()+at);
}
template<class T> void put(QByteArray &b,T n) { char data[sizeof(T)];qToLittleEndian(n,data);b.append(data,sizeof(T)); }
QByteArray read(QFile &f,qint64 at,qint64 size) {
    check(at>=0&&size>=0&&at<=f.size()&&size<=f.size()-at&&f.seek(at),"Archive range outside file");
    auto b=f.read(size);check(b.size()==size,"Short archive read");return b;
}
void newFile(const QString &path,const QByteArray &bytes) {
    QFile f(path);check(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Output already exists or cannot be created: "+path);
    if(f.write(bytes)!=bytes.size()||!f.flush()){f.remove();fail("Cannot write output: "+path);}
}
QString newDirectory(const QString &path,const QString &game) {
    check(!path.isEmpty(),"Specify a NEW output directory");
    const auto resolved=requireOutsideGame(path,game);
    check(!QFileInfo(resolved).exists()&&!QFileInfo(resolved).isSymLink(),"Output directory already exists; choose a new name");
    const QFileInfo info(resolved);
    check(QFileInfo(info.absolutePath()).isDir(),"Output parent directory must exist");
    check(QDir(info.absolutePath()).mkdir(info.fileName()),"Cannot create NEW output directory");
    return resolved;
}
struct Entry { quint64 id;QString name; qint64 offset,size,row,meta; };
struct Index { QVector<Entry> english; qint64 size;QDateTime mtime; };
Index index(QFile &f) {
    const auto h=read(f,0,21);
    check(h.left(9)==QByteArray("scimitar\0",9)&&number<quint32>(h,9)==27&&number<quint64>(h,13)==1050,
          "Unsupported archive layout; expected observed v27/1050");
    const auto root=read(f,1050,44);
    const auto expected=number<quint32>(root,0), sets=number<quint32>(root,32);
    check(expected>0&&expected<=2000000&&sets>0&&sets<=10000&&number<qint32>(root,20)==-1&&number<qint32>(root,24)==-1,
          "Unverified archive root");
    Index out{{},f.size(),QFileInfo(f.fileName()).lastModified()};
    qint64 next=number<qint64>(root,36);quint64 total=0;
    QSet<qint64> visited;QSet<quint64> ids;
    QVector<QPair<qint64,qint64>> spans{{0,1094}},payloadSpans;
    for(quint32 s=0;s<sets;++s){
        check(!visited.contains(next),"Cyclic archive index");visited.insert(next);
        auto set=read(f,next,48);spans.append({next,next+48});
        const auto count=number<quint32>(set,0);check(total+count<=expected,"Archive count mismatch");
        const auto rows=number<qint64>(set,8), metas=number<qint64>(set,32);
        const auto table=read(f,rows,qint64(count)*20), metadata=read(f,metas,qint64(count)*192);
        spans.append({rows,rows+table.size()});spans.append({metas,metas+metadata.size()});
        for(quint32 i=0;i<count;++i){
            const qint64 r=i*20LL,m=i*192LL;
            const auto id=number<quint64>(table,r+8);const auto at=number<qint64>(table,r);
            const auto length=number<qint32>(table,r+16);
            check(!ids.contains(id)&&at>=0&&length>=0&&at<=f.size()&&length<=f.size()-at&&number<qint32>(metadata,m)==length,
                  "Archive has duplicate IDs, invalid ranges or inconsistent lengths");
            ids.insert(id);if(length)payloadSpans.append({at,at+length});
            auto bytes=metadata.mid(m+44,128);const auto zero=bytes.indexOf('\0');if(zero>=0)bytes.truncate(zero);
            const auto name=QString::fromLatin1(bytes);
            if(number<quint32>(metadata,m+16)==0x6e3c9c6f&&name.startsWith("LocalizationPackage_English(US)"))
                out.english.append({id,name,at,length,rows+r,metas+m});
        }
        total+=count;next=number<qint64>(set,16);
    }
    check(total==expected&&next==-1,"Incomplete archive index");
    auto disjoint=[](QVector<QPair<qint64,qint64>> &s){
        std::sort(s.begin(),s.end());
        for(qsizetype i=1;i<s.size();++i)check(s[i-1].second<=s[i].first,"Overlapping archive ranges");
    };
    disjoint(spans);disjoint(payloadSpans);
    qsizetype t=0;
    for(const auto &p:payloadSpans){
        while(t<spans.size()&&spans[t].second<=p.first)++t;
        if(t<spans.size())check(p.second<=spans[t].first,"Payload overlaps archive index");
    }
    return out;
}
quint32 adler0(const QByteArray &b){quint32 a=0,s=0;for(unsigned char c:b){a=(a+c)%65521;s=(s+a)%65521;}return s<<16|a;}
QByteArray descriptor(const QByteArray &raw,bool wildlands){
    check(!raw.isEmpty()&&raw.size()<=64*1024*1024,"Invalid cooked stream size");
    QVector<QPair<quint32,QByteArray>> blocks;
    QVector<lzo_align_t> work((LZO1X_1_MEM_COMPRESS+sizeof(lzo_align_t)-1)/sizeof(lzo_align_t));
    for(qsizetype at=0;at<raw.size();at+=32768){
        auto plain=raw.mid(at,32768),stored=plain;
        if(wildlands){
            QByteArray encoded(plain.size()+plain.size()/16+67,'\0');lzo_uint length=encoded.size();
            check(lzo1x_1_compress(reinterpret_cast<const unsigned char*>(plain.constData()),plain.size(),
                 reinterpret_cast<unsigned char*>(encoded.data()),&length,work.data())==LZO_E_OK,"LZO compression failed");
            if(length<lzo_uint(plain.size())){encoded.resize(length);stored=encoded;}
        }
        blocks.append({quint32(plain.size()),stored});
    }
    QByteArray b;put<quint64>(b,0x1004fa9957fbaa33ULL);put<quint16>(b,wildlands?1:3);put<quint8>(b,wildlands?1:3);
    put<quint16>(b,32768);put<quint16>(b,32768);put<quint32>(b,blocks.size());
    for(const auto &block:blocks){
        if(wildlands){put<quint16>(b,block.first);put<quint16>(b,block.second.size());}
        else {put<quint32>(b,block.first);put<quint32>(b,block.second.size());}
    }
    for(const auto &block:blocks){put(b,adler0(block.second));b+=block.second;}
    return b;
}
QPair<QByteArray,QByteArray> decode(const QByteArray &data,bool wildlands,const Options &o){
    if(wildlands)return anvil::wildlands::decodeContainer(data);
    const auto plan=breakpoint::parseContainer(data);
    bool compressed=false;for(const auto &b:plan.blocks)compressed|=b.rawSize!=quint32(b.bytes.size());
    QVector<QByteArray> blocks;
    if(!compressed){for(const auto &b:plan.blocks)blocks.append(b.bytes);}
    else {
        check(QFileInfo(o.helper).isFile()&&QFileInfo(o.codecDll).isFile(),"Breakpoint needs the decoder helper and your own oo2core_7_win64.dll");
        QTemporaryDir temp;check(temp.isValid(),"Cannot create temporary decoder directory");requireOutsideGame(temp.path(),o.gameDirectory);
        const auto dll=temp.filePath("oo2core_7_win64.dll"), jobs=temp.filePath("jobs.bin"), output=temp.filePath("decoded.bin");
        check(QFile::copy(o.codecDll,dll),"Cannot copy owned codec to temporary directory");
        QByteArray job;put<quint32>(job,plan.blocks.size());for(const auto &b:plan.blocks){put(job,b.rawSize);put<quint32>(job,b.bytes.size());job+=b.bytes;}
        newFile(jobs,job);QProcess process;
#ifdef Q_OS_WIN
        process.setProgram(o.helper);process.setArguments({QDir::toNativeSeparators(dll),QDir::toNativeSeparators(jobs),QDir::toNativeSeparators(output)});
#else
        check(!o.winePrefix.isEmpty(),"Specify --wine-prefix for Breakpoint on Linux");requireOutsideGame(o.winePrefix,o.gameDirectory);
        check(QDir().mkpath(QFileInfo(o.winePrefix).absolutePath()),"Cannot create Wine parent directory");
        const auto wine=QStandardPaths::findExecutable("wine");check(!wine.isEmpty(),"Wine is required to decode original Breakpoint resources");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("WINEPREFIX",o.winePrefix);env.insert("WINEDEBUG","-all");
        env.insert("WINEDLLOVERRIDES","mscoree,mshtml=");env.insert("XDG_DATA_HOME",o.winePrefix+"/xdg-data");env.insert("XDG_CONFIG_HOME",o.winePrefix+"/xdg-config");
        process.setProcessEnvironment(env);process.setProgram(wine);process.setArguments({o.helper,"Z:"+dll,"Z:"+jobs,"Z:"+output});
#endif
        process.start();check(process.waitForStarted(10000),"Cannot start original resource decoder");
        if(!process.waitForFinished(120000)){process.kill();process.waitForFinished(3000);fail("Original resource decoder timed out");}
        check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"Original resource decoder failed: "+QString::fromUtf8(process.readAllStandardError()).left(1000));
        check(QFileInfo(output).size()<=65*1024*1024,"Decoder output exceeds limit");const auto raw=readFile(output);qint64 at=4;
        check(number<quint32>(raw,0)==quint32(plan.blocks.size()),"Decoded block count mismatch");
        for(const auto &b:plan.blocks){const auto n=number<quint32>(raw,at);at+=4;check(n==b.rawSize&&n<=raw.size()-at,"Decoded block size mismatch");blocks.append(raw.mid(at,n));at+=n;}
        check(at==raw.size(),"Trailing decoded blocks");
    }
    QPair<QByteArray,QByteArray> result;
    for(qsizetype i=0;i<blocks.size();++i)(i<plan.metadataBlocks?result.first:result.second)+=blocks[i];
    return result;
}
Document textDocument(const QByteArray &payload,bool wildlands){return wildlands?anvil::wildlands::decodeLocalization(payload):breakpoint::decodeLocalization(payload);}
void validateTokens(const QString &source,const QString &translation){
    static const QRegularExpression pattern(R"(\{[^{}\r\n]*\}|\[[A-Z_][A-Z0-9_:. -]*\]|</?[^<>\r\n]+>|%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[hlL]*[sdifuxXocgeEGp]|%\d+|%%|\\[nrt]|\r\n|\n|\r)");
    auto tokens=[&](const QString &s){QMap<QString,int> counts;auto m=pattern.globalMatch(s);while(m.hasNext())++counts[m.next().captured()];return counts;};
    check(tokens(source)==tokens(translation),"Translation changed placeholders/tags/line breaks; fix the row before packaging");
}
struct Edit { qint64 offset;QByteArray bytes; };
struct Patch { QString name; qint64 sourceSize=0,resultSize=0;QByteArray sourceHash,resultHash;QVector<Edit> edits; };
void validatePatch(Patch &p){
    check(p.sourceSize>0&&p.sourceSize<=MaxArchive&&p.resultSize>=p.sourceSize&&p.resultSize<=MaxArchive,"Archive size outside supported bounds");
    check(!p.edits.isEmpty()&&p.edits.size()<=1024,"Invalid patch edit count");
    std::sort(p.edits.begin(),p.edits.end(),[](const Edit&a,const Edit&b){return a.offset<b.offset;});
    qint64 end=0,total=0;
    for(const auto&e:p.edits){check(e.offset>=end&&!e.bytes.isEmpty()&&e.offset<=p.resultSize&&e.bytes.size()<=p.resultSize-e.offset,"Overlapping or out-of-range patch edit");end=e.offset+e.bytes.size();total+=e.bytes.size();}
    check(total<=MaxDelta,"Package delta exceeds 256 MiB");
}
// A virtual archive pass hashes both images and optionally writes a NEW archive copy.
QPair<QByteArray,QByteArray> transform(QFile &source,const Patch &patch,QIODevice *destination,const Options &o){
    check(source.size()==patch.sourceSize,"Source archive size does not match this package");
    const QFileInfo before(source.fileName());const auto size=before.size();const auto mtime=before.lastModified();
    check(source.seek(0),"Cannot seek source archive");QCryptographicHash original(QCryptographicHash::Sha256),modified(QCryptographicHash::Sha256);
    for(qint64 at=0;at<patch.resultSize;at+=Chunk){
        const auto count=std::min(Chunk,patch.resultSize-at);QByteArray output(count,'\0');
        const auto inputCount=at<patch.sourceSize?std::min(count,patch.sourceSize-at):0;
        if(inputCount){const auto b=source.read(inputCount);check(b.size()==inputCount,"Short source read during assembly");original.addData(b);std::copy(b.begin(),b.end(),output.begin());}
        for(const auto &edit:patch.edits){const auto start=std::max(at,edit.offset),end=std::min(at+count,edit.offset+edit.bytes.size());if(start<end)std::copy_n(edit.bytes.constData()+start-edit.offset,end-start,output.data()+start-at);}
        modified.addData(output);if(destination)check(destination->write(output)==output.size(),"Cannot write archive copy; check free space");
        if(at%(128*1024*1024)==0)log(o,QString("%1: %2 / %3 MiB").arg(patch.name).arg(at/(1024*1024)).arg(patch.resultSize/(1024*1024)));
    }
    const QFileInfo after(source.fileName());check(after.size()==size&&after.lastModified()==mtime,"Game archive changed while packaging; finish Steam updates first");
    return {original.result(),modified.result()};
}
QByteArray serialize(Patch p){
    validatePatch(p);check(p.sourceHash.size()==32&&p.resultHash.size()==32,"Missing patch hashes");
    QByteArray b("ANVILMOD1",9);put<quint32>(b,1);put<quint64>(b,p.sourceSize);put<quint64>(b,p.resultSize);b+=p.sourceHash;b+=p.resultHash;put<quint32>(b,p.edits.size());
    for(const auto &e:p.edits){put<quint64>(b,e.offset);put<quint32>(b,e.bytes.size());b+=e.bytes;}
    check(b.size()<=MaxDelta,"Delta file exceeds limit");return b;
}
Patch deserialize(const QByteArray &b){
    check(b.size()<=MaxDelta&&b.left(9)=="ANVILMOD1"&&number<quint32>(b,9)==1,"Unsupported/truncated Anvil package delta");
    Patch p;p.sourceSize=number<quint64>(b,13);p.resultSize=number<quint64>(b,21);p.sourceHash=b.mid(29,32);p.resultHash=b.mid(61,32);
    const auto count=number<quint32>(b,93);check(count>0&&count<=1024,"Invalid delta edit count");qint64 at=97,previousEnd=0;
    for(quint32 i=0;i<count;++i){const auto offset=number<quint64>(b,at);const auto n=number<quint32>(b,at+8);at+=12;check(offset<=quint64(MaxArchive)&&qint64(offset)>=previousEnd&&n>0&&n<=b.size()-at,"Truncated, unsorted or invalid delta edit");p.edits.append({qint64(offset),b.mid(at,n)});at+=n;previousEnd=offset+n;}
    check(at==b.size(),"Trailing delta bytes");validatePatch(p);return p;
}
QJsonObject patchJson(const Patch &p,const QByteArray &bytes){return {{"archive",p.name},{"delta",p.name+".anvildelta"},{"sourceBytes",QString::number(p.sourceSize)},{"resultBytes",QString::number(p.resultSize)},{"sourceSha256",QString::fromLatin1(p.sourceHash.toHex())},{"resultSha256",QString::fromLatin1(p.resultHash.toHex())},{"deltaSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};}
void notice(const QString &directory){newFile(directory+"/ПРОЧИТАЙТЕ.txt",QString(
    "ЕКСПЕРИМЕНТАЛЬНИЙ ПАКЕТ ПЕРЕКЛАДУ ANVIL\n\nДані та ID перевіряються повторним декодуванням. Сумісність із запуском гри НЕ підтверджено.\nUMAC збережено з оригіналу, не перераховано. Українські та білоруські гліфи шрифтів цим пакувальником не перевіряються й не додаються. Шрифтовий набір готується окремо.\n\narchives/ містить повні змінені копії лише тих архівів, у яких є переклад.\nВстановлена гра цією програмою не змінювалася. Не встановлюйте копії без окремої перевірки та резервування оригіналів.\nПакет прив’язано до SHA-256 вихідних архівів; після оновлення гри його потрібно зібрати заново.\nДля відновлення гри після самостійної заміни використовуйте власні резервні копії оригіналів.\n\nЯкщо archives/ ще немає, скористайтеся anvil-pack-translation --mode assemble --package-dir <цей пакет> --game-dir <оригінальна гра> --output-dir <нова папка поза грою>.\n").toUtf8());}
}
void pack(const Document &document,const Options &o){
    validate(document);check(document.sourceLanguage=="en"||document.sourceLanguage=="en-US","Only English source packages are verified");
    check(document.targetLanguage=="uk"||document.targetLanguage=="be","Choose a Ukrainian or Belarusian project");
    const auto game=QFileInfo(o.gameDirectory).canonicalFilePath();check(!game.isEmpty(),"Specify an existing game directory");
    requireOutsideGame(o.outputDirectory,game);check(!QFileInfo(o.outputDirectory).exists(),"Output directory already exists");
    const bool wildlands=document.game=="wildlands";
    check(QFileInfo(game+(wildlands?"/GRW.exe":"/GRB.exe")).isFile(),"Game directory does not match the project profile");
    QMap<quint64,QVector<const anvil::Entry*>> requested;
    const QRegularExpression idPattern(wildlands?"^grw:([0-9]+):([0-9]+)$":"^grb:([0-9]+):([0-9]+)$");
    for(const auto &entry:document.entries){
        if(entry.translation.isEmpty()||entry.status=="todo"||(o.reviewedOnly&&entry.status!="reviewed"))continue;
        const auto match=idPattern.match(entry.id);bool a=false,b=false;const auto package=match.captured(1).toULongLong(&a);match.captured(2).toUInt(&b);
        check(match.hasMatch()&&a&&b,"Translated row has an invalid game/package/string ID: "+entry.id);
        validateTokens(entry.source,entry.translation);requested[package].append(&entry);
    }
    check(!requested.isEmpty(),"Немає перекладених рядків для пакування. Підтвердьте переклад або вимкніть опцію «Лише перевірені».");
    struct Selected { QString archive;Entry entry; };
    QMap<quint64,Selected> selected;QMap<QString,Index> indexes;
    for(const auto &name:{QString("DataPC.forge"),QString("DataPC_patch_01.forge")}){
        QFile f(game+"/"+name);check(f.open(QIODevice::ReadOnly),"Cannot read archive: "+name);const auto inventory=index(f);indexes.insert(name,inventory);
        for(const auto &entry:inventory.english)selected.insert(entry.id,{name,entry});
    }
    QMap<QString,Patch> patches;QJsonArray resources;qsizetype changedRows=0;
    for(auto it=requested.cbegin();it!=requested.cend();++it){
        check(selected.contains(it.key()),"Translated package is not present in the current game; update the project first");
        const auto &chosen=selected[it.key()];const auto &entry=chosen.entry;
        check(entry.size>0&&entry.size<=16*1024*1024,"Original localization resource exceeds 16 MiB limit");
        log(o,"Пакування "+chosen.archive+" / "+entry.name);
        QFile f(game+"/"+chosen.archive);check(f.open(QIODevice::ReadOnly),"Cannot read source archive");
        const auto original=decode(read(f,entry.offset,entry.size),wildlands,o);
        const auto payload=breakpoint::unwrapResource(original.first,original.second,entry.id,entry.name);
        const auto before=textDocument(payload,wildlands);QMap<QString,const anvil::Entry*> byId;
        for(const auto &e:before.entries)byId.insert(e.id,&e);
        QMap<quint32,QString> replacements;
        for(const auto *e:it.value()){
            check(byId.contains(e->id),"Translated string ID is not in the original resource: "+e->id);
            check(byId[e->id]->source==e->source,"Original text changed in the game. Update your project before packaging: "+e->id);
            if(e->translation!=e->source)replacements.insert(e->id.section(':',-1).toUInt(),e->translation);
        }
        if(replacements.isEmpty())continue;
        const auto rebuilt=breakpoint::replaceLocalization(payload,replacements);
        auto files=original.second.left(original.second.size()-payload.size());files+=rebuilt;
        qToLittleEndian<quint32>(rebuilt.size(),files.data()+4);
        auto metadata=original.first;qToLittleEndian<quint32>(files.size(),metadata.data()+10);
        const auto container=descriptor(metadata,wildlands)+descriptor(files,wildlands);
        const auto decoded=decode(container,wildlands,o);
        const auto roundtrip=textDocument(breakpoint::unwrapResource(decoded.first,decoded.second,entry.id,entry.name),wildlands);
        check(roundtrip.entries.size()==before.entries.size(),"Round-trip string count changed");
        for(const auto &e:roundtrip.entries){check(byId.contains(e.id),"Round-trip string ID changed");const auto id=e.id.section(':',-1).toUInt();check(e.source==(replacements.contains(id)?replacements[id]:byId[e.id]->source),"Round-trip text mismatch");}
        auto &patch=patches[chosen.archive];patch.name=chosen.archive;
        if(!patch.sourceSize){patch.sourceSize=indexes[chosen.archive].size;patch.resultSize=patch.sourceSize;patch.edits.append(Edit{patch.sourceSize,QByteArray{}});}
        auto &append=patch.edits.front();const auto aligned=(patch.resultSize+32767)/32768*32768;
        append.bytes+=QByteArray(aligned-patch.resultSize,'\0');append.bytes+=container;patch.resultSize=aligned+container.size();
        auto row=read(f,entry.row,20);qToLittleEndian<qint64>(aligned,row.data());qToLittleEndian<qint32>(container.size(),row.data()+16);
        QByteArray length;put<qint32>(length,container.size());patch.edits.append({entry.row,row});patch.edits.append({entry.meta,length});
        resources.append(QJsonObject{{"archive",chosen.archive},{"packageId",QString::number(entry.id)},{"name",entry.name},{"translatedStrings",replacements.size()},{"stringsPreserved",before.entries.size()},{"roundTripVerified",true}});
        changedRows+=replacements.size();
    }
    check(!patches.isEmpty(),"Переклади збігаються з оригіналом; змін для пакета немає");
    QJsonArray manifestPatches;
    for(auto &patch:patches){
        const auto aligned=(patch.resultSize+32767)/32768*32768;patch.edits.front().bytes+=QByteArray(aligned-patch.resultSize,'\0');patch.resultSize=aligned;validatePatch(patch);
        QFile f(game+"/"+patch.name);check(f.open(QIODevice::ReadOnly),"Cannot hash original archive");
        check(f.size()==indexes[patch.name].size&&QFileInfo(f).lastModified()==indexes[patch.name].mtime,"Archive changed while preparing package");
        log(o,"Перевірка контрольних сум "+patch.name);const auto hashes=transform(f,patch,nullptr,o);patch.sourceHash=hashes.first;patch.resultHash=hashes.second;
    }
    const auto directory=newDirectory(o.outputDirectory,game);
    newFile(directory+"/INCOMPLETE.txt","Package build in progress. Do not use incomplete outputs.\n");
    notice(directory);
    for(const auto &patch:patches){const auto bytes=serialize(patch);newFile(directory+"/"+patch.name+".anvildelta",bytes);manifestPatches.append(patchJson(patch,bytes));}
    QJsonObject manifest{{"format","anvil-localization-package"},{"version",1},{"game",document.game},{"sourceLanguage",document.sourceLanguage},{"targetLanguage",document.targetLanguage},
        {"status","experimental-structurally-verified-not-tested-in-game"},{"translatedStrings",changedRows},{"reviewedOnly",o.reviewedOnly},{"resources",resources},{"archives",manifestPatches},
        {"umacRecomputed",false},{"fontCoverageVerified",false},{"inGameTested",false},{"gameFilesWritten",false},{"createdUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    newFile(directory+"/manifest.json",QJsonDocument(manifest).toJson());
    if(o.materialize){auto material=o;material.outputDirectory=directory+"/archives";assemble(directory,material);}
    check(QFile::remove(directory+"/INCOMPLETE.txt"),"Cannot finish package marker");
    log(o,QString("Пакет створено: %1 перекладів. Файли гри не змінено.\n%2").arg(changedRows).arg(directory));
}
void assemble(const QString &packageDirectory,const Options &o){
    const auto game=QFileInfo(o.gameDirectory).canonicalFilePath();check(!game.isEmpty(),"Specify the original game directory");
    const auto package=QFileInfo(packageDirectory).canonicalFilePath();check(!package.isEmpty(),"Specify --package-dir");
    check(QFileInfo(package+"/manifest.json").size()<=2*1024*1024,"Manifest exceeds limit");
    const auto manifest=QJsonDocument::fromJson(readFile(package+"/manifest.json")).object();
    check(manifest.value("format")=="anvil-localization-package"&&manifest.value("version").toInt()==1,"Unsupported package manifest");
    const auto archives=manifest.value("archives").toArray();check(!archives.isEmpty()&&archives.size()<=2,"Invalid archive list");
    QVector<Patch> patches;QSet<QString> names;
    for(const auto &item:archives){const auto m=item.toObject();const auto name=m.value("archive").toString(),delta=m.value("delta").toString();
        check((name=="DataPC.forge"||name=="DataPC_patch_01.forge")&&!names.contains(name)&&delta==name+".anvildelta","Invalid/duplicate package archive filename");names.insert(name);
        check(QFileInfo(package+"/"+delta).size()<=MaxDelta,"Delta exceeds size limit");const auto bytes=readFile(package+"/"+delta);
        check(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==m.value("deltaSha256").toString(),"Delta SHA-256 mismatch");
        auto patch=deserialize(bytes);patch.name=name;
        check(QString::fromLatin1(patch.sourceHash.toHex())==m.value("sourceSha256").toString()&&QString::fromLatin1(patch.resultHash.toHex())==m.value("resultSha256").toString(),"Manifest/delta hash mismatch");
        patches.append(patch);
    }
    const auto directory=newDirectory(o.outputDirectory,game);
    newFile(directory+"/INCOMPLETE.txt","Assembly in progress. Do not install incomplete files.\n");
    for(const auto &patch:patches){
        QFile source(game+"/"+patch.name);check(source.open(QIODevice::ReadOnly),"Cannot read original archive for assembly");
        QSaveFile output(directory+"/"+patch.name);output.setDirectWriteFallback(false);check(output.open(QIODevice::WriteOnly),"Cannot create archive copy");
        const auto hashes=transform(source,patch,&output,o);
        check(hashes.first==patch.sourceHash,"Original archive SHA-256 differs. Package is for another game build or modified archive");
        check(hashes.second==patch.resultHash,"Assembled archive SHA-256 mismatch");
        check(output.commit(),"Cannot commit assembled archive copy");
        QFile verified(directory+"/"+patch.name);check(verified.open(QIODevice::ReadOnly),"Cannot verify assembled index");index(verified);
    }
    check(QFile::remove(directory+"/INCOMPLETE.txt"),"Cannot remove incomplete marker");
    newFile(directory+"/EXPERIMENTAL.txt","Structurally verified archive copies. UMAC and fonts remain unverified in game. Not automatically installed.\n");
    log(o,"Копії архівів зібрано поза грою: "+directory);
}
} // namespace anvil::packaging
