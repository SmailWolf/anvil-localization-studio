#include <QBuffer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <anvil/breakpoint.h>
#include <anvil/formatplugin.h>
#include <anvil/storage.h>
#include <stdexcept>
#include "../src/breakpoint/forgeheader.h"
namespace {
template <class T> void be(QByteArray &bytes, T n) {
    char b[sizeof(T)];
    qToBigEndian(n, b);
    bytes.append(b, sizeof(T));
}
template <class T> void le(QByteArray &bytes, T n) {
    char b[sizeof(T)];
    qToLittleEndian(n, b);
    bytes.append(b, sizeof(T));
}
QByteArray fixture() {
    QByteArray d;
    be<quint16>(d, 2);
    be<quint16>(d, 5);
    for (const auto &pair : QVector<QPair<quint16, quint16>>{{0, 0}, {'A', 0}, {0x416, 0}, {1, 2}, {3, 1}}) {
        be(d, pair.first);
        be(d, pair.second);
    }
    be<quint16>(d, 1);
    be<quint32>(d, 100);
    be<quint32>(d, 46);
    be<quint32>(d, 38);
    be<quint16>(d, 1);
    be<quint16>(d, 1);
    be<quint16>(d, 7);
    be<quint16>(d, 6);
    d += QByteArray::fromHex("000200ff0003");
    QByteArray p;
    le<quint64>(p, 12345);
    le<quint32>(p, 0x6e3c9c6f);
    p += QByteArray::fromHex("01000000000100000000000000000000f800000000b58983d2");
    le<quint32>(p, d.size());
    return p + d;
}
void setBE16(QByteArray &b, int at, quint16 n) { qToBigEndian(n, b.data() + at); }
void setBE32(QByteArray &b, int at, quint32 n) { qToBigEndian(n, b.data() + at); }
QByteArray rawDescriptor(const QByteArray &data) {
    QByteArray b;
    le<quint64>(b, 0x1004fa9957fbaa33ULL);
    le<quint16>(b, 3);
    b += char(3);
    le<quint16>(b, 32768);
    le<quint16>(b, 32768);
    le<quint32>(b, 1);
    le<quint32>(b, data.size());
    le<quint32>(b, data.size());
    quint32 a = 0, s = 0;
    for (unsigned char c : data) {
        a = (a + c) % 65521;
        s = (s + a) % 65521;
    }
    le<quint32>(b, s << 16 | a);
    return b + data;
}
} // namespace
class BreakpointTests : public QObject {
    Q_OBJECT
  private slots:
    void archiveRootSentinels() {
        // Fixed bytes from the observed v27 layout, scaled to the prototype's
        // three entries / one FileSet. Both sentinels must remain -1.
        const auto expected = QByteArray::fromHex(
            "0300000002000000000000000000000000000000"
            "ffffffffffffffff03000000010000004604000000000000");
        QCOMPARE(anvil::breakpoint::research::singleFileSetRoot(3), expected);
    }
    void codecIncompressibleBlocks() {
        const auto helper = qEnvironmentVariable("ANVIL_GRB_CODEC");
        const auto dll = qEnvironmentVariable("ANVIL_GRB_DLL");
        if (helper.isEmpty() || dll.isEmpty())
            QSKIP("Optional codec regression needs the built helper and an owned game DLL");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto localDll = temp.filePath("oo2core_7_win64.dll");
        QVERIFY(QFile::copy(dll, localDll));
        QByteArray random(32768, '\0');
        quint32 seed = 25349072;
        for (auto &c : random) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            c = char(seed >> 24);
        }
        const QVector<QByteArray> blocks = {QByteArray("metadata-block!!", 16), random,
                                           QByteArray(32768, 'A')};
        QByteArray jobs;
        le<quint32>(jobs, blocks.size());
        for (const auto &b : blocks) {
            le<quint32>(jobs, b.size());
            le<quint32>(jobs, b.size());
            jobs += b;
        }
        const auto input = temp.filePath("jobs.bin"), encoded = temp.filePath("encoded.bin"),
                   decoded = temp.filePath("decoded.bin");
        anvil::saveWithBackup(input, jobs);
        auto run = [&](const QString &source, const QString &destination, bool compress) {
            QProcess child;
            QStringList args;
#ifdef Q_OS_WIN
            child.setProgram(helper);
            args = {localDll, source, destination};
#else
            const auto prefix = qEnvironmentVariable("ANVIL_GRB_WINE_PREFIX");
            if (prefix.isEmpty())
                return false;
            child.setProgram("wine");
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("WINEPREFIX", prefix);
            env.insert("WINEDEBUG", "-all");
            env.insert("WINEDLLOVERRIDES", "mscoree,mshtml=");
            env.insert("XDG_DATA_HOME", prefix + "/xdg-data");
            env.insert("XDG_CONFIG_HOME", prefix + "/xdg-config");
            child.setProcessEnvironment(env);
            args = {helper, "Z:" + localDll, "Z:" + source, "Z:" + destination};
#endif
            if (compress)
                args << "compress";
            child.start(child.program(), args);
            if (!child.waitForFinished(45000)) {
                child.kill();
                child.waitForFinished(5000);
                return false;
            }
            return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
        };
        QVERIFY(run(input, encoded, true));
        const auto bytes = anvil::readFile(encoded);
        qsizetype at = 0;
        auto get = [&]() {
            if (at + 4 > bytes.size())
                throw std::runtime_error("Short codec response");
            quint32 value = 0;
            for (int i = 0; i < 4; ++i)
                value |= quint32(quint8(bytes.at(at++))) << (8 * i);
            return value;
        };
        QCOMPARE(get(), quint32(blocks.size()));
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            const auto raw = get(), stored = get();
            QCOMPARE(raw, quint32(blocks[i].size()));
            QVERIFY(stored > 0 && stored <= raw && stored <= bytes.size() - at);
            if (i < 2) {
                QCOMPARE(stored, raw);
                QCOMPARE(bytes.mid(at, stored), blocks[i]);
            } else {
                QVERIFY(stored < raw);
            }
            at += stored;
        }
        QCOMPARE(at, bytes.size());
        QVERIFY(run(encoded, decoded, false));
        QByteArray expected;
        le<quint32>(expected, blocks.size());
        for (const auto &b : blocks) {
            le<quint32>(expected, b.size());
            expected += b;
        }
        QCOMPARE(anvil::readFile(decoded), expected);
    }
    void grammarAndStableIds() {
        const auto d = anvil::breakpoint::decodeLocalization(fixture());
        QCOMPARE(d.game, QString("breakpoint"));
        QCOMPARE(d.sourceLanguage, QString("en-US"));
        QCOMPARE(d.entries.size(), 2);
        QCOMPARE(d.entries[0].id, QString("grb:12345:100"));
        QCOMPARE(d.entries[1].id, QString("grb:12345:107"));
        QCOMPARE(d.entries[0].source, QString("A"));
        QCOMPARE(d.entries[1].source, QString::fromUtf8("ЖAAЖA"));
        QCOMPARE(d.entries[1].status, QString("todo"));
        QVERIFY(anvil::fromJson(anvil::toJson(d)) == d);
    }
    void replacementWriter() {
        const auto source = fixture();
        QCOMPARE(anvil::breakpoint::replaceLocalization(source, {}), source);
        const QMap<quint32, QString> changes{{100, QStringLiteral("Налаштування")}};
        const auto out =
            anvil::breakpoint::decodeLocalization(anvil::breakpoint::replaceLocalization(source, changes));
        const auto before = anvil::breakpoint::decodeLocalization(source);
        QCOMPARE(out.entries.size(), before.entries.size());
        QCOMPARE(out.entries[0].id, before.entries[0].id);
        QCOMPARE(out.entries[0].source, QStringLiteral("Налаштування"));
        QCOMPARE(out.entries[1], before.entries[1]);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 anvil::breakpoint::replaceLocalization(source, {{999, "missing"}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 anvil::breakpoint::replaceLocalization(source, {{100, QString(QChar(0))}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::replaceLocalization(
                                                         source, {{100, QString(QChar(0xd800))}}));
    }
    void replacementSplitsEncodedBlocks() {
        const QMap<quint32, QString> changes{{100, QString(20000, 'B')}, {107, QString(20000, 'C')}};
        const auto out =
            anvil::breakpoint::decodeLocalization(anvil::breakpoint::replaceLocalization(fixture(), changes));
        QCOMPARE(out.entries[0].source, changes[100]);
        QCOMPARE(out.entries[1].source, changes[107]);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::replaceLocalization(
                                                         fixture(), {{100, QString(40000, 'B')}}));
    }
    void emptyPackage() {
        auto b = fixture().left(41);
        qToLittleEndian<quint32>(0, b.data() + 37);
        QVERIFY(anvil::breakpoint::decodeLocalization(b).entries.isEmpty());
    }
    void truncatedResource() {
        const auto b = fixture();
        for (int i = 0; i < b.size(); ++i)
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::decodeLocalization(b.left(i)));
    }
    void malformedGrammar_data() {
        QTest::addColumn<QByteArray>("bytes");
        auto b = fixture();
        setBE16(b, 41 + 4 + 3 * 4 + 2, 3);
        QTest::newRow("cycle") << b;
        b = fixture();
        setBE16(b, 41 + 4 + 4, 0xd800);
        QTest::newRow("broken surrogate") << b;
        b = fixture();
        setBE32(b, 41 + 30, 12);
        QTest::newRow("directory overlap") << b;
        b = fixture();
        setBE16(b, 41 + 42, 0);
        QTest::newRow("duplicate id") << b;
        b = fixture();
        setBE16(b, 41 + 40, 7);
        QTest::newRow("decreasing limits") << b;
        b = fixture();
        b[b.size() - 1] = char(255);
        QTest::newRow("invalid symbol") << b;
        b = fixture();
        b += char(0);
        qToLittleEndian<quint32>(b.size() - 41, b.data() + 37);
        QTest::newRow("trailing bytes") << b;
        b = fixture();
        b[12] = 2;
        QTest::newRow("unknown wrapper") << b;
    }
    void malformedGrammar() {
        QFETCH(QByteArray, bytes);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::decodeLocalization(bytes));
    }
    void containerValidation() {
        const auto bytes = rawDescriptor("metadata") + rawDescriptor("files");
        const auto plan = anvil::breakpoint::parseContainer(bytes);
        QCOMPARE(plan.blocks.size(), 2);
        QCOMPARE(plan.metadataBlocks, 1);
        QCOMPARE(plan.blocks[1].bytes, QByteArray("files"));
        auto bad = bytes;
        bad[31] = char(bad[31] ^ 1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::parseContainer(bad));
        bad = bytes;
        bad[10] = 4;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::parseContainer(bad));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::parseContainer(bytes + "x"));
        for (int i = 0; i < bytes.size(); ++i)
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::parseContainer(bytes.left(i)));
    }
    void resourceFraming() {
        const auto payload = fixture();
        const QByteArray name = "LocalizationPackage_English(US)";
        QByteArray files;
        le<quint32>(files, 0x6e3c9c6f);
        le<quint32>(files, payload.size());
        le<quint32>(files, name.size());
        files += name;
        files += char(0);
        files += payload;
        QByteArray meta;
        le<quint16>(meta, 1);
        le<quint64>(meta, 12345);
        le<quint32>(meta, files.size());
        le<quint16>(meta, 0);
        QCOMPARE(anvil::breakpoint::unwrapResource(meta, files, 12345, QString::fromLatin1(name)), payload);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::unwrapResource(
                                                         meta, files, 999, QString::fromLatin1(name)));
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            anvil::breakpoint::unwrapResource(meta, files + "junk", 12345, QString::fromLatin1(name)));
    }
    void archiveBounds() {
        QByteArray bytes(1200, '\0');
        bytes.replace(0, 9, QByteArray("scimitar\0", 9));
        qToLittleEndian<quint32>(27, bytes.data() + 9);
        qToLittleEndian<quint64>(1050, bytes.data() + 13);
        qToLittleEndian<quint32>(0, bytes.data() + 1050);
        qToLittleEndian<quint32>(1, bytes.data() + 1082);
        qToLittleEndian<qint64>(1094, bytes.data() + 1086);
        qToLittleEndian<qint64>(1142, bytes.data() + 1102);
        qToLittleEndian<qint64>(-1, bytes.data() + 1110);
        qToLittleEndian<qint64>(1142, bytes.data() + 1126);
        QBuffer stream(&bytes);
        QVERIFY(stream.open(QIODevice::ReadOnly));
        QVERIFY(anvil::breakpoint::englishIndex(stream).isEmpty());
        qToLittleEndian<qint64>(1094, bytes.data() + 1110);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::englishIndex(stream));
        qToLittleEndian<quint32>(9999999, bytes.data() + 1082);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::breakpoint::englishIndex(stream));
    }
    void plugin() {
        const auto path = qEnvironmentVariable("ANVIL_GRB_PLUGIN");
        if (path.isEmpty())
            QSKIP("Separate plugin test supplies its path");
        QPluginLoader loader(path);
        auto *object = loader.instance();
        QVERIFY2(object, qPrintable(loader.errorString()));
        auto *plugin = qobject_cast<anvil::FormatPlugin *>(object);
        QVERIFY(plugin);
        QVERIFY(plugin->recognizes(fixture()));
        QVERIFY(!plugin->recognizes("scimitar"));
        QCOMPARE(plugin->decode(fixture()).entries.size(), 2);
    }
    void outputGuard() {
        const auto extractor = qEnvironmentVariable("ANVIL_GRB_EXTRACTOR");
        if (extractor.isEmpty())
            QSKIP("Separate guard test supplies the executable");
        QTemporaryDir game, workspace;
        QVERIFY(game.isValid());
        QVERIFY(workspace.isValid());
        auto reject = [&](const QString &destination, const QByteArray &reason) {
            QProcess process;
            process.start(extractor, {"--game-dir", game.path(), "--output", destination});
            QVERIFY(process.waitForFinished(10000));
            QCOMPARE(process.exitCode(), 1);
            QVERIFY2(process.readAllStandardError().contains(reason), "Expected explicit refusal");
        };
        reject(game.filePath("forbidden.als.json"), "outside the game directory");
        reject(game.filePath("nested/forbidden.als.json"), "outside the game directory");
        QCOMPARE(QDir(game.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
        const auto existing = workspace.filePath("existing.als.json");
        anvil::saveWithBackup(existing, "original");
        reject(existing, "Output already exists");
        QCOMPARE(anvil::readFile(existing), QByteArray("original"));
#ifdef Q_OS_UNIX
        const auto link = workspace.filePath("game-link");
        QVERIFY(QFile::link(game.path(), link));
        reject(link + "/forbidden.als.json", "outside the game directory");
#endif
    }
    void patchOutputGuard() {
        const auto builder = qEnvironmentVariable("ANVIL_GRB_PATCH_BUILDER");
        if (builder.isEmpty())
            QSKIP("Separate patch guard supplies the executable");
        QTemporaryDir game, workspace;
        QVERIFY(game.isValid());
        QVERIFY(workspace.isValid());
        auto reject = [&](const QString &destination, const QByteArray &reason) {
            QProcess child;
            child.start(builder, {"--game-dir", game.path(), "--output-dir", destination});
            QVERIFY(child.waitForFinished(10000));
            QCOMPARE(child.exitCode(), 1);
            QVERIFY2(child.readAllStandardError().contains(reason),
                     "Expected refusal before reading or writing game data");
        };
        reject(game.filePath("test-patch"), "Refusing writes inside the game directory");
        reject(game.filePath("nested/test-patch"), "Refusing writes inside the game directory");
        reject(workspace.path(), "Output directory already exists");
        const auto freshOutput = workspace.filePath("new-patch");
        reject(freshOutput, "Patch generation disabled");
        QVERIFY(!QFileInfo::exists(freshOutput));
#ifdef Q_OS_UNIX
        const auto link = workspace.filePath("game-link");
        QVERIFY(QFile::link(game.path(), link));
        reject(link + "/test-patch", "Refusing writes inside the game directory");
#endif
        QCOMPARE(QDir(game.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }
    void localCorpus() {
        // Optional private-data regression: no game samples are bundled in the source tree.
        const auto root = qEnvironmentVariable("ANVIL_GRB_CORPUS");
        if (root.isEmpty())
            QSKIP("Private local regression, set ANVIL_GRB_CORPUS");
        const auto index = QJsonDocument::fromJson(anvil::readFile(root + "/decoded-index.json")).array();
        QCOMPARE(index.size(), 23);
        qsizetype total = 0;
        for (const auto &value : index) {
            const auto e = value.toObject();
            const auto filename =
                e.value("resources").toArray()[0].toObject().value("payload_file").toString();
            const auto doc =
                anvil::breakpoint::decodeLocalization(anvil::readFile(root + "/decoded/" + filename));
            const auto reference =
                QJsonDocument::fromJson(anvil::readFile(root + "/decoded/" + filename + ".json")).array();
            QCOMPARE(doc.entries.size(), reference.size());
            for (qsizetype i = 0; i < reference.size(); ++i) {
                const auto r = reference[i].toObject();
                QCOMPARE(doc.entries[i].source, r.value("text").toString());
                const auto id = r.value("key").toString().toULongLong() + r.value("delta").toInt();
                QVERIFY(doc.entries[i].id.endsWith(":" + QString::number(id)));
            }
            QVERIFY(anvil::fromJson(anvil::toJson(doc)) == doc);
            QVERIFY(anvil::fromCsv(anvil::toCsv(doc)) == doc.entries);
            total += doc.entries.size();
        }
        QCOMPARE(total, qsizetype(97156));
    }
};
QTEST_GUILESS_MAIN(BreakpointTests)
#include "breakpoint_tests.moc"
