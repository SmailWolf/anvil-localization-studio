// Explicit user-run install/restore utility for the experimental menu patch v2.
// No mode is selected by default. Checking never writes to the game or backups.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

#ifndef ANVIL_ORIGINAL_SHA256
#define ANVIL_ORIGINAL_SHA256 "754f960e5e7f89e136a7400ae494cbfc596f06162ba1efc1f3b029f9ecc98d75"
#endif
#ifndef ANVIL_PATCH_SHA256
#define ANVIL_PATCH_SHA256 "c8716b6d24cc4f4a0b33fcfcc5860136da9b70bdb8f886cc411015589eb1a9de"
#endif
#ifndef ANVIL_TARGET_ARCHIVE
#define ANVIL_TARGET_ARCHIVE "DataPC_patch_01.forge"
#endif
#ifndef ANVIL_PATCH_FAMILY
#define ANVIL_PATCH_FAMILY "DataPC_patch_*.forge"
#endif
#ifndef ANVIL_ALLOWED_PEER
#define ANVIL_ALLOWED_PEER "DataPC_patch_01.forge"
#endif
namespace {
const QString OriginalHash = QStringLiteral(ANVIL_ORIGINAL_SHA256);
const QString PatchHash = QStringLiteral(ANVIL_PATCH_SHA256);
const QString TargetArchive = QStringLiteral(ANVIL_TARGET_ARCHIVE);
#ifdef ANVIL_FONT_PATCH
const QString PatchLabel = QStringLiteral("шрифтовий патч Anvil UA");
#else
const QString PatchLabel = QStringLiteral("патч меню v2");
#endif
constexpr qint64 Chunk = 2 * 1024 * 1024;
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toUtf8().constData()); }
QString sha(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        fail(QStringLiteral("Очікується звичайний файл без символічного посилання: ") + path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не вдалося прочитати: ") + path);
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const auto b = f.read(Chunk);
        if (b.isEmpty())
            fail(QStringLiteral("Помилка читання: ") + path);
        h.addData(b);
    }
    return QString::fromLatin1(h.result().toHex());
}
void requireHash(const QString &path, const QString &expected) {
    if (sha(path) != expected)
        fail(QStringLiteral("Контрольна сума не збігається. Файл не змінено: ") + path);
}
QString resolved(const QString &path) {
    QFileInfo f(QFileInfo(path).absoluteFilePath());
    QString tail;
    while (!f.exists()) {
        tail = "/" + f.fileName() + tail;
        const auto parent = f.absolutePath();
        if (parent == f.absoluteFilePath())
            fail(QStringLiteral("Не вдалося перевірити шлях: ") + path);
        f.setFile(parent);
    }
    return QDir::cleanPath(f.canonicalFilePath() + tail);
}
bool within(const QString &path, const QString &root) {
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    return path.compare(root, cs) == 0 || path.startsWith(root + "/", cs);
}
[[maybe_unused]] bool gameName(QString name) {
    name.replace('\\', '/');
    name = name.section('/', -1).trimmed();
    return name.compare("GRB.exe", Qt::CaseInsensitive) == 0 ||
           name.compare("GRB_vulkan.exe", Qt::CaseInsensitive) == 0 ||
           name.compare("GRB_BE.exe", Qt::CaseInsensitive) == 0;
}
struct Edit { qint64 offset; QByteArray bytes; };
bool gameRunning();
struct Patch {
    bool delta = false;
    qint64 archiveSize = 0;
    qint64 resultSize = 0;
    QVector<Edit> edits;
};
Patch loadPatch(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        fail(QStringLiteral("Очікується звичайний файл патча без посилання."));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не вдалося відкрити пакет патча."));
    if (f.peek(9) != "ANVILGRB2") {
        f.close();
        requireHash(path, PatchHash);
        return {};
    }
    if (f.size() > 4 * 1024 * 1024)
        fail(QStringLiteral("Пакет змін перевищує підтримувану межу."));
    const auto bytes = f.readAll();
    qsizetype at = 9;
    auto take = [&](qsizetype n) {
        if (n < 0 || n > bytes.size() - at)
            fail(QStringLiteral("Обрізаний пакет змін."));
        auto result = bytes.mid(at, n);
        at += n;
        return result;
    };
    auto u32 = [&]() { return qFromLittleEndian<quint32>(take(4).constData()); };
    auto u64 = [&]() { return qFromLittleEndian<quint64>(take(8).constData()); };
    const auto version = u32();
    if (version != 1 && version != 2)
        fail(QStringLiteral("Невідома версія пакета змін."));
    Patch patch;
    patch.delta = true;
    const auto size = u64();
    if (!size || size > 2ULL * 1024 * 1024 * 1024)
        fail(QStringLiteral("Некоректний розмір архіву в пакеті змін."));
    patch.archiveSize = qint64(size);
    const auto resultSize = version == 2 ? u64() : size;
    if (!resultSize || resultSize > 2ULL * 1024 * 1024 * 1024)
        fail(QStringLiteral("Некоректний розмір результату в пакеті змін."));
    patch.resultSize = qint64(resultSize);
    if (take(64) != OriginalHash.toLatin1() || take(64) != PatchHash.toLatin1())
        fail(QStringLiteral("Пакет змін призначено для іншого патча або версії гри."));
    const auto count = u32();
    if (!count || count > 32)
        fail(QStringLiteral("Некоректна кількість ділянок у пакеті змін."));
    qint64 end = 0;
    for (quint32 i = 0; i < count; ++i) {
        const auto offset = u64();
        const auto length = u32();
        if (!length || offset > resultSize || length > resultSize - offset || offset < quint64(end))
            fail(QStringLiteral("Некоректні або перетнуті ділянки пакета змін."));
        patch.edits.append({qint64(offset), take(length)});
        end = qint64(offset + length);
    }
    if (at != bytes.size())
        fail(QStringLiteral("Зайві байти в пакеті змін."));
    return patch;
}
// A delta cannot select an output name or checksum: both full-file SHA-256 values
// are pinned in this executable. No transformed bytes reach the game on mismatch.
void transform(const QString &source, const Patch &patch, QIODevice *output) {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || input.size() != patch.archiveSize)
        fail(QStringLiteral("Розмір архіву не відповідає пакету змін."));
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (qint64 offset = 0; offset < patch.resultSize; offset += Chunk) {
        const auto expectedSize = std::min(Chunk, patch.resultSize - offset);
        QByteArray b(expectedSize, '\0');
        const auto fromSource = std::max(qint64(0), std::min(expectedSize, input.size() - offset));
        const auto original = input.read(fromSource);
        if (original.size() != fromSource)
            fail(QStringLiteral("Помилка читання оригіналу під час складання патча."));
        if (fromSource)
            std::memcpy(b.data(), original.constData(), size_t(fromSource));
        for (const auto &edit : patch.edits) {
            const auto first = std::max(offset, edit.offset);
            const auto last = std::min(offset + b.size(), edit.offset + edit.bytes.size());
            if (first < last)
                std::memcpy(b.data() + (first - offset), edit.bytes.constData() + (first - edit.offset),
                            size_t(last - first));
        }
        h.addData(b);
        if (output && output->write(b) != b.size())
            fail(QStringLiteral("Помилка запису тимчасового архіву. Оригінал збережено."));
    }
    if (QString::fromLatin1(h.result().toHex()) != PatchHash)
        fail(QStringLiteral("Зібраний патч не відповідає очікуваній SHA-256. Оригінал не замінено."));
}
void atomicDelta(const QString &target, const Patch &patch) {
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не вдалося підготувати тимчасовий архів. Оригінал збережено."));
    transform(target, patch, &output);
    requireHash(target, OriginalHash);
    if (gameRunning())
        fail(QStringLiteral("Гра запущена. Оригінал не замінено."));
    if (!output.commit())
        fail(QStringLiteral("Не вдалося атомарно встановити патч. Оригінал збережено."));
    requireHash(target, PatchHash);
}
bool gameRunning() {
#ifdef ANVIL_TEST_PROCESS_PROBE
    return qEnvironmentVariableIsSet("ANVIL_TEST_GAME_RUNNING");
#elif defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        fail(QStringLiteral("Не вдалося перевірити запущені процеси."));
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (gameName(QString::fromWCharArray(entry.szExeFile))) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return running;
#elif defined(Q_OS_LINUX)
    const QDir proc("/proc");
    if (!proc.exists())
        fail(QStringLiteral("Не вдалося перевірити запущені процеси."));
    for (const auto &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        pid.toULongLong(&number);
        if (!number)
            continue;
        QFile comm(proc.filePath(pid + "/comm"));
        if (comm.open(QIODevice::ReadOnly) && gameName(QString::fromUtf8(comm.read(256))))
            return true;
        QFile cmd(proc.filePath(pid + "/cmdline"));
        if (cmd.open(QIODevice::ReadOnly)) {
            const auto first = cmd.read(4096).split('\0').value(0);
            if (gameName(QString::fromUtf8(first)))
                return true;
        }
    }
    return false;
#else
    fail(QStringLiteral("Перевірка процесів підтримує Linux і Windows."));
#endif
}
// QSaveFile keeps the previous destination intact until a verified copy is ready.
// There is no direct-write fallback, so a failed copy cannot truncate the game.
void atomicCopy(const QString &source, const QString &destination, const QString &expected,
                const QString &currentDestination = {}, bool watchGame = false) {
    QFile input(source);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не вдалося підготувати копію: ") + destination);
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        const auto bytes = input.read(Chunk);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            fail(QStringLiteral("Копіювання перервано. Попередній файл збережено."));
        h.addData(bytes);
    }
    if (QString::fromLatin1(h.result().toHex()) != expected)
        fail(QStringLiteral("Вхідний файл змінився під час копіювання."));
    if (!currentDestination.isEmpty())
        requireHash(destination, currentDestination);
    if (watchGame && gameRunning())
        fail(QStringLiteral("Гра запущена. Закрийте її та повторіть операцію."));
    if (!output.commit())
        fail(QStringLiteral("Не вдалося завершити атомарну заміну. Попередній файл збережено."));
    requireHash(destination, expected);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Helpers are called with redirected pipes by the GUI and CI on Windows.
    qputenv("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", "1");
