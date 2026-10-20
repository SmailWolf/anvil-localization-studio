#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QUrl>
#include <memory>
namespace anvil {
struct ModelSource {
    QUrl url;
    QString filename;
    qint64 bytes;
    QByteArray sha256;
};
ModelSource translateGemmaSource();
class ModelDownload final : public QObject {
    Q_OBJECT
public:
    explicit ModelDownload(QObject *parent=nullptr);
    ~ModelDownload() override;
    bool start(const ModelSource &source, const QString &directory);
    void cancel();
    bool isRunning() const { return reply_ != nullptr; }
signals:
    void progress(qint64 received, qint64 total);
    void succeeded(const QString &path);
    void failed(const QString &message);
private:
    QNetworkAccessManager network_;
    QNetworkReply *reply_=nullptr;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    ModelSource source_;
    QString path_, error_;
    qint64 received_=0;
    QByteArray magic_;
    void read();
    void finish();
};
}
