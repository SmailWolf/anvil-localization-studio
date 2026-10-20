#include <QCoreApplication>
#include "modeldownloaddialog.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSettings>
#include <QDesktopServices>
ModelDownloadDialog::ModelDownloadDialog(QWidget *parent):QDialog(parent) {
    setWindowTitle(QCoreApplication::translate("Anvil", "Завантажити модель перекладу"));resize(640,330);
    auto *layout=new QVBoxLayout(this);
    auto *notice=new QLabel(QCoreApplication::translate("Anvil", "TranslateGemma 4B · Q4_K_M · 2,49 ГБ\nЗавантажується з Hugging Face. Після завантаження переклад працює локально, без інтернету. Файл перевіряється за SHA-256."),this);
    notice->setWordWrap(true);layout->addWidget(notice);
    auto *license=new QPushButton(QCoreApplication::translate("Anvil", "Умови використання Gemma та джерело моделі"),this);
    connect(license,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://huggingface.co/google/translategemma-4b-it"));});layout->addWidget(license);
    layout->addWidget(new QLabel(QCoreApplication::translate("Anvil", "Папка для моделі (потрібно щонайменше 2,6 ГБ):"),this));
    auto *row=new QHBoxLayout;
    auto *directory=new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/models",this);
    directory->setObjectName("modelDownloadDirectory");row->addWidget(directory);
    auto *browse=new QPushButton(QCoreApplication::translate("Anvil", "Обрати…"),this);row->addWidget(browse);layout->addLayout(row);
    connect(browse,&QPushButton::clicked,this,[=,this]{auto path=QFileDialog::getExistingDirectory(this,QCoreApplication::translate("Anvil", "Папка для моделі"),directory->text());if(!path.isEmpty())directory->setText(path);});
    auto *progress=new QProgressBar(this);progress->setRange(0,1000);progress->setValue(0);layout->addWidget(progress);
    auto *status=new QLabel(QCoreApplication::translate("Anvil", "Модель ще не завантажено."),this);status->setWordWrap(true);layout->addWidget(status);
    auto *buttons=new QHBoxLayout;
    auto *start=new QPushButton(QCoreApplication::translate("Anvil", "Завантажити"),this);start->setObjectName("downloadModelStart");buttons->addWidget(start);
    auto *stop=new QPushButton(QCoreApplication::translate("Anvil", "Скасувати завантаження"),this);stop->setEnabled(false);buttons->addWidget(stop);
    auto *close=new QPushButton(QCoreApplication::translate("Anvil", "Закрити"),this);buttons->addWidget(close);layout->addLayout(buttons);
    auto state=[=](bool running){directory->setEnabled(!running);browse->setEnabled(!running);start->setEnabled(!running);stop->setEnabled(running);};
    connect(start,&QPushButton::clicked,this,[=,this]{status->setText(QCoreApplication::translate("Anvil", "З’єднання з сервером…"));state(true);if(!download_.start(anvil::translateGemmaSource(),directory->text().trimmed()))state(false);});
    connect(stop,&QPushButton::clicked,&download_,&anvil::ModelDownload::cancel);
    connect(close,&QPushButton::clicked,this,&ModelDownloadDialog::reject);
    connect(&download_,&anvil::ModelDownload::progress,this,[=](qint64 received,qint64 total){progress->setValue(int(received*1000/total));status->setText(QCoreApplication::translate("Anvil", "Завантажено %1 / %2 МБ").arg(received/1000000).arg(total/1000000));});
    connect(&download_,&anvil::ModelDownload::failed,this,[=](const QString &message){state(false);status->setText(message);});
    connect(&download_,&anvil::ModelDownload::succeeded,this,[=,this](const QString &path){path_=path;QSettings().setValue("translation/model",path);state(false);start->setEnabled(false);status->setText(QCoreApplication::translate("Anvil", "Модель завантажена та перевірена. Можна перекладати."));});
}
void ModelDownloadDialog::reject(){if(download_.isRunning()){download_.cancel();return;}QDialog::reject();}
