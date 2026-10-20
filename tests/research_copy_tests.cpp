#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include "../src/breakpoint/forgeheader.h"

namespace {
constexpr quint64 UiId = 715061952218ULL;
constexpr qint64 UiOffset = 1990;
template <class T> void put(QByteArray &b, T value) {
    char bytes[sizeof(T)];
    qToLittleEndian(value, bytes);
    b.append(bytes, sizeof(T));
}
template <class T> void set(QByteArray &b, qsizetype at, T value) {
    qToLittleEndian(value, b.data() + at);
}
QByteArray frame(const QByteArray &raw) {
    QByteArray b;
    put<quint64>(b, 0x1004fa9957fbaa33ULL);
    put<quint16>(b, 3);
    b += char(3);
    put<quint16>(b, 32768);
    put<quint16>(b, 32768);
    put<quint32>(b, 1);
    put<quint32>(b, raw.size());
    put<quint32>(b, raw.size());
    quint32 a = 0, s = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        s = (s + a) % 65521;
    }
    put<quint32>(b, s << 16 | a);
    return b + raw;
}
QByteArray container(qsizetype size, char c, quint64 id = UiId) {
    QByteArray metadata;
    put<quint16>(metadata, 1);
    put(metadata, id);
    put<quint32>(metadata, size);
    put<quint16>(metadata, 0);
    return frame(metadata) + frame(QByteArray(size, c));
}
QByteArray archive() {
    QByteArray b(1050, '\0');
    b.replace(0, 9, QByteArray("scimitar\0", 9));
    set<quint32>(b, 9, 27);
    set<qint64>(b, 13, 1050);
    b += anvil::breakpoint::research::singleFileSetRoot(4);
    put<quint32>(b, 4);
    put<quint32>(b, 2);
    put<qint64>(b, 1142);
    put<qint64>(b, -1);
    put<qint32>(b, 0);
    put<qint32>(b, 3);
    put<qint64>(b, 1222);
    put<qint64>(b, UiOffset);
    const QVector<quint64> ids{UiId, 42, 16, 145};
    const QVector<QByteArray> pieces{container(100, 'O'), "unrelated-entry", "original-meta", "original-prefetch"};
    qint64 offset = UiOffset;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        put(b, offset);
        put(b, ids[i]);
        put<qint32>(b, pieces[i].size());
        offset += pieces[i].size();
    }
    for (qsizetype i = 0; i < ids.size(); ++i) {
        QByteArray row(192, '\0');
        set<qint32>(row, 0, pieces[i].size());
        set<quint64>(row, 4, 0x1122334455667788ULL);
        set<quint32>(row, 16, i ? 0 : 0x6e3c9c6f);
        const QByteArray name = i ? QByteArray::number(ids[i]) : QByteArray("LocalizationPackage_English(US)");
        row.replace(44, name.size(), name);
        b += row;
    }
    for (const auto &piece : pieces)
        b += piece;
    return b;
}
bool save(const QString &path, const QByteArray &b) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::NewOnly) && f.write(b) == b.size();
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
struct Result { int code; QByteArray error; };
Result run(const QString &game, const QString &replacement, const QString &output) {
    QProcess p;
    p.start(qEnvironmentVariable("ANVIL_GRB_RESEARCH_COPY"),
            {"--game-dir", game, "--replacement", replacement, "--output-dir", output});
    if (!p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(5000);
        return {-99, "Child process timeout"};
    }
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -98, p.readAllStandardError()};
}
} // namespace

