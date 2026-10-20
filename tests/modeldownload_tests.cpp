#include <anvil/modeldownload.h>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
class DownloadTests : public QObject {
    Q_OBJECT
private slots:
    void download_data() {
        QTest::addColumn<QByteArray>("body");QTest::addColumn<int>("code");QTest::addColumn<bool>("valid");
        QTest::newRow("valid")<<QByteArray("GGUFsynthetic test model")<<200<<true;
        QTest::newRow("wrong-hash")<<QByteArray("GGUFmodified test model!")<<200<<false;
        QTest::newRow("html")<<QByteArray("<html>permission denied")<<200<<false;
        QTest::newRow("http-error")<<QByteArray("GGUFsynthetic test model")<<403<<false;
        QTest::newRow("truncated")<<QByteArray("GGUFshort")<<200<<false;
        QTest::newRow("too-large")<<QByteArray("GGUFsynthetic test model-extra")<<200<<false;
    }
    void download() {
        QFETCH(QByteArray,body);QFETCH(int,code);QFETCH(bool,valid);
        QTemporaryDir temp;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,&server,[&]{auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[=]{socket->readAll();socket->write("HTTP/1.1 "+QByteArray::number(code)+" Test\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
        const QByteArray expected="GGUFsynthetic test model";
        anvil::ModelSource source{QUrl(QString("http://127.0.0.1:%1/model").arg(server.serverPort())),"test.gguf",expected.size(),QCryptographicHash::hash(expected,QCryptographicHash::Sha256).toHex()};
        anvil::ModelDownload download;QSignalSpy success(&download,&anvil::ModelDownload::succeeded),failure(&download,&anvil::ModelDownload::failed),progress(&download,&anvil::ModelDownload::progress);
        QVERIFY(download.start(source,temp.path()));QTRY_VERIFY_WITH_TIMEOUT(!download.isRunning(),5000);
        QCOMPARE(success.size(),valid?1:0);QCOMPARE(failure.size(),valid?0:1);
        QFile output(temp.filePath("test.gguf"));QCOMPARE(output.exists(),valid);
        if(valid){QVERIFY(output.open(QIODevice::ReadOnly));QCOMPARE(output.readAll(),expected);QVERIFY(progress.size()>1);}
    }
    void cancellation() {
        QTemporaryDir temp;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        anvil::ModelDownload job;QSignalSpy failure(&job,&anvil::ModelDownload::failed);
        auto source=anvil::translateGemmaSource();source.url=QUrl(QString("http://127.0.0.1:%1/model").arg(server.serverPort()));
        QVERIFY(job.start(source,temp.path()));job.cancel();QTRY_VERIFY(!job.isRunning());QCOMPARE(failure.size(),1);
        QVERIFY(QDir(temp.path()).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    }
    void existingAndGameGuard() {
        QTemporaryDir temp;auto source=anvil::translateGemmaSource();anvil::ModelDownload job;
        QFile existing(temp.filePath(source.filename));QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
        QVERIFY(!job.start(source,temp.path()));QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArray("keep"));
        QFile marker(temp.filePath("DataPC.forge"));QVERIFY(marker.open(QIODevice::WriteOnly));marker.close();
        QVERIFY(!job.start(source,temp.filePath("models/new")));QVERIFY(!QFileInfo::exists(temp.filePath("models")));
        source.url=QUrl("http://example.com/model.gguf");QVERIFY(!job.start(source,temp.filePath("elsewhere")));
    }
};
QTEST_GUILESS_MAIN(DownloadTests)
#include "modeldownload_tests.moc"
