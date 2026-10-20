#include <anvil/fonts.h>
#include <anvil/storage.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
static void fontResources() { static const bool initialized=[] { Q_INIT_RESOURCE(fonts);return true; }(); (void)initialized; }
namespace anvil::fonts {
namespace {
constexpr qsizetype MaxBytes=16*1024*1024;
const QString Ukrainian=QString::fromUtf8("АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя");
const QString Belarusian=QString::fromUtf8("АБВГДЕЁЖЗІЙКЛМНОПРСТУЎФХЦЧШЫЬЭЮЯабвгдеёжзійклмнопрстуўфхцчшыьэюя");
void check(bool condition,const char *message) { if(!condition) throw std::runtime_error(message); }
QByteArray read(const QString &path) {
    QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot open font/donor file");
    check(file.size()>0&&file.size()<=MaxBytes,"Font/donor size exceeds 16 MiB limit");
    return file.readAll();
}
void write(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create font kit file");
    check(file.write(bytes)==bytes.size()&&file.commit(),"Cannot save font kit file");
}
QRawFont face(const QString &path,int size) {
    QRawFont font(read(path),size,QFont::PreferNoHinting);
    check(font.isValid(),"Invalid TTF/OTF font");return font;
}
QString missing(const QRawFont &font,const QString &required) {
    QString result;
    for(const auto c:required)if(!font.supportsCharacter(c))result+=c;
    return result;
}
quint32 u32(const QByteArray &b,qsizetype at) { return qFromLittleEndian<quint32>(b.constData()+at); }
void set32(QByteArray &b,qsizetype at,quint32 value) { qToLittleEndian(value,b.data()+at); }
float f32(const QByteArray &b,qsizetype at) { auto bits=u32(b,at);float v;std::memcpy(&v,&bits,4);return v; }
void setFloat(QByteArray &b,qsizetype at,float v) { quint32 bits;std::memcpy(&bits,&v,4);set32(b,at,bits); }
struct Glyph { quint32 cp;std::array<float,7> metrics;QByteArray pixels; };
struct Native { QByteArray header;QVector<Glyph> glyphs; };
Native decode(const QByteArray &blob) {
    check(blob.size()>=44&&blob.size()<=MaxBytes&&blob.first(8)==QByteArray("GFOF\3\3\0\0",8),"Unsupported or truncated raw GFOF v3.3 font");
    auto count=u32(blob,20);check(count>=1&&count<=4096&&44+count*36<=quint32(blob.size()),"Invalid glyph count");
    Native n{blob.first(44),{}};QSet<quint32> seen;qsizetype cursor=44+count*36;
    for(quint32 i=0;i<count;++i) {
        qsizetype pos=44+i*36;Glyph g;g.cp=u32(blob,pos);
        check(g.cp<=0x10ffff&&!(g.cp>=0xd800&&g.cp<=0xdfff)&&!seen.contains(g.cp),"Invalid or duplicate codepoint");
        for(int k=0;k<7;++k){g.metrics[k]=f32(blob,pos+4+k*4);check(std::isfinite(g.metrics[k])&&std::abs(g.metrics[k])<=65536,"Invalid glyph metrics");}
        const auto w=g.metrics[5],h=g.metrics[6];
        check(w>=0&&h>=0&&w<=1024&&h<=1024&&w==std::floor(w)&&h==std::floor(h),"Invalid bitmap dimensions");
        qsizetype bytes=qsizetype(w)*qsizetype(h);
        check(u32(blob,pos+32)==cursor&&bytes<=blob.size()-cursor,"Invalid bitmap offset or size");
        g.pixels=blob.mid(cursor,bytes);cursor+=bytes;seen.insert(g.cp);n.glyphs.append(g);
    }
    check(cursor==blob.size(),"Trailing font bytes");return n;
}
QByteArray encode(const Native &n) {
    check(n.header.size()==44&&n.glyphs.size()>=1&&n.glyphs.size()<=4096,"Invalid glyph list");
    QByteArray out=n.header;out.resize(44+n.glyphs.size()*36);set32(out,20,n.glyphs.size());
    for(qsizetype i=0;i<n.glyphs.size();++i) {
        const auto &g=n.glyphs[i];auto pos=44+i*36;set32(out,pos,g.cp);
        for(int k=0;k<7;++k)setFloat(out,pos+4+k*4,g.metrics[k]);
        set32(out,pos+32,out.size());check(g.metrics[5]*g.metrics[6]==g.pixels.size(),"Bitmap size mismatch");
        check(out.size()+g.pixels.size()<=MaxBytes,"Font exceeds size limit");out+=g.pixels;
    }
    decode(out);return out;
}
// Exact squared Euclidean distance transform, separable lower-envelope method.
void distanceLine(const QVector<double> &f,QVector<double> &d) {
    const int n=f.size();QVector<int> v(n);QVector<double> z(n+1);int k=0;v[0]=0;z[0]=-1e30;z[1]=1e30;
    for(int q=1;q<n;++q) {
        double s;
        do { const int p=v[k];s=((f[q]+q*q)-(f[p]+p*p))/(2.*(q-p));if(s>z[k])break;--k; } while(k>=0);
        ++k;v[k]=q;z[k]=s;z[k+1]=1e30;
    }
    k=0;d.resize(n);
    for(int q=0;q<n;++q){while(z[k+1]<q)++k;d[q]=(q-v[k])*(q-v[k])+f[v[k]];}
}
QVector<double> distances(const QImage &image,bool toInside) {
    const int w=image.width(),h=image.height();QVector<double> tmp(w*h),out(w*h),f,d;
    f.resize(w);
    for(int y=0;y<h;++y){for(int x=0;x<w;++x)f[x]=((qGray(image.pixel(x,y))>=128)==toInside)?0.:1e12;distanceLine(f,d);for(int x=0;x<w;++x)tmp[y*w+x]=d[x];}
    f.resize(h);
    for(int x=0;x<w;++x){for(int y=0;y<h;++y)f[y]=tmp[y*w+x];distanceLine(f,d);for(int y=0;y<h;++y)out[y*w+x]=d[y];}
    return out;
}
Glyph raster(const QRawFont &font,quint32 cp,int spread) {
    const auto text=QString::fromUcs4(&cp,1);const auto indexes=font.glyphIndexesForString(text);
    check(!indexes.isEmpty()&&indexes[0]!=0,"Font lacks required glyph");
    const auto index=indexes[0];const auto path=font.pathForGlyph(index);Glyph g{cp,{}, {}};
    g.metrics[0]=font.advancesForGlyphIndexes({index})[0].x();if(path.isEmpty())return g;
    constexpr int Scale=4;
    const auto bounds=path.boundingRect();const int pad=spread+1;
    const int left=std::floor(bounds.left())-pad,top=std::floor(bounds.top())-pad;
    const int w=std::ceil(bounds.right())+pad-left,h=std::ceil(bounds.bottom())+pad-top;
    check(w>0&&h>0&&w<=256&&h<=256,"Rasterized glyph exceeds size limit");
    QImage image(w*Scale,h*Scale,QImage::Format_RGB32);image.fill(Qt::black);
    { QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(Scale,Scale);painter.translate(-left,-top);painter.fillPath(path,Qt::white); }
    const auto toInside=distances(image,true),toOutside=distances(image,false);g.pixels.resize(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int at=(y*Scale+Scale/2)*image.width()+x*Scale+Scale/2;
        double distance=std::sqrt(toOutside[at])-std::sqrt(toInside[at]);
        distance=(distance-(distance>0?.5:-.5))/Scale;
        g.pixels[y*w+x]=char(std::clamp(int(std::lround(128+distance*127.5/spread)),0,255));
    }
    g.metrics={g.metrics[0],float(left),float(-(top+h)),float(w),float(h),float(w),float(h)};return g;
}
}
QString bundledFont(){fontResources();return ":/assets/fonts/NotoSans-Regular.ttf";}
QString requiredCharacters(const QString &language) {
    check(language=="uk"||language=="be"||language=="both","Choose uk, be or both");
    QString chars;
    for(int i=32;i<=126;++i)chars+=QChar(i);
    chars+=QString::fromUtf8("’ʼ");if(language!="be")chars+=Ukrainian;if(language!="uk")chars+=Belarusian;
    auto points=chars.toUcs4();std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    return QString::fromUcs4(points.constData(),points.size());
}
QJsonObject inspect(const QString &path,const QString &language) {
    const auto font=face(path,50);const auto required=requiredCharacters(language),absent=missing(font,required);
    return {{"family",font.familyName()},{"language",language},{"requiredCharacters",required},{"missingCharacters",absent},
        {"coverageVerified",absent.isEmpty()},{"fontSha256",QString::fromLatin1(QCryptographicHash::hash(read(path),QCryptographicHash::Sha256).toHex())},
        {"engineRenderingTested",false},{"installedInGame",false}};
}
QJsonObject inspectGfof(const QByteArray &blob,const QString &language) {
    const auto n=decode(blob);QSet<quint32> points;for(const auto &g:n.glyphs)points.insert(g.cp);
    QString absent;for(auto cp:requiredCharacters(language).toUcs4())if(!points.contains(cp))absent+=QString::fromUcs4(&cp,1);
    return {{"glyphCount",n.glyphs.size()},{"missingCharacters",absent},{"coverageVerified",absent.isEmpty()},
        {"roundtripExact",encode(n)==blob},{"engineRenderingTested",false}};
}
QByteArray compileGfof(const QString &path,const QByteArray &donor,const QString &language,int pixelSize,int spread) {
    check(pixelSize>=20&&pixelSize<=100&&spread>=1&&spread<=16,"Invalid font size or SDF spread");
    auto n=decode(donor);check(encode(n)==donor,"Donor does not round-trip exactly");auto font=face(path,pixelSize);
    const auto required=requiredCharacters(language);check(missing(font,required).isEmpty(),"Font lacks required language glyphs");
    QMap<quint32,Glyph> glyphs;for(const auto &g:n.glyphs)glyphs.insert(g.cp,g);
    for(auto cp:required.toUcs4())if(!glyphs.contains(cp))glyphs.insert(cp,Glyph{cp,{}, {}});
    n.glyphs.clear();
    for(auto it=glyphs.cbegin();it!=glyphs.cend();++it)
        n.glyphs.append(font.supportsCharacter(it.key())?raster(font,it.key(),spread):it.value());
    return encode(n);
}
void exportKit(const QString &path,const QString &output,const QString &language,const QString &donor,int pixelSize,int spread) {
    // Resolve links and inspect every existing ancestor; never create a font kit under an installed game.
    auto dest=QFileInfo(output).absoluteFilePath();
    check(!QFileInfo::exists(dest),"Choose a new output directory");
    QString parent=QFileInfo(dest).absolutePath();
    while(true) {
        if(QFileInfo(parent).exists()) {
            const auto canonical=QFileInfo(parent).canonicalFilePath();
            for(QString at=canonical;;at=QFileInfo(at).absolutePath()) {
                check(!QFileInfo::exists(at+"/GRB.exe")&&!QFileInfo::exists(at+"/GRW.exe")&&!QFileInfo::exists(at+"/DataPC.forge"),"Output must be outside the game directory");
                if(QFileInfo(at).absolutePath()==at)break;
            }
            break;
        }
        check(QFileInfo(parent).absolutePath()!=parent,"Cannot resolve output directory");parent=QFileInfo(parent).absolutePath();
    }
    auto report=inspect(path,language);check(report["coverageVerified"].toBool(),"Font lacks required language glyphs");
    QByteArray native;
    if(!donor.isEmpty()) {
        const auto original=read(donor);native=compileGfof(path,original,language,pixelSize,spread);
        report["gfof"]=inspectGfof(native,language);report["donorSha256"]=QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex());
        report["gfofSha256"]=QString::fromLatin1(QCryptographicHash::hash(native,QCryptographicHash::Sha256).toHex());
        report["pixelSize"]=pixelSize;report["sdfSpread"]=spread;
    }
    check(QDir().mkpath(dest),"Cannot create font output directory");
    try {
        const bool bundled=path==bundledFont();
        const QString fontName=read(path).startsWith("OTTO")?"font.otf":"font.ttf";
        write(dest+"/"+fontName,read(path));
        if(bundled)write(dest+"/OFL.txt",read(":/assets/fonts/OFL.txt"));
        if(!native.isEmpty())write(dest+"/experimental-font.gfof",native);
        report["fontFile"]=fontName;report["license"]=bundled?"OFL-1.1 (OFL.txt)":"User supplied: include the font author's license before distribution";
        write(dest+"/validation.json",QJsonDocument(report).toJson());
        write(dest+"/README.txt",QByteArray("ANVIL FONT KIT - BETA\n\nCoverage has been checked for the selected language(s).\nTTF/OTF is a source font, not a drop-in Anvil game patch.\nAn optional experimental GFOF v3.3 blob uses the separately extracted donor layout.\nNo installed game files were changed. Game rendering is NOT verified.\nWildlands native font containers have NOT been verified.\nThe translation packer does not insert these fonts into game archives.\nKeep this kit outside the game. Do not replace game archives with font files.\nInclude the font license when distributing derived fonts.\n"));
    } catch(...) {QDir(dest).removeRecursively();throw;}
}
}
