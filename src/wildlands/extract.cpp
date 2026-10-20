#include <anvil/wildlands.h>
#include <anvil/storage.h>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <stdexcept>
namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Helpers are called with redirected pipes by the GUI and CI on Windows.
    qputenv("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", "1");
    app.setApplicationName("anvil-extract-wildlands");
    QCommandLineParser args;
    args.setApplicationDescription("Read-only extraction of English Wildlands localization. No game writes.");
    args.addHelpOption();
    args.addOptions({{{"g", "game-dir"}, "Wildlands directory with GRW.exe", "directory"},
                     {{"o", "output"}, "NEW .als.json project outside the game directory", "file"}});
    args.process(app);
    try {
        const auto root = QFileInfo(args.value("game-dir")).canonicalFilePath();
        require(!args.value("game-dir").isEmpty() && !root.isEmpty(), "Specify an existing --game-dir");
        require(!args.value("output").isEmpty(), "Specify --output");
        const QFileInfo destination(args.value("output"));
        require(destination.fileName().endsWith(".als.json", Qt::CaseInsensitive), "Output must end in .als.json");
        require(!destination.exists() && !destination.isSymLink(), "Output already exists or is a symlink");
        const auto parent = QFileInfo(destination.absolutePath()).canonicalFilePath();
        require(!parent.isEmpty(), "Output parent directory must exist");
#ifdef Q_OS_WIN
        const auto cs = Qt::CaseInsensitive;
#else
        const auto cs = Qt::CaseSensitive;
#endif
        require(parent.compare(root, cs) != 0 && !parent.startsWith(root + "/", cs),
                "Output must be outside the game directory");
        const auto document = anvil::wildlands::extractEnglish(root);
        const auto json = anvil::toJson(document);
        QFile output(parent + "/" + destination.fileName());
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create a NEW output project");
        if (output.write(json) != json.size() || !output.flush()) {
            output.remove();
            throw std::runtime_error("Cannot write the extracted project");
        }
        QTextStream(stdout) << "Exported " << document.entries.size() << " Wildlands strings. Game files unchanged.\n"
                            << output.fileName() << "\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "Extraction failed: " << QString::fromUtf8(e.what()) << "\n";
        return 1;
    }
}
