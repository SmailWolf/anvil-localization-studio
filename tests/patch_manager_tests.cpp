#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
namespace {
const QByteArray Original = "anvil-test-original-v2", Patched = "anvil-test-modified-v2-with-font";
template <class T> void put(QByteArray &b, T value) {
    char raw[sizeof(T)];
    qToLittleEndian(value, raw);
    b.append(raw, sizeof(T));
}
QByteArray delta() {
    QByteArray b = "ANVILGRB2";
    put<quint32>(b, 2);
    put<quint64>(b, Original.size());
    put<quint64>(b, Patched.size());
    b += QCryptographicHash::hash(Original, QCryptographicHash::Sha256).toHex();
    b += QCryptographicHash::hash(Patched, QCryptographicHash::Sha256).toHex();
    put<quint32>(b, 1);
    put<quint64>(b, 0);
    put<quint32>(b, Patched.size());
    return b + Patched;
}
bool save(const QString &path, const QByteArray &b) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
struct Fixture {
    QTemporaryDir game, work;
    QString target = game.filePath("DataPC_patch_01.forge"), patch = work.filePath("candidate.forge"),
            backupDir = work.filePath("backups");
    bool init() { return game.isValid() && work.isValid() && save(target, Original) && save(patch, Patched); }
    QString backup() const {
        const auto h = QCryptographicHash::hash(Original, QCryptographicHash::Sha256).toHex().left(16);
        return backupDir + "/DataPC_patch_01." + QString::fromLatin1(h) + ".original";
    }
    int run(const QString &mode, bool running = false, const QString &backupOverride = {}) {
        QProcess p;
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("ANVIL_TEST_GAME_RUNNING");
        if (running)
            env.insert("ANVIL_TEST_GAME_RUNNING", "1");
        p.setProcessEnvironment(env);
        p.start(qEnvironmentVariable("ANVIL_PATCH_MANAGER_TEST_BIN"),
                {"--mode", mode, "--game-dir", game.path(), "--patch-file", patch,
                 "--backup-dir", backupOverride.isEmpty() ? backupDir : backupOverride});
        if (!p.waitForFinished(10000)) {
            p.kill();
            p.waitForFinished(5000);
            return -99;
        }
        return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -98;
    }
};
} // namespace
class PatchManagerTests : public QObject {
    Q_OBJECT
  private slots:
    void checkingDoesNotWrite() {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.run("check"), 0);
        QCOMPARE(read(f.target), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
        QCOMPARE(QDir(f.game.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
        QCOMPARE(f.run(""), 1);
        QCOMPARE(read(f.target), Original);
    }
    void installAndRestore() {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.run("install"), 0);
        QCOMPARE(read(f.target), Patched);
        QCOMPARE(read(f.backup()), Original);
        QCOMPARE(f.run("install"), 0);
        QCOMPARE(read(f.backup()), Original);
        QCOMPARE(f.run("check"), 0);
        QVERIFY(QFile::remove(f.patch));
        QCOMPARE(f.run("restore"), 0);
        QCOMPARE(read(f.target), Original);
        QCOMPARE(f.run("restore"), 0);
        QCOMPARE(read(f.backup()), Original);
        QVERIFY(!QFileInfo::exists(f.game.filePath(".anvil-menu-patch.lock")));
    }
    void deltaInstallAndRestore() {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(save(f.patch, delta()));
        QCOMPARE(f.run("check"), 0);
        QCOMPARE(read(f.target), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
        QCOMPARE(f.run("install"), 0);
        QCOMPARE(read(f.target), Patched);
        QCOMPARE(read(f.backup()), Original);
        QCOMPARE(f.run("check"), 0);
        QCOMPARE(f.run("install"), 0);
        QCOMPARE(f.run("restore"), 0);
        QCOMPARE(read(f.target), Original);
    }
    void brokenDeltaCannotReplaceGame_data() {
        QTest::addColumn<QByteArray>("bytes");
        auto b = delta();
        b[b.size() - 1] ^= 1;
        QTest::newRow("incorrect-assembled-hash") << b;
        QTest::newRow("truncated") << delta().left(40);
        QTest::newRow("trailing") << (delta() + "junk");
        b = delta();
        qToLittleEndian<quint64>(9999999, b.data() + 161);
        QTest::newRow("outside-archive") << b;
        b = delta();
        qToLittleEndian<quint32>(99999, b.data() + 157);
        QTest::newRow("too-many-edits") << b;
        b = delta();
        b[29] = 'z';
        QTest::newRow("wrong-source-hash") << b;
    }
    void brokenDeltaCannotReplaceGame() {
        QFETCH(QByteArray, bytes);
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(save(f.patch, bytes));
        QCOMPARE(f.run("check"), 1);
        QCOMPARE(read(f.target), Original);
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), Original);
        if (QFileInfo::exists(f.backup()))
            QCOMPARE(read(f.backup()), Original);
        QVERIFY(!QFileInfo::exists(f.game.filePath(".anvil-menu-patch.lock")));
        QCOMPARE(QDir(f.game.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }
    void wrongVersionOrPatchIsRefused() {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(save(f.target, "other-game-version-or-mod"));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), QByteArray("other-game-version-or-mod"));
        QVERIFY(!QFileInfo::exists(f.backupDir));
        QVERIFY(save(f.target, Original));
        QVERIFY(save(f.patch, "broken-patch"));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
    }
    void backupCannotBeInsideGame() {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.run("install", false, f.game.filePath("backup")), 1);
        QCOMPARE(read(f.target), Original);
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(f.game.path(), f.work.filePath("link")));
        QCOMPARE(f.run("install", false, f.work.filePath("link/nested")), 1);
        QCOMPARE(read(f.target), Original);
