#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
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
void outsideGame(const QString &path, const QString &root) {
    // Resolve the nearest existing ancestor as well as lexical '..' and symlinks.
    QFileInfo info(QFileInfo(path).absoluteFilePath());
    QString suffix;
    while (!info.exists()) {
        suffix = "/" + info.fileName() + suffix;
        const auto parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            fail("Cannot resolve destination");
        info.setFile(parent);
    }
    const QString resolved = QDir::cleanPath(info.canonicalFilePath() + suffix);
#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif
    if (resolved.compare(root, cs) == 0 || resolved.startsWith(root + "/", cs))
        fail("Output and Wine prefix must be outside the game directory");
}
void put32(QByteArray &bytes, quint32 n) {
    char b[4];
    qToLittleEndian(n, b);
    bytes.append(b, 4);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("anvil-extract-breakpoint");
    app.setApplicationVersion("0.2.0");
    QCommandLineParser args;
    args.setApplicationDescription("Read-only English localization extraction for the tested Breakpoint v27 "
                                   "layout. No game files are written.");
    args.addHelpOption();
    args.addVersionOption();
    args.addOptions(
        {{{"g", "game-dir"}, "Breakpoint installation directory", "directory"},
         {{"o", "output"}, "New Studio project outside the game directory (*.als.json)", "file"},
         {"codec-dll", "Explicit oo2core_7_win64.dll; default: game directory", "file"},
         {"helper", "Decoder helper executable; default: beside this executable", "file"},
         {"wine-prefix", "Linux only: dedicated Wine prefix outside the game directory", "directory"}});
    args.process(app);
    try {
        const QString root = QFileInfo(args.value("game-dir")).canonicalFilePath();
        if (args.value("game-dir").isEmpty() || root.isEmpty() || !QFileInfo(root).isDir())
            fail("Specify an existing --game-dir");
        const auto output = QFileInfo(args.value("output")).absoluteFilePath();
        if (args.value("output").isEmpty() || !output.endsWith(".als.json", Qt::CaseInsensitive))
            fail("Specify --output with .als.json suffix");
        outsideGame(output, root);
        if (QFileInfo::exists(output))
            fail("Output already exists; choose a new project filename");
        if (!QFileInfo(QFileInfo(output).absolutePath()).isDir())
            fail("Output parent directory does not exist");
        const QString dll = args.isSet("codec-dll") ? QFileInfo(args.value("codec-dll")).absoluteFilePath()
                                                    : root + "/oo2core_7_win64.dll";
        const QString helper = args.isSet("helper") ? QFileInfo(args.value("helper")).absoluteFilePath()
                                                    : app.applicationDirPath() + "/anvil-oodle-helper.exe";
        if (!QFileInfo(dll).isFile() || !QFileInfo(helper).isFile())
            fail("Decoder DLL or helper missing; see docs/BREAKPOINT.md");
#ifndef Q_OS_WIN
        if (!args.isSet("wine-prefix"))
            fail("On Linux specify a dedicated --wine-prefix outside the game directory");
        const auto prefix = QFileInfo(args.value("wine-prefix")).absoluteFilePath();
        outsideGame(prefix, root);
        const auto wine = QStandardPaths::findExecutable("wine");
        if (wine.isEmpty())
            fail("Wine is required on Linux for the owned game's decoder DLL");
#endif
        struct Selected {
            QString archive;
            anvil::breakpoint::ArchiveEntry entry;
            QByteArray bytes;
            anvil::breakpoint::Container plan;
        };
        QMap<quint64, Selected> selected;
        quint64 storedTotal = 0;
        // Explicit precedence for this export: the patch replaces matching package IDs.
        for (const auto &name : {QString("DataPC.forge"), QString("DataPC_patch_01.forge")}) {
            QFile input(root + "/" + name);
            if (!input.open(QIODevice::ReadOnly))
                fail(input.errorString());
            const QFileInfo before(input);
            const auto entries = anvil::breakpoint::englishIndex(input);
            for (const auto &entry : entries) {
                if (entry.size > 16 * 1024 * 1024 || storedTotal + entry.size > 64 * 1024 * 1024)
                    fail("Localization samples exceed extraction limits");
                if (!input.seek(entry.offset))
                    fail(input.errorString());
                const auto bytes = input.read(entry.size);
                if (bytes.size() != entry.size)
                    fail("Short archive entry read");
                storedTotal += entry.size;
                selected.insert(entry.id, {name, entry, bytes, anvil::breakpoint::parseContainer(bytes)});
            }
            const QFileInfo after(input.fileName());
            if (before.size() != after.size() || before.lastModified() != after.lastModified())
                fail("Game archive changed during reading; finish Steam downloads first");
        }
        if (selected.isEmpty())
            fail("No English(US) LocalizationPackage entries found");
        QTemporaryDir temp;
        if (!temp.isValid())
            fail("Cannot create temporary decoder directory");
        outsideGame(temp.path(), root);
        const auto dllCopy = temp.filePath("oo2core_7_win64.dll");
        if (!QFile::copy(dll, dllCopy))
            fail("Cannot copy codec DLL to the temporary directory");
        QByteArray jobs;
        quint32 count = 0;
        for (const auto &sample : selected)
            count += sample.plan.blocks.size();
        put32(jobs, count);
        for (const auto &sample : selected)
            for (const auto &b : sample.plan.blocks) {
                put32(jobs, b.rawSize);
                put32(jobs, b.bytes.size());
                jobs += b.bytes;
            }
        const auto jobsPath = temp.filePath("jobs.bin"), decodedPath = temp.filePath("decoded.bin");
        anvil::saveWithBackup(jobsPath, jobs);
        QProcess codec;
#ifdef Q_OS_WIN
        codec.setProgram(helper);
        codec.setArguments({QDir::toNativeSeparators(dllCopy), QDir::toNativeSeparators(jobsPath),
                            QDir::toNativeSeparators(decodedPath)});
#else
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix);
        env.insert("WINEDEBUG", "-all");
        env.insert("WINEDLLOVERRIDES", "mscoree,mshtml=");
        // Keep Wine shell integration files in the dedicated prefix, not user menus.
        env.insert("XDG_DATA_HOME", prefix + "/xdg-data");
        env.insert("XDG_CONFIG_HOME", prefix + "/xdg-config");
        codec.setProcessEnvironment(env);
        codec.setProgram(wine);
        codec.setArguments({helper, "Z:" + dllCopy, "Z:" + jobsPath, "Z:" + decodedPath});
#endif
        codec.start();
        if (!codec.waitForStarted(10000))
            fail("Cannot start decoder helper: " + codec.errorString());
        if (!codec.waitForFinished(120000)) {
            codec.kill();
            codec.waitForFinished(5000);
            fail("Decoder helper timed out");
        }
        if (codec.exitStatus() != QProcess::NormalExit || codec.exitCode() != 0)
            fail("Decoder helper failed: " + QString::fromUtf8(codec.readAllStandardError()).left(3000));
        if (QFileInfo(decodedPath).size() > 64 * 1024 * 1024)
            fail("Decoder output exceeds 64 MiB");
        const auto decoded = anvil::readFile(decodedPath);
        qsizetype at = 0;
        auto read32 = [&]() {
            if (decoded.size() - at < 4)
                fail("Truncated helper output");
            const auto n = qFromLittleEndian<quint32>(decoded.constData() + at);
            at += 4;
            return n;
        };
        if (read32() != count)
            fail("Helper output block count mismatch");
        anvil::Document project;
        project.game = "breakpoint";
        project.sourceLanguage = "en-US";
        QTextStream log(stdout);
        qsizetype empty = 0;
        for (const auto &sample : selected) {
            QByteArray metadata, files;
            for (qsizetype i = 0; i < sample.plan.blocks.size(); ++i) {
                const auto n = read32();
                if (n != sample.plan.blocks[i].rawSize || n > decoded.size() - at)
                    fail("Helper output size mismatch");
                (i < sample.plan.metadataBlocks ? metadata : files).append(decoded.constData() + at, n);
                at += n;
            }
            const auto payload =
                anvil::breakpoint::unwrapResource(metadata, files, sample.entry.id, sample.entry.name);
            auto doc = anvil::breakpoint::decodeLocalization(payload);
            if (doc.entries.isEmpty())
                ++empty;
            for (auto &e : doc.entries)
                e.context = sample.archive + " | " + sample.entry.name + " | " + e.context;
            log << sample.archive << " / " << sample.entry.name << ": " << doc.entries.size() << " strings\n";
            project.entries += doc.entries;
        }
        if (at != decoded.size())
            fail("Trailing helper output");
        const auto json = anvil::toJson(project);
        // Exclusive creation: this tool never replaces an existing project or archive.
        QFile destination(output);
        if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            fail(destination.errorString());
        if (destination.write(json) != json.size() || !destination.flush()) {
            destination.remove();
            fail("Cannot write extracted project");
        }
        log << "Exported " << project.entries.size() << " strings from " << selected.size()
            << " effective packages (" << empty << " empty).\n"
            << output << "\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "Extraction failed: " << QString::fromUtf8(e.what()) << "\n";
        return 1;
    }
}
