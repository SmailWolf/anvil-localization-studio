#include "ui/mainwindow.h"
#include <QPlainTextEdit>
#include <QLabel>
#include <QAction>
#include <QDialog>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QSet>
#include <QMessageBox>
#include <QDialogButtonBox>
#include "package_fixture.h"
#include <anvil/storage.h>
#include <QStandardPaths>
#include <QtTest>
#include <QTranslator>
#include "ui/modeldownloaddialog.h"
#include "ui/fontdialog.h"
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QDir::tempPath()+"/anvil-ui-tests-settings");
    }
    void englishInterface() {
        QTranslator translator;QVERIFY(translator.load(QCoreApplication::applicationDirPath()+"/anvil_en.qm"));
        qApp->installTranslator(&translator);
        {
            MainWindow window;
            bool project=false,download=false;
            for(auto *a:window.findChildren<QAction*>()){project|=a->text()=="Project";download|=a->text()=="Download model…";}
            QVERIFY(project);QVERIFY(download);
            FontDialog fonts(&window);QCOMPARE(fonts.windowTitle(),QString("Fonts — Ukrainian + Belarusian"));
            ModelDownloadDialog dialog(&window);QCOMPARE(dialog.windowTitle(),QString("Download translation model"));
            QCOMPARE(window.findChild<QLineEdit*>("search")->placeholderText(),QString("Search all fields…"));
        }
        qApp->removeTranslator(&translator);
    }
    void fontDialogBothLanguages() {
        FontDialog dialog;dialog.show();
        auto *language=dialog.findChild<QComboBox*>("fontLanguage");QVERIFY(language);
        QCOMPARE(language->currentData().toString(),QString("both"));
        auto *report=dialog.findChild<QPlainTextEdit*>("fontReport");QVERIFY(report);
        QVERIFY(report->toPlainText().contains("Відсутні літери: немає"));
        auto *save=dialog.findChild<QPushButton*>("exportFontKit");QVERIFY(save&&save->isEnabled());
        language->setCurrentIndex(language->findData("be"));QVERIFY(save->isEnabled());
        language->setCurrentIndex(language->findData("uk"));QVERIFY(save->isEnabled());
        const auto screenshot=qEnvironmentVariable("ANVIL_FONT_SCREENSHOT");
        if(!screenshot.isEmpty()){language->setCurrentIndex(0);QVERIFY(dialog.grab().save(screenshot));}
    }
    void projectLanguageSelectionAndExistingTranslationGuard() {
        MainWindow window;anvil::Document doc;doc.entries={{"a","Settings","","","todo"}};
        window.loadExample(doc);window.show();
        QAction *action=nullptr;for(auto *a:window.findChildren<QAction*>())if(a->text()=="Мови проєкту…")action=a;
        QVERIFY(action);bool selected=false;
        QTimer::singleShot(0,&window,[&]{
            auto *combo=window.findChild<QComboBox*>("projectTargetLanguage");if(!combo)return;
            combo->setCurrentIndex(combo->findData("be"));selected=true;
            qobject_cast<QDialog*>(combo->parentWidget())->accept();
        });action->trigger();QVERIFY(selected);
        // Verify that the chosen project language reaches the translation preview.
        bool belarusianPreview=false;
        QTimer::singleShot(0,&window,[&]{
            if(auto *d=window.findChild<QDialog*>()){
                for(auto *l:d->findChildren<QLabel*>())belarusianPreview|=l->text().contains("English → Беларуская");
                d->reject();
            }
        });
        for(auto *a:window.findChildren<QAction*>())if(a->text()=="Перекласти всі неперекладені…")a->trigger();
        QVERIFY(belarusianPreview);
        // A translated document cannot be silently relabeled as the other language.
        doc.entries[0].translation="Налаштування";doc.entries[0].status="reviewed";window.loadExample(doc);
        bool refused=false;QTimer poll;poll.setInterval(10);
        connect(&poll,&QTimer::timeout,&window,[&]{
            if(auto *message=window.findChild<QMessageBox*>()){refused=message->text().contains("вже є переклади");message->accept();poll.stop();return;}
            if(auto *combo=window.findChild<QComboBox*>("projectTargetLanguage")){combo->setCurrentIndex(combo->findData("be"));qobject_cast<QDialog*>(combo->parentWidget())->accept();}
        });poll.start();action->trigger();poll.stop();QVERIFY(refused);
    }
    void packageFromGui() {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        const auto root=temp.filePath("game"),destination=temp.filePath("export");
        anvil::test::game(root,true);
        MainWindow window;window.loadExample(anvil::test::project(true));window.show();
        bool submitted=false,complete=false;QString failure;
        QTimer poll,timeout;poll.setInterval(20);timeout.setSingleShot(true);
        connect(&poll,&QTimer::timeout,&window,[&]{
            if(!submitted){
                if(auto *dialog=window.findChild<QDialog*>()){
                    auto *game=dialog->findChild<QLineEdit*>("packGameDirectory");
                    auto *output=dialog->findChild<QLineEdit*>("packOutputDirectory");
                    auto *buttons=dialog->findChild<QDialogButtonBox*>();
                    if(game&&output&&buttons){submitted=true;game->setText(root);output->setText(destination);buttons->button(QDialogButtonBox::Ok)->click();}
                }
            }else if(auto *message=window.findChild<QMessageBox*>()){
                complete=message->windowTitle()=="Пакет створено";
                if(!complete)failure=message->text();
                poll.stop();message->accept();
            }
        });
        connect(&timeout,&QTimer::timeout,&window,[&]{poll.stop();if(auto *d=window.findChild<QDialog*>())d->reject();});
        poll.start();timeout.start(30000);
        QAction *action=nullptr;for(auto *a:window.findChildren<QAction*>())if(a->text()=="Запакувати переклад…")action=a;
        QVERIFY(action);action->trigger();QVERIFY(submitted);QVERIFY2(complete,qPrintable(failure));
        QVERIFY(QFileInfo::exists(destination+"/archives/DataPC_patch_01.forge"));
        QCOMPARE(anvil::readFile(root+"/DataPC_patch_01.forge"),anvil::test::forge(true));
    }
    void fullProjectQueuePreview() {
        const auto path=qEnvironmentVariable("ANVIL_TRANSLATION_PROJECT");
        if(path.isEmpty()) QSKIP("Optional scale test needs an owned project; never starts inference");
        const auto doc=anvil::fromJson(anvil::readFile(path));
        QVERIFY(doc.entries.size()>500);
        MainWindow window;window.loadExample(doc);window.show();
        QSet<QString> expected;
        for(const auto &entry:doc.entries)if(entry.translation.isEmpty())expected.insert(entry.source);
        int previewRows=-1;
        QTimer::singleShot(0,&window,[&]{
            if(auto *dialog=window.findChild<QDialog*>()) {
                if(auto *table=dialog->findChild<QTableWidget*>())previewRows=table->rowCount();
                dialog->reject();
            }
        });
        QAction *action=nullptr;
        for(auto *candidate:window.findChildren<QAction*>())
            if(candidate->text()=="Перекласти всі неперекладені…")action=candidate;
        QVERIFY(action);
        QElapsedTimer timer;timer.start();action->trigger();
        qInfo()<<"Queue preview:"<<doc.entries.size()<<"records,"<<previewRows<<"unique,"<<timer.elapsed()<<"ms";
        QCOMPARE(previewRows,expected.size());
    }
    void realModelPreviewApply() {
        const auto model=qEnvironmentVariable("ANVIL_TRANSLATION_MODEL"), server=qEnvironmentVariable("ANVIL_TRANSLATION_SERVER");
        if(model.isEmpty()||server.isEmpty()) QSKIP("Optional test requires an owned local model");
        MainWindow window;
        anvil::Document doc;doc.sourceLanguage="en-US";
        doc.entries={{"settings","Settings","","","todo"},
                     {"existing","Main menu","Мій переклад","","reviewed"},
                     {"name","Hello {name}!","","","todo"}};
        for(int i=0;i<600;++i) doc.entries.append({"duplicate-"+QString::number(i),"Settings","","","todo"});
        doc.entries.append({"service","Name: Test_technical","","","todo"});
        window.loadExample(doc);window.show();
        auto *table=window.findChild<QTableView*>("entries");QVERIFY(table);
        // No manual selection: exercise the entire-project queue with more than 500 rows.
        bool started=false,applied=false;
        QTimer poll,deadline;poll.setInterval(100);deadline.setSingleShot(true);
        connect(&poll,&QTimer::timeout,&window,[&]{
            auto *dialog=window.findChild<QDialog*>();if(!dialog)return;
            if(!started){
                auto *modelField=dialog->findChild<QLineEdit*>("translationModel");
                auto *serverField=dialog->findChild<QLineEdit*>("translationServer");
                auto *cuda=dialog->findChild<QCheckBox*>("translationCuda");
                if(!modelField||!serverField||!cuda)return;
                cuda->setChecked(false);modelField->setText(model);serverField->setText(server);
                started=true;
                for(auto *button:dialog->findChildren<QPushButton*>())
                    if(button->text()=="Перекласти")button->click();
                return;
            }
            for(auto *button:dialog->findChildren<QPushButton*>())
                if(button->text()=="Застосувати позначені"&&button->isEnabled()){
                    const auto screenshot=qEnvironmentVariable("ANVIL_TRANSLATION_SCREENSHOT");
                    if(!screenshot.isEmpty())dialog->grab().save(screenshot);
                    applied=true;poll.stop();button->click();break;
                }
        });
        connect(&deadline,&QTimer::timeout,&window,[&]{
            poll.stop();if(auto *dialog=window.findChild<QDialog*>())dialog->reject();
        });
        poll.start();deadline.start(240000);
        QAction *action=nullptr;
        for(auto *candidate:window.findChildren<QAction*>())
            if(candidate->text()=="Перекласти всі неперекладені…")action=candidate;
        QVERIFY(action);action->trigger();
        QVERIFY(started);QVERIFY(applied);
        QCOMPARE(table->model()->index(0,2).data().toString(),QString("Налаштування"));
        QCOMPARE(table->model()->index(0,4).data().toString(),QString("translated"));
        QCOMPARE(table->model()->index(1,2).data().toString(),QString("Мій переклад"));
        QCOMPARE(table->model()->index(1,4).data().toString(),QString("reviewed"));
        QVERIFY(table->model()->index(2,2).data().toString().contains("{name}"));
        QCOMPARE(table->model()->index(602,2).data().toString(),QString("Налаштування"));
        QVERIFY(table->model()->index(603,2).data().toString().isEmpty());
        QCOMPARE(table->model()->rowCount(),604);
    }
    void multilineEditor() {
        MainWindow window;
        anvil::Document d;
        d.entries = {{"id", "Hello", "Вітаю", "", "reviewed"}};
        window.loadExample(d);
        window.show();
        auto *table = window.findChild<QTableView *>("entries");
        QVERIFY(table);
        QVERIFY(table->columnWidth(2) >= 250);
        const auto index = table->model()->index(0, 2);
        table->setCurrentIndex(index);
        table->edit(index);
        auto *editor = table->findChild<QPlainTextEdit *>();
        QVERIFY(editor);
        editor->setPlainText("Перший\nДругий");
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(table->model()->index(0, 2).data().toString(), QString("Перший\nДругий"));
        QCOMPARE(table->model()->index(0, 4).data().toString(), QString("translated"));
    }
    void editingAndFiltering() {
        MainWindow window;
        anvil::Document d;
        d.entries = {{"greeting", "Hello", "Вітаю", "demo", "reviewed"},
                     {"empty", "Bye", "", "demo", "todo"}};
        d.glossary = {{"Ghost", "Привид", "demo"}};
        window.loadExample(d);
        auto *table = window.findChild<QTableView *>("entries");
        QVERIFY(table);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(table->model());
        QVERIFY(proxy);
        auto *model = proxy->sourceModel();
        QCOMPARE(model->rowCount(), 2);
        model->setData(model->index(0, 2), "Новий переклад");
        QCOMPARE(model->index(0, 4).data().toString(), QString("translated"));
        QVERIFY(window.windowTitle().startsWith('*'));
        model->setData(model->index(0, 2), "");
        QCOMPARE(model->index(0, 4).data().toString(), QString("todo"));
        model->setData(model->index(0, 2), "Вітаю");
        auto *search = window.findChild<QLineEdit *>("search");
        QVERIFY(search);
        search->setText("вітаю");
        QCOMPARE(proxy->rowCount(), 1);
        search->setText("[literal]");
        QCOMPARE(proxy->rowCount(), 0);
        search->clear();
        auto *status = window.findChild<QComboBox *>("statusFilter");
        QVERIFY(status);
        status->setCurrentIndex(status->findData("todo"));
        QCOMPARE(proxy->rowCount(), 1);
        QCOMPARE(proxy->index(0, 0).data().toString(), QString("empty"));
        auto *glossary = window.findChild<QTableView *>("glossary");
        QVERIFY(glossary);
        QCOMPARE(glossary->model()->rowCount(), 1);
        glossary->model()->setData(glossary->model()->index(0, 1), "Привид оновлений");
        QCOMPARE(glossary->model()->index(0, 1).data().toString(), QString("Привид оновлений"));
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