#endif
    }
    void runningGameIsRefused() {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.run("install", true), 1);
        QCOMPARE(read(f.target), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
        QCOMPARE(f.run("install"), 0);
        QCOMPARE(f.run("restore", true), 1);
        QCOMPARE(read(f.target), Patched);
        QCOMPARE(f.run("restore"), 0);
    }
    void damagedBackupNeverReplacesGame() {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(QDir().mkpath(f.backupDir));
        QVERIFY(save(f.backup(), "corrupted-backup"));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), Original);
        QCOMPARE(read(f.backup()), QByteArray("corrupted-backup"));
        QVERIFY(save(f.backup(), Original));
        QCOMPARE(f.run("install"), 0);
        QVERIFY(save(f.backup(), "corrupted-backup"));
        QCOMPARE(f.run("restore"), 1);
        QCOMPARE(read(f.target), Patched);
    }
    void otherModsAndBusyLockAreRefused() {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(save(f.game.filePath("DataPC_patch_02.forge"), "other-mod"));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), Original);
        QVERIFY(QFile::remove(f.game.filePath("DataPC_patch_02.forge")));
        QLockFile lock(f.game.filePath(".anvil-menu-patch.lock"));
        QVERIFY(lock.tryLock(0));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(f.target), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
    }
    void userEditsAreNotOverwrittenByRestore() {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.run("install"), 0);
        QVERIFY(save(f.target, "new-user-mod"));
        QCOMPARE(f.run("restore"), 1);
        QCOMPARE(read(f.target), QByteArray("new-user-mod"));
        QCOMPARE(read(f.backup()), Original);
    }
    void symbolicLinkTargetsAreRefused() {
#ifdef Q_OS_UNIX
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(QFile::remove(f.target));
        const auto originalElsewhere = f.work.filePath("original");
        QVERIFY(save(originalElsewhere, Original));
        QVERIFY(QFile::link(originalElsewhere, f.target));
        QCOMPARE(f.run("install"), 1);
        QCOMPARE(read(originalElsewhere), Original);
        QVERIFY(!QFileInfo::exists(f.backupDir));
#else
        QSKIP("Symbolic-link fixture requires Unix");
#endif
    }
};
QTEST_GUILESS_MAIN(PatchManagerTests)
#include "patch_manager_tests.moc"
