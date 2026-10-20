#include <QCoreApplication>
#include "translationdialog.h"
#include "modeldownloaddialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
namespace {
QString enginePath(bool cuda) {
    auto path = QCoreApplication::applicationDirPath() + "/../engine/" + (cuda ? "cuda/" : "cpu/") + "llama-server";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return QDir::cleanPath(path);
}
}
TranslationDialog::TranslationDialog(const QVector<anvil::Entry> &entries, const QVector<anvil::Term> &glossary, QWidget *parent)
    : QDialog(parent), entries_(entries), glossary_(glossary) {
    setWindowTitle(QCoreApplication::translate("Anvil", "Локальний переклад — TranslateGemma 4B"));
    resize(1050, 700);
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(QCoreApplication::translate("Anvil", "English → Українська. Тексти обробляються на цьому комп’ютері. "
                             "Після перекладу перевірте результат і застосуйте позначені рядки."), this);
    notice->setWordWrap(true); layout->addWidget(notice);
    auto *form = new QFormLayout;
    QSettings settings;
    QString defaultModel;
    QFile preset(QCoreApplication::applicationDirPath() + "/../model-path.txt");
    if (preset.open(QIODevice::ReadOnly)) defaultModel = QString::fromUtf8(preset.read(8192)).trimmed();
    model_ = new QLineEdit(settings.value("translation/model", defaultModel).toString(), this);
    model_->setObjectName("translationModel");
    auto picker = [&](QLineEdit *field, const QString &title, const QString &filter) {
        auto *row = new QWidget(this); auto *box = new QHBoxLayout(row); box->setContentsMargins(0,0,0,0);
        auto *browse = new QPushButton(QCoreApplication::translate("Anvil", "Обрати…"), row); box->addWidget(field); box->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [=,this] {
            if (job_.isRunning()) return;
            const auto name = QFileDialog::getOpenFileName(this, title, field->text(), filter);
            if (!name.isEmpty()) field->setText(name);
        });
        return row;
    };
    auto *modelRow=picker(model_, "TranslateGemma 4B Q4_K_M", "GGUF (*.gguf)");
    auto *download=new QPushButton(QCoreApplication::translate("Anvil", "Завантажити модель…"),modelRow);
    modelRow->layout()->addWidget(download);
    connect(download,&QPushButton::clicked,this,[this]{
        if(job_.isRunning())return;
        ModelDownloadDialog dialog(this);dialog.exec();
        if(!dialog.modelPath().isEmpty())model_->setText(dialog.modelPath());
    });
    form->addRow(QCoreApplication::translate("Anvil", "Модель GGUF"),modelRow);
    cuda_ = new QCheckBox(QCoreApplication::translate("Anvil", "Використовувати NVIDIA CUDA (для RTX 4060)"), this);
    cuda_->setChecked(settings.value("translation/cuda", QFileInfo::exists("/dev/nvidia0")).toBool());
    server_ = new QLineEdit(settings.value("translation/server", enginePath(cuda_->isChecked())).toString(), this);
    server_->setObjectName("translationServer");
    cuda_->setObjectName("translationCuda");
    form->addRow(QCoreApplication::translate("Anvil", "Рушій llama-server"), picker(server_, QCoreApplication::translate("Anvil", "Рушій llama-server"), QCoreApplication::translate("Anvil", "Усі файли (*)")));
    form->addRow(cuda_);
    connect(cuda_, &QCheckBox::toggled, this, [this](bool on) {
        if (QFileInfo::exists(enginePath(on))) server_->setText(enginePath(on));
    });
    threads_ = new QSpinBox(this); threads_->setRange(1,64);
    threads_->setValue(settings.value("translation/threads", qBound(1,QThread::idealThreadCount(),8)).toInt());
    form->addRow(QCoreApplication::translate("Anvil", "Потоків CPU"), threads_);
    overwrite_ = new QCheckBox(QCoreApplication::translate("Anvil", "Дозволити заміну вже наявних перекладів"), this);
    useGlossary_ = new QCheckBox(QCoreApplication::translate("Anvil", "Підставляти точні відповідники з глосарія (перевірте відмінки)"), this);
    useGlossary_->setEnabled(!glossary_.isEmpty());
    skipService_=new QCheckBox(QCoreApplication::translate("Anvil", "Пропускати службові Name:, Context:, Acting:, //WARNING"),this);
    skipService_->setChecked(true);
    useCache_=new QCheckBox(QCoreApplication::translate("Anvil", "Зберігати проміжні результати для продовження після зупинки"),this);
    useCache_->setChecked(true);
    form->addRow(overwrite_); form->addRow(useGlossary_); form->addRow(skipService_); form->addRow(useCache_);
    layout->addLayout(form);
    groups_=anvil::groupTranslationEntries(entries_);
    targetIds_.resize(groups_.size());
    layout->addWidget(new QLabel(QString(QCoreApplication::translate("Anvil", "Записів: %1 · різних текстів: %2 · точні повтори об’єднано лише для перекладу"))
                                .arg(entries_.size()).arg(groups_.size()),this));
    table_ = new QTableWidget(groups_.size(),5,this);
    table_->setHorizontalHeaderLabels({QCoreApplication::translate("Anvil", "Застосувати"), QCoreApplication::translate("Anvil", "Оригінал"), QCoreApplication::translate("Anvil", "Результат моделі"), QCoreApplication::translate("Anvil", "Стан"), QCoreApplication::translate("Anvil", "Записів")});
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table_->setColumnWidth(0,95); table_->setColumnWidth(3,180);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (qsizetype r=0;r<groups_.size();++r) {
        auto *check = new QTableWidgetItem; check->setFlags(Qt::ItemIsEnabled); check->setCheckState(Qt::Unchecked);
        table_->setItem(r,0,check); table_->setItem(r,1,new QTableWidgetItem(groups_[r].source));
        table_->setItem(r,2,new QTableWidgetItem); table_->setItem(r,3,new QTableWidgetItem(QCoreApplication::translate("Anvil", "Очікує")));
        table_->setItem(r,4,new QTableWidgetItem(QString::number(groups_[r].rows.size())));
    }
    layout->addWidget(table_);
    status_ = new QLabel(QCoreApplication::translate("Anvil", "Модель завантажиться після натискання «Перекласти» та вивантажиться після завершення."),this);
    status_->setWordWrap(true); layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    start_ = new QPushButton(QCoreApplication::translate("Anvil", "Перекласти"),this); stop_ = new QPushButton(QCoreApplication::translate("Anvil", "Зупинити"),this);
    apply_ = new QPushButton(QCoreApplication::translate("Anvil", "Застосувати позначені"),this); close_ = new QPushButton(QCoreApplication::translate("Anvil", "Закрити"),this);
    for (auto *button : {start_,stop_,apply_,close_}) buttons->addWidget(button);
    layout->addLayout(buttons);
    connect(start_, &QPushButton::clicked,this,[this] { start(); });
    connect(stop_, &QPushButton::clicked,this,[this] { job_.requestInterruption(); status_->setText(QCoreApplication::translate("Anvil", "Зупиняється…")); });
    connect(apply_, &QPushButton::clicked,this,&QDialog::accept);
    connect(close_, &QPushButton::clicked,this,&TranslationDialog::reject);
    connect(&job_, &anvil::TranslationJob::progress,status_,&QLabel::setText);
    connect(&job_, &anvil::TranslationJob::failure,status_,&QLabel::setText);
    connect(&job_, &anvil::TranslationJob::result,this,[this](int index,const QString &text,const QString &error) {
        const int row = jobRows_.at(index);
        if (error.isEmpty()) hasResults_=true;
        table_->item(row,2)->setText(text);
        table_->item(row,3)->setText(error.isEmpty() ? QCoreApplication::translate("Anvil", "Готово — перевірте") : error);
        table_->item(row,3)->setToolTip(error);
        auto *check = table_->item(row,0);
        check->setFlags(error.isEmpty() ? Qt::ItemIsEnabled|Qt::ItemIsUserCheckable : Qt::ItemIsEnabled);
        check->setCheckState(error.isEmpty() ? Qt::Checked : Qt::Unchecked);
    });
    connect(&job_, &QThread::finished,this,[this] {
        if (status_->text().startsWith(QCoreApplication::translate("Anvil", "Готово."))) status_->setText(QCoreApplication::translate("Anvil", "Готово. Модель вивантажено. Перевірте результати."));
        else if (status_->text().startsWith(QCoreApplication::translate("Anvil", "Зупин")) || status_->text().startsWith(QCoreApplication::translate("Anvil", "Дочекайтеся")))
            status_->setText(QCoreApplication::translate("Anvil", "Зупинено. Модель вивантажено."));
        updateButtons();
    });
    updateButtons();
}
void TranslationDialog::start() {
    if (job_.isRunning()) return;
    job_.entries.clear(); jobRows_.clear(); hasResults_=false;
    table_->setUpdatesEnabled(false);
    for (qsizetype r=0;r<groups_.size();++r) {
        targetIds_[r].clear();
        auto *check=table_->item(r,0); check->setFlags(Qt::ItemIsEnabled); check->setCheckState(Qt::Unchecked);
        table_->item(r,2)->setText({});
        if (groups_[r].source.trimmed().isEmpty() ||
            (skipService_->isChecked() && anvil::isServiceTranslationText(groups_[r].source))) {
            table_->item(r,3)->setText(QCoreApplication::translate("Anvil", "Пропущено: службовий/порожній рядок")); continue;
        }
        for (int index:groups_[r].rows)
            if (overwrite_->isChecked() || entries_[index].translation.isEmpty())
                targetIds_[r].append(entries_[index].id);
        if (targetIds_[r].isEmpty()) {
            table_->item(r,3)->setText(QCoreApplication::translate("Anvil", "Пропущено: вже є переклад")); continue;
        }
        table_->item(r,3)->setText(QCoreApplication::translate("Anvil", "Очікує"));
        jobRows_.append(r); job_.entries.append(entries_[groups_[r].rows.front()]);
    }
    table_->setUpdatesEnabled(true);
    if (job_.entries.isEmpty()) { status_->setText(QCoreApplication::translate("Anvil", "Немає рядків для перекладу.")); return; }
    job_.settings={server_->text().trimmed(),model_->text().trimmed(),cuda_->isChecked(),threads_->value()};
    if (useCache_->isChecked()) {
        const auto cacheDir=QStandardPaths::isTestModeEnabled() ? QDir::tempPath()+"/anvil-ui-tests-cache"
            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        job_.settings.cacheFile=cacheDir+"/translation-progress.jsonl";
    }
    job_.glossary=useGlossary_->isChecked() ? glossary_ : QVector<anvil::Term>{};
    QSettings settings;
    settings.setValue("translation/model",job_.settings.model); settings.setValue("translation/server",job_.settings.server);
    settings.setValue("translation/cuda",job_.settings.cuda); settings.setValue("translation/threads",job_.settings.threads);
    job_.start(); updateButtons();
}
void TranslationDialog::updateButtons() {
    const bool running=job_.isRunning();
    start_->setEnabled(!running); stop_->setEnabled(running); close_->setEnabled(!running);
    apply_->setEnabled(!running && hasResults_);
    for (auto *w : QList<QWidget*>{model_,server_,cuda_,overwrite_,useGlossary_,skipService_,useCache_,threads_}) w->setEnabled(!running);
    if (!running && glossary_.isEmpty()) useGlossary_->setEnabled(false);
}
QMap<QString,QString> TranslationDialog::acceptedTranslations() const {
    QMap<QString,QString> result;
    for (qsizetype r=0;r<groups_.size();++r)
        if (table_->item(r,0)->checkState()==Qt::Checked && !table_->item(r,2)->text().isEmpty())
            for (const auto &id:targetIds_[r]) result.insert(id,table_->item(r,2)->text());
    return result;
}
void TranslationDialog::reject() {
    if (job_.isRunning()) { job_.requestInterruption(); status_->setText(QCoreApplication::translate("Anvil", "Дочекайтеся вивантаження моделі…")); return; }
    QDialog::reject();
}
void TranslationDialog::closeEvent(QCloseEvent *event) {
    if (job_.isRunning()) { job_.requestInterruption(); event->ignore(); return; }
    QDialog::closeEvent(event);
}
