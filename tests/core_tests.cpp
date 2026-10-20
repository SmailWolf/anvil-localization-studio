#include <QDir>
#include <QFile>
#include <QPluginLoader>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <anvil/document.h>
#include <anvil/formatplugin.h>
#include <anvil/storage.h>
#include <stdexcept>
class CoreTests : public QObject {
    Q_OBJECT
    static anvil::Document sample() {
        anvil::Document d;
        d.game = "breakpoint";
        d.entries.append({"dialog.1", "Hello, \"Ghost\"\n{name}", "Вітаю, «Привиде»\n{name}",
                          "line\r\ncontext", "reviewed"});
        d.entries.append({"empty", "", "", "", "todo"});
        d.glossary.append({"Ghost", "Привид", "Тест"});
        return d;
    }
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void jsonRoundtrip() {
        const auto d = sample();
        QVERIFY(anvil::fromJson(anvil::toJson(d)) == d);
    }
    void csvRoundtrip() {
        const auto d = sample();
        QVERIFY(anvil::fromCsv(anvil::toCsv(d)) == d.entries);
    }
    void csvBomAndNoFinalNewline() {
        auto bytes = anvil::toCsv(sample());
        bytes.chop(2);
        QVERIFY(anvil::fromCsv(QByteArray::fromHex("efbbbf") + bytes) == sample().entries);
    }
    void csvMalformed_data() {
        QTest::addColumn<QByteArray>("bytes");
        const QByteArray h = "id,source,translation,context,status\n";
        QTest::newRow("unclosed") << h + "\"id,source";
        QTest::newRow("stray quote") << h + "i\"d,s,t,c,todo";
        QTest::newRow("after quote") << h + "\"id\"x,s,t,c,todo";
        QTest::newRow("width") << h + "id,s,t,c";
        QTest::newRow("duplicate") << h + "id,s,t,c,todo\nid,s,t,c,todo";
        QTest::newRow("unknown status") << h + "id,s,t,c,bad";
        QTest::newRow("invalid utf8") << h + QByteArray::fromHex("ff");
        QTest::newRow("wrong header") << QByteArray("source,id\n");
    }
    void csvMalformed() {
        QFETCH(QByteArray, bytes);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::fromCsv(bytes));
    }
    void invalidDocuments() {
        auto d = sample();
        d.entries.append(d.entries.front());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::toJson(d));
        d = sample();
        d.entries[0].translation = "  ";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::toJson(d));
        d = sample();
        d.glossary.append({" ghost ", "інше", ""});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::toJson(d));
        d = sample();
        d.game = "unverified";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::toJson(d));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::fromJson("{}"));
        auto bytes = anvil::toJson(sample());
        bytes.replace("\"schemaVersion\": 1", "\"schemaVersion\": 2");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::fromJson(bytes));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::fromJson("[]"));
    }
    void backupsPreservePreviousBytes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("project.als.json");
        const auto before = anvil::toJson(sample());
        anvil::saveWithBackup(path, before);
        anvil::saveWithBackup(path, "second");
        anvil::saveWithBackup(path, "third");
        QCOMPARE(anvil::readFile(path), QByteArray("third"));
        const QDir backups(dir.filePath(".anvil-backups"));
        const auto names = backups.entryList(QDir::Files);
        QCOMPARE(names.size(), 2);
        QList<QByteArray> contents;
        for (const auto &name : names)
            contents.append(anvil::readFile(backups.filePath(name)));
        QVERIFY(contents.contains(before));
        QVERIFY(contents.contains("second"));
    }
    void backupFailureDoesNotOverwrite() {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.als.json");
        anvil::saveWithBackup(path, "original");
        QFile obstruction(dir.filePath(".anvil-backups"));
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::saveWithBackup(path, "new"));
        QCOMPARE(anvil::readFile(path), QByteArray("original"));
    }
    void ioFailure() {
        QTemporaryDir dir;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::readFile(dir.filePath("missing")));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 anvil::saveWithBackup(dir.filePath("missing/project.json"), "x"));
    }
    void recoveryRoundtrip() {
        const QString id = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = anvil::writeRecovery(sample(), id, dir.path());
        QVERIFY(anvil::fromJson(anvil::readFile(path)) == sample());
        QVERIFY(QFile::remove(path));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, anvil::writeRecovery(sample(), "../bad", dir.path()));
    }
    void pluginLoad() {
        const auto path = qEnvironmentVariable("ANVIL_TEST_PLUGIN");
        if (path.isEmpty())
            QSKIP("Run the dedicated plugin test with ANVIL_TEST_PLUGIN set");
        QPluginLoader loader(path);
        QObject *object = loader.instance();
        QVERIFY2(object, qPrintable(loader.errorString()));
        auto *plugin = qobject_cast<anvil::FormatPlugin *>(object);
        QVERIFY(plugin);
        QCOMPARE(plugin->formatId(), QString("studio.synthetic-json.v1"));
        const QByteArray bytes = "ANVIL_STUDIO_SAMPLE\n" + anvil::toJson(sample());
        QVERIFY(plugin->recognizes(bytes));
        QVERIFY(plugin->decode(bytes) == sample());
        QVERIFY(!plugin->recognizes("random .forge"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, plugin->decode("bad"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
