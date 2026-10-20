// Experimental, deliberately narrow two-label test patch builder. Never installs a patch.
#include "forgeheader.h"
#include <QBuffer>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <anvil/breakpoint.h>
#include <anvil/storage.h>
#include <stdexcept>
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
void outside(const QString &path, const QString &game) {
    QFileInfo info(QFileInfo(path).absoluteFilePath());
    QString tail;
    while (!info.exists()) {
        tail = "/" + info.fileName() + tail;
        const auto parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            fail("Cannot resolve output path");
        info.setFile(parent);
    }
    const auto resolved = QDir::cleanPath(info.canonicalFilePath() + tail);
#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif
    if (resolved.compare(game, cs) == 0 || resolved.startsWith(game + "/", cs))
        fail("Refusing writes inside the game directory");
}
template <class T> T get(const QByteArray &bytes, qsizetype offset) {
    if (offset < 0 || offset > bytes.size() || qsizetype(sizeof(T)) > bytes.size() - offset)
        fail("Truncated integer field");
    quint64 value = 0;
    for (qsizetype i = 0; i < qsizetype(sizeof(T)); ++i)
        value |= quint64(quint8(bytes.at(offset + i))) << (8 * i);
    return static_cast<T>(value);
}
template <class T> void put(QByteArray &b, T value) {
    char buf[sizeof(T)];
    qToLittleEndian(value, buf);
    b.append(buf, sizeof(T));
}
template <class T> void set(QByteArray &b, qsizetype at, T value) {
    if (at < 0 || at + qsizetype(sizeof(T)) > b.size())
        fail("Internal writer range error");
    qToLittleEndian(value, b.data() + at);
}
quint32 checksum(const QByteArray &b) {
    quint32 a = 0, s = 0;
    for (unsigned char c : b) {
        a = (a + c) % 65521;
        s = (s + a) % 65521;
    }
    return s << 16 | a;
}
QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
struct Piece {
    quint64 id;
    QByteArray info, bytes;
    bool changed = false;
};
struct Donor {
    QByteArray header;
    QMap<quint64, Piece> pieces;
};
Donor readDonor(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(f.errorString());
    const QFileInfo before(f);
    auto read = [&](qint64 off, qint64 n) {
        if (off < 0 || n < 0 || off > f.size() || n > f.size() - off || !f.seek(off))
            fail("Invalid donor range");
        const auto b = f.read(n);
        if (b.size() != n)
            fail("Truncated donor");
        return b;
    };
    Donor donor;
    donor.header = read(0, 1050);
    // Reuse the independently tested full index validation before retaining donor metadata.
    const auto english = anvil::breakpoint::englishIndex(f);
    if (english.size() != 1 || english[0].id != 715061952218ULL)
        fail("This test builder requires the observed patch-01 layout");
    auto root = read(1050, 44);
    const auto expected = get<quint32>(root, 0);
    const auto sets = get<quint32>(root, 32);
    auto pos = get<qint64>(root, 36);
    quint64 total = 0;
    for (quint32 si = 0; si < sets; ++si) {
        const auto header = read(pos, 40);
        const auto n = get<quint32>(header, 0);
        if (n > expected || total + n > expected)
            fail("Invalid donor count");
        const auto offsets = read(get<qint64>(header, 8), qint64(n) * 20);
        const auto infos = read(get<qint64>(header, 32), qint64(n) * 192);
        for (quint32 i = 0; i < n; ++i) {
            const auto *row = offsets.constData() + 20 * i;
            const auto id = qFromLittleEndian<quint64>(row + 8);
            if (id == 16 || id == 145 || id == 715061952218ULL) {
                const auto len = qFromLittleEndian<qint32>(row + 16);
                if (len <= 0 || len > 2 * 1024 * 1024)
                    fail("Unexpected donor entry size");
                donor.pieces[id] = {id, infos.mid(192 * i, 192), read(qFromLittleEndian<qint64>(row), len),
                                    false};
            }
        }
        total += n;
        pos = get<qint64>(header, 16);
    }
    if (total != expected || pos != -1 || donor.pieces.size() != 3)
        fail("Missing or incomplete donor sidecars");
    const QFileInfo after(path);
    if (before.size() != after.size() || before.lastModified() != after.lastModified())
        fail("Donor changed while reading");
    return donor;
}
class Codec {
    QTemporaryDir temp_;
    QString helper_, wine_, prefix_, dll_;

