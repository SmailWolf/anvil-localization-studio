#include <anvil/fonts.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstring>
#include <stdexcept>
namespace {
QByteArray donor() {
    QByteArray b(82,'\0');b.replace(0,8,QByteArray("GFOF\3\3\0\0",8));
    qToLittleEndian<quint32>(1,b.data()+20);qToLittleEndian<quint32>(0x10ffff,b.data()+44);
    const float metrics[]={12,-1,-2,2,1,2,1};
    for(int i=0;i<7;++i){quint32 bits;std::memcpy(&bits,&metrics[i],4);qToLittleEndian(bits,b.data()+48+i*4);}
    qToLittleEndian<quint32>(80,b.data()+76);b[80]=char(128);b[81]=char(255);return b;
}
void touch(const QString &path){QFile f(path);if(!f.open(QIODevice::WriteOnly))qFatal("Cannot create test file");f.write("test");}
}
class FontTests final:public QObject {
    Q_OBJECT
  private slots:
    void bothAlphabetsNoFontFallback() {
        auto all=anvil::fonts::requiredCharacters();
        for(const auto c:QString::fromUtf8("ҐґЄєІіЇїЁёЎў’ʼ"))QVERIFY(all.contains(c));
        QVERIFY(!anvil::fonts::requiredCharacters("be").contains(QChar(0x0490)));
        QVERIFY(!anvil::fonts::requiredCharacters("uk").contains(QChar(0x040e)));
        auto report=anvil::fonts::inspect(anvil::fonts::bundledFont());
        QVERIFY(report["coverageVerified"].toBool());QVERIFY(!report["engineRenderingTested"].toBool());
        QVERIFY_EXCEPTION_THROWN(anvil::fonts::requiredCharacters("ru"),std::runtime_error);
    }
    void nativeFontCompilationAndValidation() {
        const auto original=donor();
        auto before=anvil::fonts::inspectGfof(original);
        QVERIFY(before["roundtripExact"].toBool());QVERIFY(!before["coverageVerified"].toBool());
        auto compiled=anvil::fonts::compileGfof(anvil::fonts::bundledFont(),original);
        const auto report=anvil::fonts::inspectGfof(compiled);
        QVERIFY(report["coverageVerified"].toBool());QVERIFY(report["roundtripExact"].toBool());
        QVERIFY(!report["engineRenderingTested"].toBool());
        QCOMPARE(compiled.first(20),original.first(20));QCOMPARE(compiled.mid(24,20),original.mid(24,20));
        // Unsupported donor glyph must retain all metrics and pixels, without font fallback.
        const auto count=qFromLittleEndian<quint32>(compiled.constData()+20);
        const auto record=compiled.mid(44+(count-1)*36,36);
        QCOMPARE(record.first(32),original.mid(44,32));QCOMPARE(compiled.last(2),original.last(2));
        for(int n=0;n<original.size();++n)QVERIFY_EXCEPTION_THROWN(anvil::fonts::inspectGfof(original.first(n)),std::runtime_error);
        auto bad=original;bad[4]=4;QVERIFY_EXCEPTION_THROWN(anvil::fonts::compileGfof(anvil::fonts::bundledFont(),bad),std::runtime_error);
        bad=original;qToLittleEndian<quint32>(0xd800,bad.data()+44);QVERIFY_EXCEPTION_THROWN(anvil::fonts::inspectGfof(bad),std::runtime_error);
        const auto output=qEnvironmentVariable("ANVIL_FONT_TEST_OUTPUT");
        if(!output.isEmpty()){QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write(compiled);}
    }
    void exportAndGameDirectoryGuards() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto output=temp.filePath("fonts");
        anvil::fonts::exportKit(anvil::fonts::bundledFont(),output);
        QVERIFY(QFileInfo::exists(output+"/font.ttf"));QVERIFY(QFileInfo::exists(output+"/OFL.txt"));
        QVERIFY_EXCEPTION_THROWN(anvil::fonts::exportKit(anvil::fonts::bundledFont(),output),std::runtime_error);
        const auto game=temp.filePath("game");QVERIFY(QDir().mkpath(game));touch(game+"/GRB.exe");
        QVERIFY_EXCEPTION_THROWN(anvil::fonts::exportKit(anvil::fonts::bundledFont(),game+"/fonts"),std::runtime_error);
        QVERIFY(!QFileInfo::exists(game+"/fonts"));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(game,temp.filePath("game-link")));
        QVERIFY_EXCEPTION_THROWN(anvil::fonts::exportKit(anvil::fonts::bundledFont(),temp.filePath("game-link/nested/fonts")),std::runtime_error);
#endif
        const auto raw=temp.filePath("donor.gfof");QFile f(raw);QVERIFY(f.open(QIODevice::WriteOnly));f.write(donor());f.close();
        anvil::fonts::exportKit(anvil::fonts::bundledFont(),temp.filePath("native"),"both",raw);
        QVERIFY(QFileInfo::exists(temp.filePath("native/experimental-font.gfof")));
    }
};
QTEST_MAIN(FontTests)
#include "font_tests.moc"
