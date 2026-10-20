#include <anvil/translation.h>
#include <anvil/storage.h>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>
#include <stdexcept>
class TranslationTests final : public QObject {
    Q_OBJECT
  private slots:
    void unavailableCacheFailsBeforeStartingAnyServer() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto fakeModel=temp.filePath("fake.gguf");
        QFile file(fakeModel);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("GGUF"),4);file.close();
        anvil::TranslationJob job;
        job.settings.model=fakeModel;
        job.settings.server=temp.filePath("must-not-run");
        job.settings.cacheFile=fakeModel+"/not-a-directory/cache.jsonl";
        QSignalSpy failed(&job,&anvil::TranslationJob::failure),ended(&job,&QThread::finished);
        job.start();QVERIFY(ended.wait(10000));
        QCOMPARE(failed.size(),1);
        QVERIFY(failed[0][0].toString().contains("проміжний кеш"));
    }
    void duplicateQueueRetainsEveryIdentity() {
        const QVector<anvil::Entry> entries={{"a","Settings","","","todo"},
            {"b","Settings","Налаштування","","reviewed"},{"c","Settings ","","","todo"},
            {"d","Hello {name}!","","","todo"},{"e","Hello {name}!","","","todo"}};
        const auto groups=anvil::groupTranslationEntries(entries);
        QCOMPARE(groups.size(),3);
        QCOMPARE(groups[0].rows,QVector<int>({0,1}));
        QCOMPARE(groups[1].rows,QVector<int>({2}));
        QCOMPARE(groups[2].rows,QVector<int>({3,4}));
        QCOMPARE(entries[1].status,QString("reviewed"));
        QVERIFY(anvil::isServiceTranslationText("Name: (NME) Camp_Call [EXT]"));
        QVERIFY(anvil::isServiceTranslationText("Context: Answer to Camp_Call."));
        QVERIFY(anvil::isServiceTranslationText("Acting: Urgent. Under control."));
        QVERIFY(anvil::isServiceTranslationText("//WARNING\\ Camp_Call"));
        QVERIFY(!anvil::isServiceTranslationText("Roger, send reinforcements now."));
        QVERIFY(!anvil::isServiceTranslationText("[M] Open the map."));
    }
    void extractionPathsStayOutsideGame() {
        QTemporaryDir game, work;
        QVERIFY(game.isValid()); QVERIFY(work.isValid());
        QCOMPARE(anvil::requireOutsideGame(work.filePath("new/project.als.json"),game.path()),
                 work.filePath("new/project.als.json"));
        QVERIFY_EXCEPTION_THROWN(anvil::requireOutsideGame(game.filePath("new/project.als.json"),game.path()),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::requireOutsideGame(game.path(),game.path()),std::runtime_error);
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(game.path(),work.filePath("linked-game")));
        QVERIFY_EXCEPTION_THROWN(anvil::requireOutsideGame(work.filePath("linked-game/new/project.als.json"),game.path()),std::runtime_error);
