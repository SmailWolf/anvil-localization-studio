#include <anvil/package.h>
#include <anvil/storage.h>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTextStream>
#include <stdexcept>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    // Helpers are called with redirected pipes by the GUI and CI on Windows.
    qputenv("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", "1");app.setApplicationName("anvil-pack-translation");
    QCommandLineParser args;args.addHelpOption();
    args.setApplicationDescription("Build experimental translation packages and archive COPIES outside the game. Never installs.");
    args.addOptions({{"mode","pack or assemble","mode"},{"project","Saved .als.json translation project","file"},
        {"game-dir","Original game installation (read-only)","directory"},{"output-dir","NEW directory outside the game","directory"},
        {"package-dir","Existing package for assemble mode","directory"},{"reviewed-only","Include only reviewed translations"},
        {"delta-only","Create the small package, without full archive copies"},{"helper","Oodle helper path","file"},
        {"codec-dll","Owned Breakpoint codec DLL path","file"},{"wine-prefix","Dedicated Wine prefix outside game (Breakpoint Linux)","directory"}});
    args.process(app);
    try{
        if(args.value("game-dir").isEmpty()||args.value("output-dir").isEmpty())throw std::runtime_error("Specify --game-dir and --output-dir");
        anvil::packaging::Options options;
        options.gameDirectory=QFileInfo(args.value("game-dir")).absoluteFilePath();options.outputDirectory=QFileInfo(args.value("output-dir")).absoluteFilePath();
        options.helper=args.isSet("helper")?args.value("helper"):app.applicationDirPath()+"/anvil-oodle-helper.exe";
        options.codecDll=args.isSet("codec-dll")?args.value("codec-dll"):options.gameDirectory+"/oo2core_7_win64.dll";
        options.winePrefix=args.value("wine-prefix");options.reviewedOnly=args.isSet("reviewed-only");options.materialize=!args.isSet("delta-only");
        options.progress=[](const QString &s){QTextStream(stdout)<<s<<"\n"<<Qt::flush;};
        if(args.value("mode")=="pack"){
            const auto project=args.value("project");if(project.isEmpty()||QFileInfo(project).size()>128*1024*1024)throw std::runtime_error("Select a project no larger than 128 MiB");
            anvil::packaging::pack(anvil::fromJson(anvil::readFile(project)),options);
        }else if(args.value("mode")=="assemble")anvil::packaging::assemble(args.value("package-dir"),options);
        else throw std::runtime_error("Specify --mode pack or --mode assemble");
        return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"Packaging failed: "<<QString::fromUtf8(e.what())<<"\n";return 1;}
}
