#include "ui/mainwindow.h"
#include <QApplication>
#include <QPixmap>
#include <QTimer>
#include <QSettings>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AnvilStudio");
    QCoreApplication::setApplicationName("AnvilLocalizationStudio");
    QCoreApplication::setApplicationVersion("0.7.0-beta.1");
    auto language=QSettings().value("interface/language","uk").toString();
    const int languageArg=app.arguments().indexOf("--ui-language");
    if(languageArg>=0 && languageArg+1<app.arguments().size())language=app.arguments()[languageArg+1];
    if(language!="uk"&&language!="en")return 2;
    QLocale::setDefault(QLocale(language));
    QTranslator translator,qtTranslator;
    if(language=="en") {
        if(!translator.load(":/i18n/anvil_en.qm"))return 3;
        app.installTranslator(&translator);
    } else if(qtTranslator.load("qt_uk",QLibraryInfo::path(QLibraryInfo::TranslationsPath)))app.installTranslator(&qtTranslator);
    MainWindow window;
    const auto arguments = app.arguments();
    const int game = arguments.indexOf("--game");
    if (game >= 0) {
        if (game + 1 >= arguments.size() ||
            (arguments[game+1] != "wildlands" && arguments[game+1] != "breakpoint")) return 2;
        anvil::Document blank;
        blank.game = arguments[game+1]; blank.sourceLanguage = "en-US";
        window.loadExample(blank);
    }
    const int open = arguments.indexOf("--open");
    if (open >= 0 && (open + 1 >= arguments.size() || !window.loadProjectPath(arguments[open + 1])))
        return 2;
    if (arguments.contains("--smoke-test")) {
        anvil::Document d;
        d.entries.append({"sample", "Hello, {name}!", "Вітаю, {name}!", "Synthetic sample", "translated"});
        d.glossary.append({"Ghost", "Привид", "Synthetic term"});
        if (open < 0)
            window.loadExample(d);
        QTimer::singleShot(250, &app, [&] {
            const auto screenshot = qEnvironmentVariable("ANVIL_SMOKE_SCREENSHOT");
            if (!screenshot.isEmpty() && !window.grab().save(screenshot)) {
                app.exit(2);
                return;
            }
            app.quit();
        });
    }
    window.show();
    return app.exec();
}