#ifdef ANVIL_FONT_PATCH
    app.setApplicationName("anvil-font-patch");
#else
    app.setApplicationName("anvil-menu-patch");
#endif
    QCommandLineParser args;
    args.addHelpOption();
    args.setApplicationDescription("Experimental Breakpoint patch: explicit check/install/restore. "
                                   "In-game compatibility and UMAC acceptance are unverified.");
    args.addOptions({{"mode", "check, install or restore (required)", "mode"},
                     {"game-dir", "Breakpoint directory", "path"},
                     {"patch-file", "Menu v2 delta or full prepared archive", "path"},
                     {"backup-dir", "Backup directory outside the game", "path"}});
    args.process(app);
    try {
        const auto mode = args.value("mode");
        if (mode != "check" && mode != "install" && mode != "restore")
            fail(QStringLiteral("Вкажіть --mode check, install або restore."));
        const auto game = QFileInfo(args.value("game-dir")).canonicalFilePath();
        if (game.isEmpty() || !QFileInfo(game).isDir())
            fail(QStringLiteral("Вкажіть каталог гри через --game-dir."));
        if (args.value("backup-dir").isEmpty())
            fail(QStringLiteral("Вкажіть --backup-dir поза грою."));
        const auto backupDir = resolved(args.value("backup-dir"));
        if (within(backupDir, game))
            fail(QStringLiteral("Резервна копія повинна бути поза каталогом гри."));
        const auto target = game + "/" + TargetArchive;
        const auto backupStem = TargetArchive.endsWith(".forge") ? TargetArchive.chopped(6) : TargetArchive;
        const auto backup = backupDir + "/" + backupStem + "." + OriginalHash.left(16) + ".original";
        const auto currentHash = sha(target);
        if (currentHash != OriginalHash && currentHash != PatchHash)
            fail(QStringLiteral("Інша версія гри або сторонній мод. Жоден файл не змінено."));
        if (mode == "restore" && currentHash == OriginalHash) {
            QTextStream(stdout) << QStringLiteral("У грі вже оригінальний архів. Зміни не потрібні.\n");
            return 0;
        }
        Patch patch;
        if (mode != "restore") {
            const auto patchFile = QFileInfo(args.value("patch-file")).absoluteFilePath();
            if (within(resolved(patchFile), game))
                fail(QStringLiteral("Пакет патча повинен зберігатися поза грою."));
            patch = loadPatch(patchFile);
            const auto siblings = QDir(game).entryList({QStringLiteral(ANVIL_PATCH_FAMILY)}, QDir::Files);
            for (const auto &name : siblings)
                if (name != QStringLiteral(ANVIL_ALLOWED_PEER))
                    fail(QStringLiteral("Знайдено інший патч DataPC. Спочатку приберіть його з гри: ") + name);
#ifdef ANVIL_PEER_SHA256
            requireHash(game + "/" + QStringLiteral(ANVIL_ALLOWED_PEER), QStringLiteral(ANVIL_PEER_SHA256));
#endif
            if (mode == "check") {
                if (patch.delta) {
                    if (currentHash == OriginalHash)
                        transform(target, patch, nullptr);
                    else {
                        requireHash(backup, OriginalHash);
                        transform(backup, patch, nullptr);
                    }
                }
                QTextStream(stdout) << QStringLiteral("Контрольні суми відповідають пакету. Гру не змінено.\n")
                                    << QStringLiteral("Статус архіву: ")
                                    << (currentHash == OriginalHash ? QStringLiteral("original") : PatchLabel)
                                    << QStringLiteral(". Працездатність у грі ще не підтверджена.\n");
                return 0;
            }
        }
        if (gameRunning())
            fail(QStringLiteral("Гра запущена. Закрийте її перед встановленням або відкатом."));
        // Only explicit install/restore modes create a lock or backup directories.
        QLockFile lock(game + "/.anvil-menu-patch.lock");
        if (!lock.tryLock(0))
            fail(QStringLiteral("Інша операція з патчем уже виконується."));
        requireHash(target, currentHash);
        if (mode == "restore") {
            requireHash(backup, OriginalHash);
            atomicCopy(backup, target, OriginalHash, PatchHash, true);
            QTextStream(stdout) << QStringLiteral("Оригінальний архів відновлено. SHA-256 перевірено.\n");
            return 0;
        }
        if (currentHash == PatchHash) {
            requireHash(backup, OriginalHash);
            QTextStream(stdout) << PatchLabel << QStringLiteral(" уже встановлено, резервна копія справна.\n");
            return 0;
        }
        if (!QDir().mkpath(backupDir))
            fail(QStringLiteral("Не вдалося створити каталог резервування."));
        if (QFileInfo::exists(backup))
            requireHash(backup, OriginalHash);
        else
            atomicCopy(target, backup, OriginalHash);
        // A fresh hash check prevents replacing a game file modified during backup.
        requireHash(backup, OriginalHash);
        if (patch.delta)
            atomicDelta(target, patch);
        else
            atomicCopy(QFileInfo(args.value("patch-file")).absoluteFilePath(), target, PatchHash,
                       OriginalHash, true);
        QTextStream(stdout) << PatchLabel << QStringLiteral(" встановлено. Оригінал збережено поза грою:\n")
                            << backup << QStringLiteral("\nДля відкату запустіть restore.sh.\n");
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << QString::fromUtf8(e.what()) << "\n";
        return 1;
    }
}
