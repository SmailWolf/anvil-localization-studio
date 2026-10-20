#include <QCoreApplication>
#include <anvil/modeldownload.h>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
namespace anvil {
ModelSource translateGemmaSource() {
    return {QUrl("https://huggingface.co/mradermacher/translategemma-4b-it-GGUF/resolve/d37868f759d2f817a43d28dd80cf9f982ffa62fd/translategemma-4b-it.Q4_K_M.gguf"),
        "translategemma-4b-it.Q4_K_M.gguf",2489909760LL,
        "81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330"};
}
ModelDownload::ModelDownload(QObject *parent):QObject(parent) {}
ModelDownload::~ModelDownload() {
    if(reply_) { disconnect(reply_,nullptr,this,nullptr); reply_->abort(); }
}
bool ModelDownload::start(const ModelSource &source,const QString &directory) {
    if(isRunning()) return false;
    auto fail=[this](const QString &message){emit failed(message);return false;};
    // HTTP is restricted to the local fixture server; public downloads require TLS.
    const bool local=source.url.scheme()=="http" && (source.url.host()=="127.0.0.1" || source.url.host()=="::1");
    if((source.url.scheme()!="https"&&!local)||!source.url.userInfo().isEmpty()||source.bytes<4||
        source.sha256.size()!=64||source.filename!=QFileInfo(source.filename).fileName()||source.filename.isEmpty())
        return fail(QCoreApplication::translate("Anvil", "Некоректне джерело моделі."));
    // Inspect the nearest existing ancestor before creating any directory.
    auto existing=QFileInfo(directory);
    while(!existing.exists()&&existing.absoluteFilePath()!=existing.absolutePath())existing=QFileInfo(existing.absolutePath());
    auto ancestor=QDir(existing.canonicalFilePath());
    do {
        if(QFileInfo::exists(ancestor.filePath("DataPC.forge"))||QFileInfo::exists(ancestor.filePath("GRB.exe"))||QFileInfo::exists(ancestor.filePath("GRW.exe")))
            return fail(QCoreApplication::translate("Anvil", "Збережіть модель поза папкою гри."));
    } while(ancestor.cdUp());
    if(!QDir().mkpath(directory))return fail(QCoreApplication::translate("Anvil", "Не вдалося створити папку для моделі."));
    QDir folder(QFileInfo(directory).canonicalFilePath());
    path_=folder.filePath(source.filename);
    if(QFileInfo::exists(path_)||QFileInfo(path_).isSymLink()) return fail(QCoreApplication::translate("Anvil", "Файл уже існує. Оберіть іншу папку або наявну модель через «Обрати…»."));
    QStorageInfo storage(folder.absolutePath());
    if(storage.isValid()&&storage.bytesAvailable()>=0&&storage.bytesAvailable()<source.bytes+64*1024*1024)
        return fail(QCoreApplication::translate("Anvil", "Недостатньо вільного місця для моделі."));
    file_=std::make_unique<QSaveFile>(path_);file_->setDirectWriteFallback(false);
    if(!file_->open(QIODevice::WriteOnly)) { file_.reset();return fail(QCoreApplication::translate("Anvil", "Не вдалося відкрити файл для запису.")); }
    source_=source;received_=0;error_.clear();magic_.clear();hash_.reset();
    QNetworkRequest request(source.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setRawHeader("Accept-Encoding","identity");
    reply_=network_.get(request);reply_->setReadBufferSize(1024*1024);
    connect(reply_,&QIODevice::readyRead,this,&ModelDownload::read);
    connect(reply_,&QNetworkReply::finished,this,&ModelDownload::finish);
    emit progress(0,source.bytes);return true;
}
void ModelDownload::read() {
    if(!reply_||!file_||!error_.isEmpty()) return;
    const auto status=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(status>=300&&status<400) { reply_->readAll();return; }
    if(status!=200) {error_=QCoreApplication::translate("Anvil", "Сервер відмовив у завантаженні (HTTP %1).").arg(status);reply_->abort();return;}
    while(reply_->bytesAvailable()>0) {
        const auto data=reply_->read(1024*1024);
        if(data.isEmpty()) break;
        received_+=data.size();
        if(magic_.size()<4)magic_+=data.left(4-magic_.size());
        if(received_>source_.bytes||(magic_.size()==4&&magic_!="GGUF")) {
            error_=QCoreApplication::translate("Anvil", "Завантажений файл не відповідає моделі GGUF.");reply_->abort();return;
        }
        if(file_->write(data)!=data.size()) {error_=QCoreApplication::translate("Anvil", "Помилка запису моделі. Перевірте вільне місце.");reply_->abort();return;}
        hash_.addData(data);emit progress(received_,source_.bytes);
    }
}
void ModelDownload::finish() {
    read();
    auto *reply=reply_;
    if(error_.isEmpty()&&reply->error()!=QNetworkReply::NoError) error_=reply->errorString();
    if(error_.isEmpty()&&(received_!=source_.bytes||hash_.result().toHex()!=source_.sha256))
        error_=QCoreApplication::translate("Anvil", "Розмір або контрольна сума моделі не збігається. Спробуйте завантажити знову.");
    if(error_.isEmpty()&&(QFileInfo::exists(path_)||QFileInfo(path_).isSymLink()))error_=QCoreApplication::translate("Anvil", "Файл уже існує. Його не перезаписано.");
    if(error_.isEmpty()&&!file_->commit())error_=QCoreApplication::translate("Anvil", "Не вдалося зберегти перевірену модель.");
    if(!error_.isEmpty())file_->cancelWriting();
    file_.reset();reply_=nullptr;reply->deleteLater();
    if(error_.isEmpty())emit succeeded(path_);else emit failed(error_);
}
void ModelDownload::cancel() {
    if(reply_) {error_=QCoreApplication::translate("Anvil", "Завантаження скасовано. Незавершений файл видалено.");reply_->abort();}
}
}
