#include <anvil/package.h>
#include <anvil/breakpoint.h>
#include <anvil/wildlands.h>
#include <anvil/storage.h>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <stdexcept>
#include "package_fixture.h"
using namespace anvil::test;
class PackageTests:public QObject {
    Q_OBJECT
  private slots:
    void roundTripBothGames_data(){
        QTest::addColumn<bool>("wild");QTest::addColumn<QString>("target");
        QTest::newRow("breakpoint-uk")<<false<<QString("uk");QTest::newRow("wildlands-uk")<<true<<QString("uk");
        QTest::newRow("breakpoint-be")<<false<<QString("be");QTest::newRow("wildlands-be")<<true<<QString("be");
    }
    void roundTripBothGames(){
        QFETCH(QString,target);QFETCH(bool,wild);QTemporaryDir temp;QVERIFY(temp.isValid());const auto root=temp.filePath("game");game(root,wild);
        anvil::packaging::Options options;options.gameDirectory=root;options.outputDirectory=temp.filePath("package");
        auto doc=project(wild);doc.targetLanguage=target;if(target=="be")doc.entries[0].translation="Налады";
        anvil::packaging::pack(doc,options);
        QCOMPARE(anvil::readFile(root+"/DataPC.forge"),forge(wild));QCOMPARE(anvil::readFile(root+"/DataPC_patch_01.forge"),forge(wild));
        QVERIFY(!QFile::exists(options.outputDirectory+"/INCOMPLETE.txt"));
        QFile copy(options.outputDirectory+"/archives/DataPC_patch_01.forge");QVERIFY(copy.open(QIODevice::ReadOnly));
        const auto entries=anvil::breakpoint::englishIndex(copy);QCOMPARE(entries.size(),1);QVERIFY(copy.seek(entries[0].offset));
        const auto cooked=copy.read(entries[0].size);QByteArray metadata,files;
        if(wild){const auto d=anvil::wildlands::decodeContainer(cooked);metadata=d.first;files=d.second;}
        else{const auto d=anvil::breakpoint::parseContainer(cooked);for(qsizetype i=0;i<d.blocks.size();++i)(i<d.metadataBlocks?metadata:files)+=d.blocks[i].bytes;}
        const auto native=anvil::breakpoint::unwrapResource(metadata,files,12345,"LocalizationPackage_English(US)");
        const auto after=wild?anvil::wildlands::decodeLocalization(native):anvil::breakpoint::decodeLocalization(native);
        QCOMPARE(after.entries[0].source,doc.entries[0].translation);QCOMPARE(after.entries[1].source,project(wild).entries[1].source);
        QVERIFY(copy.seek(1166));QCOMPARE(copy.read(8),QByteArray("OPAQUE!!"));
        const auto manifest=QJsonDocument::fromJson(anvil::readFile(options.outputDirectory+"/manifest.json")).object();
        QCOMPARE(manifest.value("targetLanguage").toString(),target);QCOMPARE(manifest.value("translatedStrings").toInt(),1);QCOMPARE(manifest.value("inGameTested").toBool(),false);
        auto source=forge(wild);source[source.size()-1]='x';write(root+"/DataPC_patch_01.forge",source);
        auto rebuild=options;rebuild.outputDirectory=temp.filePath("wrong-source");
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::assemble(options.outputDirectory,rebuild),std::runtime_error);
        QVERIFY(!QFile::exists(rebuild.outputDirectory+"/DataPC_patch_01.forge"));
        QVERIFY(QFile::exists(rebuild.outputDirectory+"/INCOMPLETE.txt"));
    }
    void staleProjectAndPlaceholders(){
        QTemporaryDir temp;const auto root=temp.filePath("game");game(root,true);
        anvil::packaging::Options options;options.gameDirectory=root;options.outputDirectory=temp.filePath("result");
        auto d=project(true);d.entries[0].source="Different version";
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::pack(d,options),std::runtime_error);QVERIFY(!QFile::exists(options.outputDirectory));
        d=project(true);d.entries[0].translation+=" {invented}";
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::pack(d,options),std::runtime_error);QVERIFY(!QFile::exists(options.outputDirectory));
        options.reviewedOnly=true;d=project(true);
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::pack(d,options),std::runtime_error);QVERIFY(!QFile::exists(options.outputDirectory));
    }
    void noWritesIntoGame(){
        QTemporaryDir temp;const auto root=temp.filePath("game");game(root,true);
        anvil::packaging::Options options;options.gameDirectory=root;options.outputDirectory=root+"/bad-package";
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::pack(project(true),options),std::runtime_error);QVERIFY(!QFile::exists(options.outputDirectory));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(root,temp.filePath("linked-game")));options.outputDirectory=temp.filePath("linked-game/package");
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::pack(project(true),options),std::runtime_error);
#endif
    }
    void tamperedDeltaRejected(){
        QTemporaryDir temp;const auto root=temp.filePath("game");game(root,true);
        anvil::packaging::Options options;options.gameDirectory=root;options.outputDirectory=temp.filePath("package");options.materialize=false;
        anvil::packaging::pack(project(true),options);const auto file=options.outputDirectory+"/DataPC_patch_01.forge.anvildelta";
        auto delta=anvil::readFile(file);delta[delta.size()-1]^=1;write(file,delta);
        auto output=options;output.outputDirectory=temp.filePath("assembly");
        QVERIFY_EXCEPTION_THROWN(anvil::packaging::assemble(options.outputDirectory,output),std::runtime_error);QVERIFY(!QFile::exists(output.outputDirectory));
    }
};
QTEST_GUILESS_MAIN(PackageTests)
#include "package_tests.moc"
