#include <anvil/wildlands.h>
#include <anvil/formatplugin.h>
#include <QBuffer>
#include <QFile>
#include <QPluginLoader>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <lzo/lzo1x.h>
#include <stdexcept>
namespace {
template<class T> void append(QByteArray &b, T n) {
    char data[sizeof(T)]; qToLittleEndian(n, data); b.append(data, sizeof(T));
}
quint32 checksum(const QByteArray &bytes) {
    quint32 a=0,b=0;
    for (unsigned char c: bytes) { a=(a+c)%65521; b=(b+a)%65521; }
    return b<<16|a;
}
QByteArray descriptor(const QByteArray &raw, bool compressed) {
    QByteArray stored=raw;
    if (compressed) {
        stored.resize(raw.size()+raw.size()/16+64+3);
        lzo_uint length=stored.size();
        QVector<lzo_align_t> work((LZO1X_1_MEM_COMPRESS+sizeof(lzo_align_t)-1)/sizeof(lzo_align_t));
        if (lzo1x_1_compress(reinterpret_cast<const unsigned char*>(raw.constData()), raw.size(),
                            reinterpret_cast<unsigned char*>(stored.data()), &length, work.data())!=LZO_E_OK)
            throw std::runtime_error("Fixture compression failed");
        stored.resize(length);
    }
    QByteArray b;
    append<quint64>(b,0x1004fa9957fbaa33ULL); append<quint16>(b,1); b+=char(1);
    append<quint16>(b,32768); append<quint16>(b,32768); append<quint32>(b,1);
    append<quint16>(b,raw.size()); append<quint16>(b,stored.size());
    append(b,checksum(stored)); return b+stored;
}
QByteArray emptyPayload() {
    QByteArray b; append<quint64>(b,123);
    b+=QByteArray::fromHex("6f9c3c6e01020000000100000000000000000000f800000000b58983d200000000");
    return b;
}
}
class WildlandsTests: public QObject {
    Q_OBJECT
  private slots:
    void safeLzoAndRaw() {
        const QByteArray meta("synthetic-meta"), data(32768,'X');
        const auto decoded=anvil::wildlands::decodeContainer(descriptor(meta,false)+descriptor(data,true));
        QCOMPARE(decoded.first,meta); QCOMPARE(decoded.second,data);
    }
    void malformedContainers() {
        const auto good=descriptor("meta",false)+descriptor(QByteArray(1000,'A'),true);
        for (qsizetype n=0;n<good.size();++n)
            QVERIFY_EXCEPTION_THROWN(anvil::wildlands::decodeContainer(good.left(n)),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(anvil::wildlands::decodeContainer(good+'x'),std::runtime_error);
        for (const auto position: {8,10,11,13,15,19,21,23,27}) {
            auto broken=good; broken[position]=char(quint8(broken[position])^0x40);
            QVERIFY_EXCEPTION_THROWN(anvil::wildlands::decodeContainer(broken),std::runtime_error);
        }
        // Correct checksum with invalid LZO bytes must still fail the safe decoder.
        auto broken=descriptor("meta",false)+descriptor(QByteArray(1000,'A'),true);
        const qsizetype start=31;
        broken[start+27]=0;
        const auto stored=qFromLittleEndian<quint16>(broken.constData()+start+21);
        qToLittleEndian(checksum(broken.mid(start+27,stored)),broken.data()+start+23);
        QVERIFY_EXCEPTION_THROWN(anvil::wildlands::decodeContainer(broken),std::runtime_error);
    }
    void emptyEmanualIsGameSpecific() {
        const auto bytes=emptyPayload();
        const auto d=anvil::wildlands::decodeLocalization(bytes);
        QCOMPARE(d.game,QString("wildlands")); QVERIFY(d.entries.isEmpty());
        QVERIFY_EXCEPTION_THROWN(anvil::breakpoint::decodeLocalization(bytes),std::runtime_error);
        auto bad=bytes; bad[13]=3;
        QVERIFY_EXCEPTION_THROWN(anvil::wildlands::decodeLocalization(bad),std::runtime_error);
    }
    void plugin() {
        QPluginLoader loader(qEnvironmentVariable("ANVIL_GRW_PLUGIN"));
        auto *plugin=qobject_cast<anvil::FormatPlugin*>(loader.instance());
        QVERIFY2(plugin,qPrintable(loader.errorString()));
        QCOMPARE(plugin->formatId(),QString("grw.localizationpayload.v1"));
        QVERIFY(plugin->recognizes(emptyPayload()));
        QCOMPARE(plugin->decode(emptyPayload()).game,QString("wildlands"));
    }
    void outputGuard() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QFile exe(temp.filePath("GRW.exe")); QVERIFY(exe.open(QIODevice::WriteOnly));exe.write("fixture");exe.close();
        const auto path=temp.filePath("bad.als.json");
        QProcess process;
        process.start(qEnvironmentVariable("ANVIL_GRW_EXTRACTOR"),{"--game-dir",temp.path(),"--output",path});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(),1);
        QVERIFY(process.readAllStandardError().contains("outside the game directory"));
        QVERIFY(!QFile::exists(path));
#ifndef Q_OS_WIN
        QTemporaryDir outside; QVERIFY(outside.isValid());
        QVERIFY(QFile::link(temp.path(),outside.filePath("linked-game")));
        process.start(qEnvironmentVariable("ANVIL_GRW_EXTRACTOR"),
                      {"--game-dir",temp.path(),"--output",outside.filePath("linked-game/bad.als.json")});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(),1);
        QVERIFY(process.readAllStandardError().contains("outside the game directory"));
        QVERIFY(!QFile::exists(path));
#endif
    }
};
QTEST_GUILESS_MAIN(WildlandsTests)
#include "wildlands_tests.moc"