class ResearchCopyTests : public QObject {
    Q_OBJECT
  private slots:
    void editsOnlyOneSlot() {
        QTemporaryDir game, workspace;
        QVERIFY(game.isValid() && workspace.isValid());
        const auto original = archive(), replacement = container(80, 'N');
        const auto source = game.filePath("DataPC_patch_01.forge"), input = workspace.filePath("ui.data"),
                   output = workspace.filePath("experiment");
        QVERIFY(save(source, original));
        QVERIFY(save(input, replacement));
        const auto result = run(game.path(), input, output);
        QCOMPARE(result.code, 0);
        QCOMPARE(read(source), original);
        QCOMPARE(read(output + "/control.forge.research"), original);
        auto expected = original;
        set<qint32>(expected, 1142 + 16, replacement.size());
        set<qint32>(expected, 1222, replacement.size());
        expected.replace(UiOffset, replacement.size(), replacement);
        expected.replace(UiOffset + replacement.size(), 20, QByteArray(20, '\0'));
        QCOMPARE(read(output + "/modified.forge.research"), expected);
        const auto report = QJsonDocument::fromJson(read(output + "/report.json")).object();
        QCOMPARE(report.value("unchangedEntryCount").toInt(), 3);
        QCOMPARE(report.value("opaqueHashBytesPreserved").toString(), "8877665544332211");
        QVERIFY(report.value("allBytesMatchExpected").toBool());
        QVERIFY(report.value("doNotInstall").toBool());
        QVERIFY(!report.value("umacRecomputed").toBool());
        QVERIFY(!report.value("gameFilesWritten").toBool());
    }
    void noOpIsByteIdentical() {
        QTemporaryDir game, workspace;
        const auto original = archive();
        const auto source = game.filePath("DataPC_patch_01.forge"), input = workspace.filePath("ui.data"),
                   output = workspace.filePath("noop");
        QVERIFY(save(source, original));
        QVERIFY(save(input, container(100, 'O')));
        QCOMPARE(run(game.path(), input, output).code, 0);
        QCOMPARE(read(output + "/modified.forge.research"), original);
    }
    void rejectsUnsafeOutput() {
        QTemporaryDir game, workspace;
        const auto original = archive();
        const auto source = game.filePath("DataPC_patch_01.forge"), input = workspace.filePath("ui.data");
        QVERIFY(save(source, original));
        QVERIFY(save(input, container(80, 'N')));
        QVERIFY(run(game.path(), input, game.filePath("new-copy")).error.contains("Refusing writes"));
        QVERIFY(run(game.path(), input, game.filePath("nested/new-copy")).error.contains("Refusing writes"));
        QVERIFY(save(workspace.filePath("sentinel"), "keep"));
        QVERIFY(run(game.path(), input, workspace.path()).error.contains("already exists"));
        QCOMPARE(read(workspace.filePath("sentinel")), QByteArray("keep"));
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(game.path(), workspace.filePath("game-link")));
        QVERIFY(run(game.path(), input, workspace.filePath("game-link/copy")).error.contains("Refusing writes"));
#endif
        QCOMPARE(read(source), original);
        QCOMPARE(QDir(game.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
    }
    void rejectsMalformedInputs_data() {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QByteArray>("replacement");
        QTest::newRow("too-large-for-slot") << archive() << container(101, 'N');
        QTest::newRow("wrong-package-id") << archive() << container(80, 'N', 99);
        auto broken = container(80, 'N');
        broken[broken.size() - 1] ^= 1;
        QTest::newRow("bad-checksum") << archive() << broken;
        broken = archive();
        set<qint32>(broken, 1222, 1);
        QTest::newRow("inconsistent-lengths") << broken << container(80, 'N');
        broken = archive();
        set<qint32>(broken, 1142 + 20 + 16, 0);
        set<qint32>(broken, 1222 + 192, 0);
        QTest::newRow("zero-length-entry") << broken << container(80, 'N');
        broken = archive();
        set<qint64>(broken, 1142 + 20, UiOffset);
        QTest::newRow("overlapping-data") << broken << container(80, 'N');
        broken = archive();
        set<quint64>(broken, 1142 + 20 + 8, UiId);
        QTest::newRow("duplicate-id") << broken << container(80, 'N');
    }
    void rejectsMalformedInputs() {
        QFETCH(QByteArray, source);
        QFETCH(QByteArray, replacement);
        QTemporaryDir game, workspace;
        const auto sourcePath = game.filePath("DataPC_patch_01.forge"), input = workspace.filePath("ui.data"),
                   output = workspace.filePath("refused");
        QVERIFY(save(sourcePath, source));
        QVERIFY(save(input, replacement));
        QCOMPARE(run(game.path(), input, output).code, 1);
        QCOMPARE(read(sourcePath), source);
        QVERIFY(!QFileInfo::exists(output));
    }
};
QTEST_GUILESS_MAIN(ResearchCopyTests)
#include "research_copy_tests.moc"