  public:
    Codec(const QString &helper, const QString &dll, const QString &prefix)
        : helper_(helper), prefix_(prefix) {
        if (!temp_.isValid())
            fail("Cannot create codec workspace");
        dll_ = temp_.filePath("oo2core_7_win64.dll");
        if (!QFile::copy(dll, dll_))
            fail("Cannot copy codec DLL");
#ifndef Q_OS_WIN
        wine_ = QStandardPaths::findExecutable("wine");
        if (wine_.isEmpty())
            fail("Wine not found");
#endif
    }
    QVector<anvil::breakpoint::Block> run(const QVector<anvil::breakpoint::Block> &jobs, bool compress) {
        QByteArray input;
        put<quint32>(input, jobs.size());
        for (const auto &job : jobs) {
            put(input, job.rawSize);
            put<quint32>(input, job.bytes.size());
            input += job.bytes;
        }
        const auto in = temp_.filePath("input.bin"), out = temp_.filePath("output.bin");
        anvil::saveWithBackup(in, input);
        QFile::remove(out);
        QProcess child;
        QStringList arguments;
#ifdef Q_OS_WIN
        child.setProgram(helper_);
        arguments = {QDir::toNativeSeparators(dll_), QDir::toNativeSeparators(in),
                     QDir::toNativeSeparators(out)};
#else
        child.setProgram(wine_);
        arguments = {helper_, "Z:" + dll_, "Z:" + in, "Z:" + out};
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix_);
        env.insert("WINEDEBUG", "-all");
        env.insert("WINEDLLOVERRIDES", "mscoree,mshtml=");
        env.insert("XDG_DATA_HOME", prefix_ + "/xdg-data");
        env.insert("XDG_CONFIG_HOME", prefix_ + "/xdg-config");
        child.setProcessEnvironment(env);
#endif
        if (compress)
            arguments << "compress";
        child.setArguments(arguments);
        child.start();
        if (!child.waitForStarted(10000))
            fail(child.errorString());
        if (!child.waitForFinished(120000)) {
            child.kill();
            child.waitForFinished(5000);
            fail("Codec timeout");
        }
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
            fail("Codec failed: " + QString::fromUtf8(child.readAllStandardError()).left(2000));
        if (QFileInfo(out).size() > 32 * 1024 * 1024)
            fail("Codec output exceeds limit");
        const auto bytes = anvil::readFile(out);
        qsizetype at = 0;
        auto get = [&]() {
            if (bytes.size() - at < 4)
                fail("Short codec output");
            const auto n = qFromLittleEndian<quint32>(bytes.constData() + at);
            at += 4;
            return n;
        };
        if (get() != jobs.size())
            fail("Codec block count mismatch");
        QVector<anvil::breakpoint::Block> result;
        for (const auto &job : jobs) {
            const auto raw = get(), stored = compress ? get() : raw;
            if (raw != job.rawSize || stored > 65536 || stored > bytes.size() - at)
                fail("Codec output length mismatch");
            result.append({raw, bytes.mid(at, stored)});
            at += stored;
        }
        if (at != bytes.size())
            fail("Trailing codec output");
        return result;
    }
    QPair<QByteArray, QByteArray> decode(const QByteArray &container) {
        const auto plan = anvil::breakpoint::parseContainer(container);
        const auto blocks = run(plan.blocks, false);
        QByteArray metadata, files;
        for (qsizetype i = 0; i < blocks.size(); ++i)
            (i < plan.metadataBlocks ? metadata : files) += blocks[i].bytes;
        return {metadata, files};
    }
    QByteArray encode(const QByteArray &metadata, const QByteArray &files) {
        QVector<anvil::breakpoint::Block> raw;
        QVector<qsizetype> counts;
        for (const auto &stream : {metadata, files}) {
            qsizetype n = 0;
            for (qsizetype i = 0; i < stream.size(); i += 32768) {
                const auto bytes = stream.mid(i, 32768);
                raw.append({quint32(bytes.size()), bytes});
                ++n;
            }
            counts.append(n);
        }
        const auto compressed = run(raw, true);
        QByteArray result;
        qsizetype cursor = 0;
        for (auto count : counts) {
            put<quint64>(result, 0x1004fa9957fbaa33ULL);
            put<quint16>(result, 3);
            result += char(3);
            put<quint16>(result, 32768);
            put<quint16>(result, 32768);
            put<quint32>(result, count);
            for (qsizetype i = 0; i < count; ++i) {
                const auto &b = compressed[cursor + i];
                put(result, b.rawSize);
                put<quint32>(result, b.bytes.size());
            }
            for (qsizetype i = 0; i < count; ++i) {
                const auto &b = compressed[cursor + i];
                put(result, checksum(b.bytes));
                result += b.bytes;
            }
            cursor += count;
        }
        if (decode(result) != qMakePair(metadata, files))
            fail("Rebuilt container failed full round trip");
        return result;
    }
};
QByteArray prefetch() {
    QByteArray table;
    put<quint32>(table, 1);
    put<quint64>(table, 715061952218ULL);
    put<quint32>(table, 4);
    put<quint32>(table, 0);
    put<quint32>(table, 4);
    put<quint32>(table, 0);
    // Legal literal-only LZO1X stream, independently checked with liblzo2 in validation.
    QByteArray lzo;
    lzo += char(17 + table.size());
    lzo += table;
    lzo += QByteArray::fromHex("110000");
    QByteArray frame;
    put<quint64>(frame, 0x1004fa9957fbaa33ULL);
    put<quint16>(frame, 3);
    frame += char(0);
    put<quint16>(frame, 32768);
    put<quint16>(frame, 0);
    frame += char(1);
    put<quint32>(frame, lzo.size());
    put<quint32>(frame, table.size());
    put(frame, checksum(lzo));
    frame += lzo;
    return frame;
}
QByteArray forge(const QByteArray &prefix, const QVector<Piece> &entries) {
    const quint32 count = entries.size();
    QByteArray result = prefix;
    result += anvil::breakpoint::research::singleFileSetRoot(count);
    const qint64 offsets = 1142, info = offsets + 20 * count, body = info + 192 * count;
    put(result, count);
    put<quint32>(result, 2);
    put(result, offsets);
    put<qint64>(result, -1);
    put<qint32>(result, 0);
    put<qint32>(result, count - 1);
    put(result, info);
    put(result, body);
    qint64 cursor = body;
    for (const auto &e : entries) {
        put(result, cursor);
        put(result, e.id);
        put<qint32>(result, e.bytes.size());
        cursor += e.bytes.size();
    }
    for (qsizetype i = 0; i < entries.size(); ++i) {
        auto metadata = entries[i].info;
        if (metadata.size() != 192)
            fail("Invalid donor metadata size");
        set<qint32>(metadata, 0, entries[i].bytes.size());
        // No UMAC implementation is claimed. Zero denotes an unset metadata hash;
        // acceptance by GRB remains an explicit in-game validation requirement.
        if (entries[i].changed)
            set<quint64>(metadata, 4, 0);
        set<qint32>(metadata, 28, i + 1 < entries.size() ? i + 1 : -1);
        set<qint32>(metadata, 32, i ? i - 1 : -1);
        result += metadata;
    }
    if (result.size() != body)
        fail("Internal forge table size mismatch");
    for (const auto &e : entries)
        result += e.bytes;
    result += QByteArray((32768 - result.size() % 32768) % 32768, '\0');
    return result;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("anvil-build-breakpoint-test");
    QCommandLineParser args;
    args.addHelpOption();
    args.setApplicationDescription(
        "WITHDRAWN: generated patch caused a reported game crash. Patch generation is disabled.");
    args.addOptions({{"game-dir", "Breakpoint installation directory", "path"},
                     {"output-dir", "New output directory outside game directory", "path"},
                     {"wine-prefix", "Dedicated Linux Wine prefix outside game directory", "path"},
                     {"helper", "Oodle helper executable", "path"}});
    args.process(app);
    try {
        const auto game = QFileInfo(args.value("game-dir")).canonicalFilePath();
        if (game.isEmpty() || !QFileInfo(game).isDir())
            fail("Specify --game-dir");
        if (args.value("output-dir").isEmpty())
            fail("Specify --output-dir");
        const auto output = QFileInfo(args.value("output-dir")).absoluteFilePath();
        outside(output, game);
        if (QFileInfo::exists(output))
            fail("Output directory already exists; choose a new one");
        if (!QFileInfo(QFileInfo(output).absolutePath()).isDir())
            fail("Output parent does not exist");
        // Byte-level round trips did not establish engine compatibility. Keep the
        // prototype for research, but refuse all generation until that is resolved.
        fail("Patch generation disabled: the 2026-10-19 candidate caused a reported game crash. "
             "Do not install DataPC_patch_02.forge from that package.");
        const auto prefix = QFileInfo(args.value("wine-prefix")).absoluteFilePath();
#ifndef Q_OS_WIN
        if (!args.isSet("wine-prefix"))
            fail("Specify --wine-prefix on Linux");
        outside(prefix, game);
#endif
        const auto helper = args.isSet("helper") ? QFileInfo(args.value("helper")).absoluteFilePath()
                                                 : app.applicationDirPath() + "/anvil-oodle-helper.exe";
        if (!QFileInfo(helper).isFile())
            fail("Decoder helper not found");
        auto donor = readDonor(game + "/DataPC_patch_01.forge");
        Codec codec(helper, game + "/oo2core_7_win64.dll", prefix);
        const auto streams = codec.decode(donor.pieces[715061952218ULL].bytes);
        const QString name = "LocalizationPackage_English(US)";
        const auto original =
            anvil::breakpoint::unwrapResource(streams.first, streams.second, 715061952218ULL, name);
        const auto before = anvil::breakpoint::decodeLocalization(original);
        if (before.entries.size() != 20439)
            fail("This test requires the observed 20439-string English UI package");
        QMap<quint32, QString> replacements;
        QJsonArray changes;
        for (const auto &e : before.entries) {
            QString target;
            if (e.source == "OPTIONS" || e.source == "SETTINGS")
                target = QStringLiteral("НАЛАШТУВАННЯ");
            else if (e.source == "Settings")
                target = QStringLiteral("Налаштування");
            else if (e.source == "Main Menu")
                target = QStringLiteral("Головне меню");
            else
                continue;
            const auto id = e.id.section(':', -1).toUInt();
            replacements[id] = target;
            changes.append(
                QJsonObject{{"id", QString::number(id)}, {"source", e.source}, {"translation", target}});
        }
        if (replacements.size() != 10)
            fail("Unexpected menu label count; refusing a different game build");
        const auto payload = anvil::breakpoint::replaceLocalization(original, replacements);
        QByteArray files;
        put<quint32>(files, 0x6e3c9c6f);
        put<qint32>(files, payload.size());
        put<qint32>(files, name.size());
        files += name.toLatin1();
        files += char(0);
        files += payload;
        auto metadata = streams.first;
        set<quint32>(metadata, 10, files.size());
        auto localization = donor.pieces[715061952218ULL];
        localization.bytes = codec.encode(metadata, files);
        localization.changed = true;
        auto pfi = donor.pieces[145];
        pfi.bytes = prefetch();
        pfi.changed = true;
        const auto patch = forge(donor.header, {localization, donor.pieces[16], pfi});
        QBuffer probe;
        probe.setData(patch);
        probe.open(QIODevice::ReadOnly);
        const auto entries = anvil::breakpoint::englishIndex(probe);
        if (entries.size() != 1 || entries[0].id != localization.id)
            fail("Output forge index verification failed");
        const auto readBack = codec.decode(patch.mid(entries[0].offset, entries[0].size));
        if (anvil::breakpoint::unwrapResource(readBack.first, readBack.second, localization.id, name) !=
            payload)
            fail("Output forge payload verification failed");
        QJsonObject manifest{
            {"formatVersion", 1},
            {"status", "experimental-not-tested-in-game"},
            {"game", "Ghost Recon Breakpoint"},
            {"steamBuildId", "25349072"},
            {"languageToSelectInGame", "English"},
            {"patchFile", "DataPC_patch_02.forge"},
            {"changedStrings", changes},
            {"originalStringCount", before.entries.size()},
            {"changedStringCount", replacements.size()},
            {"packageId", QString::number(localization.id)},
            {"sha256", hash(patch)},
            {"donorLocalizationSha256", hash(donor.pieces[localization.id].bytes)},
            {"donorMetaFileSha256", hash(donor.pieces[16].bytes)},
            {"metadataHashPolicy",
             "UMAC not implemented; changed entries use zero/unset; runtime acceptance unverified"},
            {"gameFilesWritten", false},
            {"inGameTested", false}};
        const QString readme = QStringLiteral(
            "ТЕСТОВИЙ ПАТЧ BREAKPOINT — НАЛАШТУВАННЯ / ГОЛОВНЕ МЕНЮ\n\nСтатус: експериментальний. Структура "
            "і повторне декодування перевірені; у грі не запускався.\n\nПакет змінює тільки 10 точних "
            "повторів OPTIONS / SETTINGS / Settings / Main Menu в англійському UI-пакеті. Решта 20429 рядків "
            "цього пакета не змінені.\n\nДля ручного тесту: закрийте гру. Якщо у грі вже є "
            "DataPC_patch_02.forge, не замінюйте його — цей тест не об’єднує моди. Скопіюйте тільки "
            "DataPC_patch_02.forge із цього пакета в кореневу папку Breakpoint. Виберіть English для тексту "
            "інтерфейсу.\n\nВидалення: закрийте гру та видаліть тільки доданий вами DataPC_patch_02.forge. "
            "Оригінальні DataPC.forge та DataPC_patch_01.forge замінювати не потрібно.\n\nПеревірте: чи "
            "завантажується меню, чи з’явилися українські написи, чи немає квадратів замість літер. Якщо гра "
            "не завантажується — приберіть цей тестовий файл.\n\nНевизначеності: новий standalone patch-02 з "
            "donor MetaFile не перевірено в рушії; UMAC для змінених записів не обчислюється "
            "(нульовий/unset); покриття українських літер шрифтами ще не підтверджене. Тому це кандидат для "
            "тесту, а не підтверджений робочий українізатор.\n\nФайлів встановленої гри під час створення не "
            "змінено. Не копіюйте manifest.json або цей текст у гру.\n");
        const QFileInfo outInfo(output);
        if (!QDir(outInfo.absolutePath()).mkdir(outInfo.fileName()))
            fail("Cannot exclusively create output directory");
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile f(output + "/" + name);
            if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly) || f.write(bytes) != bytes.size() ||
                !f.flush())
                fail("Cannot write patch package");
        };
        write("DataPC_patch_02.forge", patch);
        write("manifest.json", QJsonDocument(manifest).toJson());
        write("ПРОЧИТАЙТЕ.txt", readme.toUtf8());
        QTextStream(stdout) << "Created experimental test patch: " << output
                            << "\nChanged strings: " << replacements.size()
                            << "; archive bytes: " << patch.size() << "\nNOT installed or tested in game.\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "Test patch failed: " << QString::fromUtf8(e.what()) << "\n";
        return 1;
    }
}