#endif
    }
    void placeholdersAndGlossary() {
        const QString source="Hello {name}! Press [M]. <b>%1$s</b>\\n\nGhost";
        const auto plan=anvil::protectTranslation(source,{{"Ghost","Привид",""}});
        QVERIFY(!plan.masked.contains("{name}")); QVERIFY(!plan.masked.contains("Ghost"));
        auto translated=plan.masked; translated.replace("Hello", "Вітаю"); translated.replace("Press", "Натисніть");
        QCOMPARE(anvil::restoreTranslation(translated,plan),QString("Вітаю {name}! Натисніть [M]. <b>%1$s</b>\\n\nПривид"));
        auto missing=translated;missing.remove(plan.replacements[0].first);
        QVERIFY_EXCEPTION_THROWN(anvil::restoreTranslation(missing,plan),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::restoreTranslation(translated+plan.replacements[0].first,plan),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::restoreTranslation(translated+" [INVENTED]",plan),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::restoreTranslation(translated+" __ANVIL_9999__",plan),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::protectTranslation("__ANVIL_0000__"),std::runtime_error);
    }
    void updateKeepsOnlyUnchangedSourceAndIdentity() {
        anvil::Document old;old.sourceLanguage="en-US";
        old.entries={{"a","Settings","Налаштування","old","reviewed"},
                     {"b","Old text","Старий текст","old","translated"},
                     {"removed","Gone","Вилучено","old","translated"}};
        old.glossary={{"Ghost","Привид","term"}};
        auto fresh=old;fresh.glossary.clear();fresh.entries={{"a","Settings","","new","todo"},
            {"b","Changed text","","new","todo"},{"new","New text","","new","todo"}};
        const auto merged=anvil::mergeUpdated(old,fresh);
        QCOMPARE(merged.preserved,1);QCOMPARE(merged.added,1);QCOMPARE(merged.changed,1);QCOMPARE(merged.removed,1);
        QCOMPARE(merged.document.entries[0].translation,QString("Налаштування"));
        QCOMPARE(merged.document.entries[0].status,QString("reviewed"));
        QCOMPARE(merged.document.entries[0].context,QString("new"));
        QVERIFY(merged.document.entries[1].translation.isEmpty());QCOMPARE(merged.document.entries[1].status,QString("todo"));
        QCOMPARE(merged.document.glossary,old.glossary);QCOMPARE(old.entries[1].translation,QString("Старий текст"));
        fresh.game="breakpoint";QVERIFY_EXCEPTION_THROWN(anvil::mergeUpdated(old,fresh),std::runtime_error);
        fresh.game=old.game;fresh.sourceLanguage="ru";QVERIFY_EXCEPTION_THROWN(anvil::mergeUpdated(old,fresh),std::runtime_error);
    }
    void realModel_data() {
        QTest::addColumn<QString>("target");
        QTest::newRow("uk") << QString("uk");
        QTest::newRow("be") << QString("be");
    }
    void realModel() {
        QFETCH(QString,target);
        const auto model=qEnvironmentVariable("ANVIL_TRANSLATION_MODEL"), server=qEnvironmentVariable("ANVIL_TRANSLATION_SERVER");
        if(model.isEmpty()||server.isEmpty()) QSKIP("Optional test requires owned TranslateGemma GGUF and llama-server");
        anvil::TranslationJob job;
        job.settings={server,model,qEnvironmentVariable("ANVIL_TRANSLATION_CUDA")=="1",4};
        job.settings.targetLanguage=target;
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        job.settings.cacheFile=cache.filePath("progress.jsonl");
        job.entries={{"settings","Settings","","","todo"},{"menu","Main menu","","","todo"},
                     {"name","Hello {name}!","","","todo"},{"map","Press [M] to open the map.","","","todo"}};
        QSignalSpy result(&job,&anvil::TranslationJob::result), failed(&job,&anvil::TranslationJob::failure), ended(&job,&QThread::finished);
        connect(&job,&anvil::TranslationJob::progress,this,[](const QString &s){qInfo().noquote()<<s;});
        job.start();
        const bool completed=ended.wait(240000);
        if(!completed){job.requestInterruption();job.wait(10000);}
        QVERIFY(completed);
        for(const auto &item:failed)qWarning().noquote()<<item[0].toString();
        QCOMPARE(failed.size(),0);QCOMPARE(result.size(),4);
        QJsonArray outputs;
        for(const auto &item:result){
            qInfo().noquote()<<item[0].toInt()<<item[1].toString()<<item[2].toString();
            QVERIFY2(item[2].toString().isEmpty(),qPrintable(item[2].toString()));
            QVERIFY(QRegularExpression("[А-Яа-яІіЇїЄєҐґ]").match(item[1].toString()).hasMatch());
            outputs.append(QJsonObject{{"source",job.entries[item[0].toInt()].source},{"translation",item[1].toString()}});
        }
        if(target=="be") {
            QVERIFY2(result[0][1].toString()!=QString("Налаштування"),"Belarusian request returned Ukrainian Settings");
            QVERIFY2(result[1][1].toString()!=QString("Головне меню"),"Belarusian request returned Ukrainian Main menu");
        }
        QCOMPARE(result[2][1].toString().count("{name}"),1);QCOMPARE(result[3][1].toString().count("[M]"),1);
        QVERIFY(QFileInfo::exists(job.settings.cacheFile));
        const auto firstCacheBytes=QFileInfo(job.settings.cacheFile).size();
        result.clear();failed.clear();ended.clear();
        // An already stopped run can resume validated outputs without invoking generation again.
        job.start();
        const bool resumed=ended.wait(240000);
        if(!resumed){job.requestInterruption();job.wait(10000);}
        QVERIFY(resumed);
        QCOMPARE(failed.size(),0);QCOMPARE(result.size(),4);
        QCOMPARE(QFileInfo(job.settings.cacheFile).size(),firstCacheBytes+1);
        if(target=="uk") {
            // Same model, source and journal: changing target must force a new result.
            const auto previous=QFileInfo(job.settings.cacheFile).size();
            job.settings.targetLanguage="be";job.entries.resize(1);
            result.clear();failed.clear();ended.clear();job.start();
            const bool otherLanguage=ended.wait(240000);
            if(!otherLanguage){job.requestInterruption();job.wait(10000);}
            QVERIFY(otherLanguage);QCOMPARE(failed.size(),0);QCOMPARE(result.size(),1);
            QVERIFY(result[0][2].toString().isEmpty());
            QVERIFY(result[0][1].toString()!=outputs[0].toObject()["translation"].toString());
            QVERIFY(QFileInfo(job.settings.cacheFile).size()>previous+1);
        }
        const auto output=qEnvironmentVariable("ANVIL_TRANSLATION_REPORT");
        if(!output.isEmpty()){QFile file(output+"."+target+".json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(outputs).toJson());}
    }
};
QTEST_GUILESS_MAIN(TranslationTests)
#include "translation_tests.moc"
