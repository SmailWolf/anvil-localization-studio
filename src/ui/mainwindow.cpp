#include <QCoreApplication>
#include "mainwindow.h"
#include "translationdialog.h"
#include "fontdialog.h"
#include "modeldownloaddialog.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStandardPaths>
#include <QProgressDialog>
#include <anvil/wildlands.h>
#include <QSignalBlocker>
#include <QSet>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <anvil/breakpoint.h>
#include <anvil/storage.h>
#include <functional>
#include <stdexcept>
namespace {
QString statusLabel(const QString &s) {
    if (s == "translated")
        return QCoreApplication::translate("Anvil", "Перекладено");
    if (s == "reviewed")
        return QCoreApplication::translate("Anvil", "Перевірено");
    return QCoreApplication::translate("Anvil", "До перекладу");
}
class TextDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *editor = new QPlainTextEdit(parent);
        editor->setToolTip(QCoreApplication::translate("Anvil", "Ctrl+Enter — підтвердити; Enter — новий рядок"));
        return editor;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        static_cast<QPlainTextEdit *>(editor)->setPlainText(index.data().toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        model->setData(index, static_cast<QPlainTextEdit *>(editor)->toPlainText());
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                key->modifiers().testFlag(Qt::ControlModifier)) {
                auto *editor = qobject_cast<QWidget *>(object);
                emit commitData(editor);
                emit closeEditor(editor);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }
};
class StatusDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant &value, const QLocale &) const override {
        return statusLabel(value.toString());
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *box = new QComboBox(parent);
        for (const auto &s : {"todo", "translated", "reviewed"})
            box->addItem(statusLabel(s), s);
        return box;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto *box = static_cast<QComboBox *>(editor);
        box->setCurrentIndex(box->findData(index.data()));
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        model->setData(index, static_cast<QComboBox *>(editor)->currentData());
    }
};
QString exceptionText(const std::exception &e) { return QString::fromUtf8(e.what()); }
void headers(QStandardItemModel &m, const QStringList &labels) { m.setHorizontalHeaderLabels(labels); }
} // namespace
MainWindow::MainWindow() : session_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    resize(1250, 780);
    setWindowTitle("Anvil Localization Studio — 0.8.0-beta.1");
    headers(entries_, {"ID", QCoreApplication::translate("Anvil", "Оригінал"), QCoreApplication::translate("Anvil", "Переклад"), QCoreApplication::translate("Anvil", "Контекст"), QCoreApplication::translate("Anvil", "Статус")});
    headers(glossary_, {QCoreApplication::translate("Anvil", "Термін оригіналу"), QCoreApplication::translate("Anvil", "Відповідник"), QCoreApplication::translate("Anvil", "Примітка")});
    filter_.setSourceModel(&entries_);
    filter_.setFilterKeyColumn(-1);
    filter_.setFilterCaseSensitivity(Qt::CaseInsensitive);
    entryView_ = new QTableView(this);
    entryView_->setObjectName("entries");
    entryView_->setModel(&filter_);
    entryView_->setItemDelegateForColumn(4, new StatusDelegate(entryView_));
    for (int column : {1, 2, 3})
        entryView_->setItemDelegateForColumn(column, new TextDelegate(entryView_));
    glossaryView_ = new QTableView(this);
    glossaryView_->setObjectName("glossary");
    glossaryView_->setModel(&glossary_);
    glossaryView_->setItemDelegateForColumn(2, new TextDelegate(glossaryView_));
    for (auto *view : {entryView_, glossaryView_}) {
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setStretchLastSection(true);
        view->verticalHeader()->setDefaultSectionSize(54);
        view->setAlternatingRowColors(true);
    }
    entryView_->setColumnWidth(0, 160);
    entryView_->setColumnWidth(1, 330);
    entryView_->setColumnWidth(2, 330);
    entryView_->setColumnWidth(3, 170);
    glossaryView_->setColumnWidth(0, 300);
    glossaryView_->setColumnWidth(1, 300);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(entryView_, QCoreApplication::translate("Anvil", "Редактор"));
    tabs->addTab(glossaryView_, QCoreApplication::translate("Anvil", "Глосарій"));
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *notice = new QLabel(QCoreApplication::translate("Anvil", "Wildlands / Breakpoint • редактор перекладів • JSON/CSV • без змін файлів гри"), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    layout->addWidget(tabs);
    setCentralWidget(central);
    auto *bar = addToolBar(QCoreApplication::translate("Anvil", "Проєкт"));
    bar->setMovable(false);
    auto addAction = [&](QMenu *menu, const QString &title, const QKeySequence &shortcut,
                         std::function<void()> fn, bool tool) {
        auto *a = menu->addAction(title);
        a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, [fn] { fn(); });
        if (tool)
            bar->addAction(a);
    };
    auto *file = menuBar()->addMenu(QCoreApplication::translate("Anvil", "Проєкт"));
    addAction(
        file, QCoreApplication::translate("Anvil", "Новий"), QKeySequence::New,
        [this] {
            if (mayDiscard()) {
                path_.clear();
                populate({});
                dirty_ = false;
                updateSummary();
            }
        },
        true);
    addAction(file, QCoreApplication::translate("Anvil", "Відкрити JSON…"), QKeySequence::Open, [this] { openProject(); }, true);
    addAction(file, QCoreApplication::translate("Anvil", "Зберегти JSON…"), QKeySequence::Save, [this] { save(); }, true);
    addAction(
        file, QCoreApplication::translate("Anvil", "Зберегти JSON як…"), QKeySequence::SaveAs,
        [this] {
            const auto old = path_;
            path_.clear();
            if (!save())
                path_ = old;
        },
        false);
    file->addSeparator();
    addAction(file, QCoreApplication::translate("Anvil", "Витягнути з гри…"), {}, [this] { extractGame(game_->currentData() == "wildlands"); }, true);
    addAction(file, QCoreApplication::translate("Anvil", "Витягнути тексти Wildlands…"), {}, [this] { extractGame(true); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Витягнути тексти Breakpoint…"), {}, [this] { extractGame(false); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Оновити проєкт із гри…"), {},
              [this] { extractGame(game_->currentData() == "wildlands", true); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Запакувати переклад…"), {}, [this] { packTranslation(); }, false);
    auto *advanced = file->addMenu(QCoreApplication::translate("Anvil", "Додатково — декодовані ресурси"));
    addAction(advanced, QCoreApplication::translate("Anvil", "Відкрити декодований ресурс Wildlands…"), {}, [this] { importBreakpoint(true); }, false);
    addAction(advanced, QCoreApplication::translate("Anvil", "Відкрити декодований ресурс Breakpoint…"), {}, [this] { importBreakpoint(); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Імпортувати CSV…"), {}, [this] { importCsv(); }, true);
    addAction(file, QCoreApplication::translate("Anvil", "Експортувати CSV…"), {}, [this] { exportCsv(); }, true);
    addAction(file, QCoreApplication::translate("Anvil", "Відновити автокопію…"), {}, [this] { recover(); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Мови проєкту…"), {}, [this] { editLanguages(); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Шрифти — Українська + Беларуская…"), {}, [this] { FontDialog dialog(this);dialog.exec(); }, false);
    addAction(file, QCoreApplication::translate("Anvil", "Закрити"), QKeySequence::Quit, [this] { close(); }, false);
    auto *edit = menuBar()->addMenu(QCoreApplication::translate("Anvil", "Редагування"));
    addAction(
        edit, QCoreApplication::translate("Anvil", "Додати рядок"), QKeySequence("Ctrl+Insert"),
        [this] {
            const QString id = "new_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            QList<QStandardItem *> row;
            for (const auto &s : QStringList{id, "", "", "", "todo"})
                row.append(new QStandardItem(s));
            entries_.appendRow(row);
            markDirty();
        },
        false);
    addAction(
        edit, QCoreApplication::translate("Anvil", "Видалити вибрані рядки"), {},
        [this] {
            QList<int> rows;
            for (const auto &i : entryView_->selectionModel()->selectedRows())
                rows.append(filter_.mapToSource(i).row());
            std::sort(rows.begin(), rows.end(), std::greater<int>());
            for (int r : rows)
                entries_.removeRow(r);
            if (!rows.isEmpty())
                markDirty();
        },
        false);
    addAction(
        edit, QCoreApplication::translate("Anvil", "Додати термін"), {},
        [this] {
            glossary_.appendRow(
                {new QStandardItem(QCoreApplication::translate("Anvil", "Новий термін ") + QString::number(glossary_.rowCount() + 1)),
                 new QStandardItem(QCoreApplication::translate("Anvil", "Відповідник")), new QStandardItem});
            markDirty();
        },
        false);
    addAction(
        edit, QCoreApplication::translate("Anvil", "Видалити вибрані терміни"), {},
        [this] {
            QList<int> rows;
            for (const auto &i : glossaryView_->selectionModel()->selectedRows())
                rows.append(i.row());
            std::sort(rows.begin(), rows.end(), std::greater<int>());
            for (int r : rows)
                glossary_.removeRow(r);
            if (!rows.isEmpty())
                markDirty();
        },
        false);
    auto *ai = menuBar()->addMenu(QCoreApplication::translate("Anvil", "Переклад"));
    addAction(ai, QCoreApplication::translate("Anvil", "Перекласти всі неперекладені…"), {}, [this] { translateSelected(true); }, false);
    addAction(ai, QCoreApplication::translate("Anvil", "Перекласти вибрані рядки…"), {}, [this] { translateSelected(); }, false);
    addAction(ai,QCoreApplication::translate("Anvil", "Завантажити модель…"),{},[this]{ModelDownloadDialog dialog(this);dialog.exec();},false);
    auto *language=menuBar()->addMenu(QCoreApplication::translate("Anvil", "Мова / Language"));
    for(const auto &item:QList<QPair<QString,QString>>{{QCoreApplication::translate("Anvil", "Українська"),"uk"},{"English","en"}}) {
        auto *action=language->addAction(item.first);
        action->setCheckable(true);action->setChecked(QSettings().value("interface/language","uk").toString()==item.second);
        connect(action,&QAction::triggered,this,[this,item]{
            QSettings().setValue("interface/language",item.second);
            QMessageBox::information(this,"Anvil Localization Studio",item.second=="en" ? "Restart the application to use English. Save your project before closing." : QCoreApplication::translate("Anvil", "Перезапустіть програму, щоб застосувати українську мову. Збережіть проєкт перед закриттям."));
        });
    }
    auto *help = menuBar()->addMenu(QCoreApplication::translate("Anvil", "Довідка"));
    addAction(help,QCoreApplication::translate("Anvil", "Інструкція користування…"),{},[this]{
        const auto name=QSettings().value("interface/language","uk").toString()=="en" ? "USER-GUIDE.en.md" : "USER-GUIDE.md";
        auto path=QCoreApplication::applicationDirPath()+"/../docs/"+name;
        if(!QFileInfo::exists(path)) {
            QDesktopServices::openUrl(QUrl("https://github.com/SmailWolf/anvil-localization-studio/blob/main/docs/"+QString(name)));
            return;
        }
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))error(QCoreApplication::translate("Anvil", "Не вдалося відкрити інструкцію."));
    },false);
    addAction(
        help, QCoreApplication::translate("Anvil", "Про формати та плагіни…"), {},
        [this] {
            QMessageBox::information(
                this, QCoreApplication::translate("Anvil", "Підтримка форматів"),
                QCoreApplication::translate("Anvil", "Breakpoint: перевірено read-only декодування LocalizationPackage для Steam build 25349072.\n"
                "Меню імпорту приймає декодований .localizationpayload. Утиліта anvil-extract-breakpoint "
                "читає англійські пакети з DataPC.forge і патча та створює JSON-проєкт.\n"
                "Wildlands: перевірено англійські пакети v27 / LZO з DataPC.forge та патча. "
                "Для обох ігор витягування та оновлення проєкту доступні через меню Проєкт.\n"
                "Це не загальна підтримка .forge. Є експериментальне пакування копій поза грою; "
                "встановлення не виконується, ігрова сумісність та шрифти ще потребують перевірки.\n"
                "Інструкції: docs/USER-GUIDE.md, docs/WILDLANDS.md, docs/BREAKPOINT.md"));
        },
        false);
    addToolBarBreak();
    auto *buildTools=addToolBar(QCoreApplication::translate("Anvil", "Переклад і пакування"));buildTools->setMovable(false);
    for(auto *action:findChildren<QAction*>())
        if(action->text()==QCoreApplication::translate("Anvil", "Перекласти всі неперекладені…")||action->text()==QCoreApplication::translate("Anvil", "Запакувати переклад…"))
            buildTools->addAction(action);
    addToolBarBreak();
    auto *tools = addToolBar(QCoreApplication::translate("Anvil", "Пошук"));
    tools->setMovable(false);
    game_ = new QComboBox(this);
    game_->addItem("Ghost Recon Wildlands", "wildlands");
    game_->addItem("Ghost Recon Breakpoint", "breakpoint");
    tools->addWidget(game_);
    search_ = new QLineEdit(this);
    search_->setObjectName("search");
    search_->setPlaceholderText(QCoreApplication::translate("Anvil", "Пошук у всіх полях…"));
    search_->setClearButtonEnabled(true);
    tools->addWidget(search_);
    status_ = new QComboBox(this);
    status_->setObjectName("statusFilter");
    status_->addItem(QCoreApplication::translate("Anvil", "Усі статуси"), "");
    for (const auto &s : {"todo", "translated", "reviewed"})
        status_->addItem(statusLabel(s), s);
    tools->addWidget(status_);
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString &text) { filter_.setFilterFixedString(text); });
    connect(status_, &QComboBox::currentIndexChanged, this,
            [this] { filter_.setStatus(status_->currentData().toString()); });
    connect(game_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_)
            markDirty();
    });
    connect(&entries_, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (loading_)
            return;
        if (item->column() == 1 || item->column() == 2) {
            loading_ = true;
            const auto text = entries_.index(item->row(), 2).data().toString();
            entries_.setData(entries_.index(item->row(), 4),
                             text.trimmed().isEmpty() ? "todo" : "translated");
            loading_ = false;
        }
        markDirty();
    });
    connect(&glossary_, &QStandardItemModel::itemChanged, this, [this] {
        if (!loading_)
            markDirty();
    });
    auto *timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this] { autosave(); });
    timer->start();
    populate({});
    if (!QDir(anvil::recoveryDirectory()).entryList({"*.json"}, QDir::Files).isEmpty())
        statusBar()->showMessage(QCoreApplication::translate("Anvil", "Доступні автокопії: Проєкт → Відновити автокопію…"));
}
MainWindow::~MainWindow() = default;
void MainWindow::commitEditor() {
    if (auto *w = QApplication::focusWidget())
        w->clearFocus();
}
void MainWindow::populate(const anvil::Document &d) {
    loading_ = true;
    entryView_->setUpdatesEnabled(false);
    filter_.setSourceModel(nullptr);
    metadata_ = d;
    {
        QSignalBlocker entrySignals(&entries_);
        QSignalBlocker glossarySignals(&glossary_);
        entries_.removeRows(0, entries_.rowCount());
        glossary_.removeRows(0, glossary_.rowCount());
        entries_.setRowCount(d.entries.size());
        for (qsizetype r = 0; r < d.entries.size(); ++r) {
            const auto &e = d.entries[r];
            const QStringList cells{e.id, e.source, e.translation, e.context, e.status};
            for (int c = 0; c < cells.size(); ++c) {
                auto *item = new QStandardItem(cells[c]);
                item->setToolTip(cells[c]);
                entries_.setItem(r, c, item);
            }
        }
        for (const auto &t : d.glossary)
            glossary_.appendRow(
                {new QStandardItem(t.source), new QStandardItem(t.target), new QStandardItem(t.note)});
    }
    filter_.setSourceModel(&entries_);
    // Replacing the proxy's source resets header sections; keep text columns usable.
    entryView_->setColumnWidth(0, 160);
    entryView_->setColumnWidth(1, 330);
    entryView_->setColumnWidth(2, 330);
    entryView_->setColumnWidth(3, 170);
    entryView_->setUpdatesEnabled(true);
    game_->setCurrentIndex(game_->findData(d.game));
    search_->clear();
    status_->setCurrentIndex(0);
    loading_ = false;
    updateSummary();
}
anvil::Document MainWindow::snapshot() const {
    anvil::Document d = metadata_;
    d.game = game_->currentData().toString();
    d.entries.clear();
    d.glossary.clear();
    for (int r = 0; r < entries_.rowCount(); ++r) {
        auto cell = [&](int c) { return entries_.index(r, c).data().toString(); };
        d.entries.append({cell(0), cell(1), cell(2), cell(3), cell(4)});
    }
    for (int r = 0; r < glossary_.rowCount(); ++r)
        d.glossary.append({glossary_.index(r, 0).data().toString(), glossary_.index(r, 1).data().toString(),
                           glossary_.index(r, 2).data().toString()});
    return d;
}
void MainWindow::markDirty() {
    dirty_ = true;
    updateSummary();
}
void MainWindow::updateSummary() {
    int translated = 0, reviewed = 0;
    for (int r = 0; r < entries_.rowCount(); ++r) {
        const auto s = entries_.index(r, 4).data().toString();
        translated += s == "translated";
        reviewed += s == "reviewed";
    }
    setWindowTitle(
        QString("%1%2 — Anvil Localization Studio 0.8.0-beta.1")
            .arg(dirty_ ? "*" : "", path_.isEmpty() ? QCoreApplication::translate("Anvil", "Новий проєкт") : QFileInfo(path_).fileName()));
    statusBar()->showMessage(QString(QCoreApplication::translate("Anvil", "Рядків: %1 | Перекладено: %2 | Перевірено: %3 | %4 → %5"))
                                 .arg(entries_.rowCount())
                                 .arg(translated)
                                 .arg(reviewed)
                                 .arg(metadata_.sourceLanguage, metadata_.targetLanguage));
}
void MainWindow::error(const QString &s) { QMessageBox::warning(this, QCoreApplication::translate("Anvil", "Операцію не виконано"), s); }
bool MainWindow::save() {
    commitEditor();
    try {
        const auto bytes = anvil::toJson(snapshot());
        QString destination = path_;
        if (destination.isEmpty())
            destination = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Anvil", "Зберегти окремий проєкт"), {},
                                                       "Anvil Studio (*.als.json)");
        if (destination.isEmpty())
            return false;
        if (!destination.endsWith(".als.json", Qt::CaseInsensitive))
            destination += ".als.json";
        anvil::saveWithBackup(destination, bytes);
        path_ = destination;
        dirty_ = false;
        if (!recoveryPath_.isEmpty()) {
            QFile::remove(recoveryPath_);
            recoveryPath_.clear();
        }
        updateSummary();
        return true;
    } catch (const std::exception &e) {
        error(exceptionText(e));
        return false;
    }
}
bool MainWindow::mayDiscard() {
    commitEditor();
    if (!dirty_)
        return true;
    const auto answer = QMessageBox::question(this, QCoreApplication::translate("Anvil", "Незбережені зміни"), QCoreApplication::translate("Anvil", "Зберегти зміни проєкту?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    if (answer == QMessageBox::Discard) {
        if (!recoveryPath_.isEmpty()) {
            QFile::remove(recoveryPath_);
            recoveryPath_.clear();
        }
        return true;
    }
    return false;
}
bool MainWindow::loadProjectPath(const QString &path) {
    try {
        const auto d = anvil::fromJson(anvil::readFile(path));
        if (!mayDiscard())
            return false;
        path_ = path;
        populate(d);
        dirty_ = false;
        updateSummary();
        return true;
    } catch (const std::exception &e) {
        error(exceptionText(e));
        return false;
    }
}
void MainWindow::openProject() {
    const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("Anvil", "Відкрити проєкт або JSON"), {}, "JSON (*.json)");
    if (!path.isEmpty())
        loadProjectPath(path);
}
void MainWindow::importBreakpoint(bool wildlands) {
    const auto path = QFileDialog::getOpenFileName(this,
        wildlands ? QCoreApplication::translate("Anvil", "Декодований пакет Wildlands") : QCoreApplication::translate("Anvil", "Декодований пакет Breakpoint"), {},
        "LocalizationPackage (*.localizationpayload)");
    if (path.isEmpty())
        return;
    try {
        if (QFileInfo(path).size() > 64 * 1024 * 1024)
            throw std::runtime_error(QCoreApplication::translate("Anvil", "Пакет перевищує 64 MiB").toStdString());
        const auto bytes = anvil::readFile(path);
        const auto d = wildlands ? anvil::wildlands::decodeLocalization(bytes)
                                 : anvil::breakpoint::decodeLocalization(bytes);
        if (!mayDiscard())
            return;
        path_.clear();
        populate(d);
        markDirty();
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void MainWindow::extractGame(bool wildlands, bool mergeCurrent) {
    commitEditor();
    anvil::Document current;
    try {
        if (mergeCurrent) {
            current = snapshot();
            if (current.entries.isEmpty()) throw std::runtime_error(QCoreApplication::translate("Anvil", "Спочатку відкрийте попередній проєкт перекладу").toStdString());
        }
    }
    catch (const std::exception &e) { error(exceptionText(e)); return; }
    const QString gameName = wildlands ? "Wildlands" : "Breakpoint";
    const auto root = QFileDialog::getExistingDirectory(this,
        QCoreApplication::translate("Anvil", "Крок 1 із 2 — оберіть головну папку ") + gameName);
    if (root.isEmpty()) return;
    const auto executableName = wildlands ? "GRW.exe" : "GRB.exe";
    if (!QFileInfo(root + "/" + executableName).isFile() || !QFileInfo(root + "/DataPC.forge").isFile()) {
        error(QCoreApplication::translate("Anvil", "Оберіть головну папку ") + gameName + QCoreApplication::translate("Anvil", ", де лежать ") + executableName +
              QCoreApplication::translate("Anvil", " та DataPC.forge. Папки videos, sounddata або support не підходять."));
        return;
    }
    QString projectsDir;
    try {
        projectsDir = anvil::requireOutsideGame(
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Anvil Projects", root);
        if (!QDir().mkpath(projectsDir)) throw std::runtime_error(QCoreApplication::translate("Anvil", "Не вдалося створити папку Anvil Projects у Документах").toStdString());
    } catch (const std::exception &e) { error(exceptionText(e)); return; }
    const auto baseName = gameName + (mergeCurrent ? "-UA-updated" : "-UA");
    auto suggested = projectsDir + "/" + baseName + ".als.json";
    for (int number = 2; QFileInfo::exists(suggested); ++number)
        suggested = projectsDir + "/" + baseName + "-" + QString::number(number) + ".als.json";
    auto output = QFileDialog::getSaveFileName(this,
        QCoreApplication::translate("Anvil", "Крок 2 із 2 — збережіть проєкт у Документах, поза грою"), suggested,
        QCoreApplication::translate("Anvil", "Проєкт перекладу (*.als.json)"));
    if (output.isEmpty())
        return;
    if (!output.endsWith(".als.json", Qt::CaseInsensitive))
        output += ".als.json";
    try { output = anvil::requireOutsideGame(output, root); }
    catch (const std::exception &e) { error(exceptionText(e)); return; }
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink()) {
        error(QCoreApplication::translate("Anvil", "Оберіть нову назву: витягування не перезаписує наявні проєкти."));
        return;
    }
#ifndef Q_OS_WIN
    const auto wineRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    try { if (!wildlands) anvil::requireOutsideGame(wineRoot + "/breakpoint-wine", root); }
    catch (const std::exception &e) { error(exceptionText(e)); return; }
    if (!wildlands && !QDir().mkpath(wineRoot)) {
        error(QCoreApplication::translate("Anvil", "Не вдалося створити робочу папку для декомпресії Breakpoint."));
        return;
    }
#endif
    QProcess process;
    QProgressDialog progress(QCoreApplication::translate("Anvil", "Читання текстів ") + gameName + QCoreApplication::translate("Anvil", ". Файли гри не змінюються…"), QString(), 0, 0, this);
    progress.setWindowTitle(QCoreApplication::translate("Anvil", "Витягування текстів"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, &progress, [&] {
        timedOut = true;
        process.kill();
        progress.reject();
    });
    connect(&process, &QProcess::finished, &progress, &QDialog::accept);
    connect(&process, &QProcess::errorOccurred, &progress, [&] { progress.reject(); });
    const auto executable = QCoreApplication::applicationDirPath() +
        (wildlands ? "/anvil-extract-wildlands" : "/anvil-extract-breakpoint") + QString()
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;
    QTimer::singleShot(0, &progress, [&] {
        QStringList arguments{"--game-dir", root, "--output", output};
#ifndef Q_OS_WIN
        if (!wildlands)
            arguments << "--wine-prefix" << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                               + "/breakpoint-wine";
#endif
        process.start(executable, arguments);
        timeout.start(180000);
    });
    progress.exec();
    timeout.stop();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    if (timedOut || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        process.error() == QProcess::FailedToStart) {
        const auto details = QString::fromUtf8(process.readAllStandardError()).trimmed().left(3000);
        if (timedOut) error(QCoreApplication::translate("Anvil", "Читання текстів перевищило час очікування. Поточний проєкт не змінено."));
        else if (details.contains("Output and Wine prefix must be outside") || details.contains("Output must be outside"))
            error(QCoreApplication::translate("Anvil", "Проєкт або робоча папка декомпресії опинилися в папці гри. Збережіть JSON у Документах → Anvil Projects."));
        else if (process.error() == QProcess::FailedToStart)
            error(QCoreApplication::translate("Anvil", "Не вдалося запустити читач гри. Розпакуйте весь пакет програми разом із папкою bin.\n") + process.errorString());
        else error(QCoreApplication::translate("Anvil", "Не вдалося прочитати тексти гри.\n") + (details.isEmpty() ? QCoreApplication::translate("Anvil", "Читач завершив роботу з помилкою.") : details));
        return;
    }
    if (mergeCurrent) {
        try {
            const auto extracted = anvil::fromJson(anvil::readFile(output));
            const auto result = anvil::mergeUpdated(current, extracted);
            const auto priorPath = output + ".before-update." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".als.json";
            anvil::saveWithBackup(priorPath, anvil::toJson(current));
            anvil::saveWithBackup(output, anvil::toJson(result.document));
            // The new project includes all unchanged in-memory translations; the old file is retained.
            populate(result.document);
            path_ = output;
            dirty_ = false;
            updateSummary();
            QMessageBox::information(this, QCoreApplication::translate("Anvil", "Оновлення проєкту"),
                QString(QCoreApplication::translate("Anvil", "Перенесено перекладів: %1\nНових рядків: %2\nЗмінено оригінал: %3\nВилучено з гри: %4\n"
                        "Нові й змінені рядки потребують перекладу. Попередній файл проєкту не змінено."))
                    .arg(result.preserved).arg(result.added).arg(result.changed).arg(result.removed));
        } catch (const std::exception &e) {
            error(exceptionText(e));
        }
    } else {
        loadProjectPath(output);
    }
}
void MainWindow::importCsv() {
    const auto path =
        QFileDialog::getOpenFileName(this, QCoreApplication::translate("Anvil", "Імпорт CSV замінить рядки, зберігши глосарій"), {}, "CSV (*.csv)");
    if (path.isEmpty())
        return;
    try {
        const auto rows = anvil::fromCsv(anvil::readFile(path));
        if (!mayDiscard())
            return;
        auto d = snapshot();
        d.entries = rows;
        populate(d);
        markDirty();
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void MainWindow::exportCsv() {
    commitEditor();
    try {
        const auto bytes = anvil::toCsv(snapshot());
        auto path =
            QFileDialog::getSaveFileName(this, QCoreApplication::translate("Anvil", "Експорт усіх рядків (без глосарія)"), {}, "CSV (*.csv)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".csv", Qt::CaseInsensitive))
            path += ".csv";
        anvil::saveWithBackup(path, bytes);
        statusBar()->showMessage(QCoreApplication::translate("Anvil", "CSV експортовано; повний проєкт зберігається окремо як JSON."));
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void MainWindow::editLanguages() {
    QDialog dialog(this);
    dialog.setWindowTitle(QCoreApplication::translate("Anvil", "Мови проєкту"));
    QFormLayout layout(&dialog);
    QLineEdit source(metadata_.sourceLanguage);
    QComboBox target; target.setObjectName("projectTargetLanguage");
    target.addItem(QCoreApplication::translate("Anvil", "Українська"), "uk");
    target.addItem("Беларуская", "be");
    if (target.findData(metadata_.targetLanguage)<0) target.addItem(metadata_.targetLanguage,metadata_.targetLanguage);
    target.setCurrentIndex(target.findData(metadata_.targetLanguage));
    layout.addRow(QCoreApplication::translate("Anvil", "Мова оригіналу"), &source);
    layout.addRow(QCoreApplication::translate("Anvil", "Мова перекладу"), &target);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        if (source.text().trimmed().isEmpty() || target.currentData().toString().isEmpty()) {
            error(QCoreApplication::translate("Anvil", "Мови не можуть бути порожніми."));
            return;
        }
        if (target.currentData().toString()!=metadata_.targetLanguage) {
            const auto doc=snapshot();
            if (!doc.glossary.isEmpty() || std::any_of(doc.entries.begin(),doc.entries.end(),[](const auto &e){return !e.translation.isEmpty();})) {
                error(QCoreApplication::translate("Anvil", "У проєкті вже є переклади або глосарій. Для іншої мови створіть окремий проєкт з оригінальних текстів: переклади не можна просто перейменувати."));
                return;
            }
        }
        metadata_.sourceLanguage = source.text().trimmed();
        metadata_.targetLanguage = target.currentData().toString();
        markDirty();
    }
}
void MainWindow::autosave() {
    if (!dirty_)
        return;
    // Do not move focus or interrupt an active edit; recover the last committed table state.
    try {
        recoveryPath_ = anvil::writeRecovery(snapshot(), session_);
        statusBar()->showMessage(QCoreApplication::translate("Anvil", "Автокопія: ") + recoveryPath_);
    } catch (const std::exception &e) {
        statusBar()->showMessage(QCoreApplication::translate("Anvil", "Автокопію не створено: ") + exceptionText(e));
    }
}
void MainWindow::recover() {
    const auto path =
        QFileDialog::getOpenFileName(this, QCoreApplication::translate("Anvil", "Відновити автокопію або резервний JSON"),
                                     anvil::recoveryDirectory(), "JSON / backup (*.json *.bak)");
    if (path.isEmpty())
        return;
    try {
        const auto d = anvil::fromJson(anvil::readFile(path));
        if (!mayDiscard())
            return;
        path_.clear();
        populate(d);
        markDirty();
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (mayDiscard())
        event->accept();
    else
        event->ignore();
}
void MainWindow::loadExample(const anvil::Document &d) {
    populate(d);
    dirty_ = false;
    updateSummary();
}

void MainWindow::translateSelected(bool allUntranslated) {
    commitEditor();
    try {
        const auto document = snapshot();
        if (!(document.sourceLanguage == "en" || document.sourceLanguage == "en-US") || (document.targetLanguage != "uk" && document.targetLanguage != "be"))
            throw std::runtime_error(QCoreApplication::translate("Anvil", "Оберіть English → Українська або Беларуская у мовах проєкту.").toStdString());
        QSet<int> rows;
        if (allUntranslated) {
            for (qsizetype r=0;r<document.entries.size();++r)
                if (document.entries[r].translation.isEmpty()) rows.insert(r);
        } else {
            for (const auto &index : entryView_->selectionModel()->selectedRows())
                rows.insert(filter_.mapToSource(index).row());
        }
        if (rows.isEmpty()) {
            error(allUntranslated ? QCoreApplication::translate("Anvil", "У проєкті немає неперекладених рядків.") :
                  QCoreApplication::translate("Anvil", "Виберіть рядки таблиці для перекладу (Ctrl або Shift для кількох)."));
            return;
        }
        auto ordered = rows.values(); std::sort(ordered.begin(), ordered.end());
        QVector<anvil::Entry> selected;
        for (int row : ordered) selected.append(document.entries.at(row));
        TranslationDialog dialog(selected, document.glossary, this, document.targetLanguage);
        if (dialog.exec() != QDialog::Accepted) return;
        const auto accepted = dialog.acceptedTranslations();
        int first=-1,last=-1;
        loading_=true;
        {
            const QSignalBlocker blocker(&entries_);
            for (int row : ordered) {
                const auto &entry = document.entries.at(row);
                if (accepted.contains(entry.id)) {
                    entries_.setData(entries_.index(row,2),accepted.value(entry.id));
                    entries_.setData(entries_.index(row,4),"translated");
                    if (first<0) first=row;
                    last=row;
                }
            }
        }
        if (first>=0)
            emit entries_.dataChanged(entries_.index(first,2),entries_.index(last,4));
        loading_=false;
        if (first>=0) markDirty();
    } catch (const std::exception &e) { error(exceptionText(e)); }
}

void MainWindow::packTranslation() {
    commitEditor();
    try {
        const auto document=snapshot();
        qsizetype translated=0,reviewed=0;
        for(const auto &entry:document.entries) {
            translated+=!entry.translation.isEmpty()&&entry.status!="todo";
            reviewed+=entry.status=="reviewed";
        }
        if(!translated) { error(QCoreApplication::translate("Anvil", "Спочатку додайте переклад і підтвердьте комірку Ctrl+Enter. Немає перекладених рядків для пакування.")); return; }
        QDialog dialog(this);dialog.setWindowTitle(QCoreApplication::translate("Anvil", "Запакувати переклад — ")+document.game);dialog.resize(760,300);
        auto *layout=new QVBoxLayout(&dialog);
        auto *description=new QLabel(QString(QCoreApplication::translate("Anvil", "Перекладених рядків: %1 · перевірених: %2. Неперекладені залишаться оригінальними."))
                                      .arg(translated).arg(reviewed),&dialog);
        description->setWordWrap(true);layout->addWidget(description);
        auto *form=new QFormLayout;
        QSettings settings;
        auto *game=new QLineEdit(settings.value("pack/game/"+document.game).toString(),&dialog);
        game->setObjectName("packGameDirectory");
        auto *output=new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+
            "/Anvil Projects/"+document.game+"-UA-package-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),&dialog);
        output->setObjectName("packOutputDirectory");
        auto row=[&](QLineEdit *field,bool chooseGame) {
            auto *widget=new QWidget(&dialog);auto *box=new QHBoxLayout(widget);box->setContentsMargins(0,0,0,0);
            auto *browse=new QPushButton(QCoreApplication::translate("Anvil", "Обрати…"),widget);box->addWidget(field);box->addWidget(browse);
            connect(browse,&QPushButton::clicked,&dialog,[&,field,chooseGame] {
                const auto directory=QFileDialog::getExistingDirectory(&dialog,chooseGame?QCoreApplication::translate("Anvil", "Папка оригінальної гри"):QCoreApplication::translate("Anvil", "Батьківська папка нового пакета"),field->text());
                if(!directory.isEmpty())field->setText(chooseGame?directory:directory+"/"+document.game+"-UA-package-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
            });return widget;
        };
        form->addRow(QCoreApplication::translate("Anvil", "Папка гри (лише читання)"),row(game,true));
        form->addRow(QCoreApplication::translate("Anvil", "Нова папка пакета поза грою"),row(output,false));layout->addLayout(form);
        auto *full=new QCheckBox(QCoreApplication::translate("Anvil", "Створити повні змінені копії .forge у папці archives/"),&dialog);full->setChecked(true);layout->addWidget(full);
        auto *onlyReviewed=new QCheckBox(QCoreApplication::translate("Anvil", "Пакувати лише перевірені рядки"),&dialog);layout->addWidget(onlyReviewed);
        auto *explanation=new QLabel(QCoreApplication::translate("Anvil", "Повні копії потребують місця за розміром вихідних архівів — це може бути багато ГБ. "
            "Без цього прапорця створюється компактний пакет для подальшого складання.\n\n"
            "Експериментальний запис: структура й тексти перевіряються, але запуск нових архівів у грі ще не підтверджено. "
            "UMAC не перераховується; шрифт для українських літер не додається. Установлена гра не змінюється."),&dialog);
        explanation->setWordWrap(true);layout->addWidget(explanation);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("Anvil", "Запакувати"));layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        const auto root=QFileInfo(game->text().trimmed()).canonicalFilePath();
        if(root.isEmpty())throw std::runtime_error(QCoreApplication::translate("Anvil", "Оберіть папку гри").toStdString());
        const auto destination=anvil::requireOutsideGame(output->text().trimmed(),root);
        if(QFileInfo(destination).exists())throw std::runtime_error(QCoreApplication::translate("Anvil", "Папка пакета вже існує. Оберіть нову назву.").toStdString());
        if(!QDir().mkpath(QFileInfo(destination).absolutePath()))throw std::runtime_error(QCoreApplication::translate("Anvil", "Не вдалося створити батьківську папку пакета").toStdString());
        QTemporaryDir snapshot;
        if(!snapshot.isValid())throw std::runtime_error(QCoreApplication::translate("Anvil", "Не вдалося створити тимчасовий знімок перекладу").toStdString());
        anvil::requireOutsideGame(snapshot.path(),root);
        const auto project=snapshot.filePath("translation.als.json");anvil::saveWithBackup(project,anvil::toJson(document));
        settings.setValue("pack/game/"+document.game,root);
        QProcess process;QProgressDialog progress(QCoreApplication::translate("Anvil", "Підготовка пакета…"),QCoreApplication::translate("Anvil", "Зупинити"),0,0,this);
        progress.setWindowTitle(QCoreApplication::translate("Anvil", "Пакування перекладу"));progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);
        bool cancelled=false;
        connect(&progress,&QProgressDialog::canceled,this,[&]{cancelled=true;process.kill();});
        connect(&process,&QProcess::finished,&progress,&QDialog::accept);
        connect(&process,&QProcess::errorOccurred,&progress,&QDialog::reject);
        connect(&process,&QProcess::readyReadStandardOutput,&progress,[&]{
            const auto text=QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            if(!text.isEmpty())progress.setLabelText(text.section('\n',-1));
        });
        QTimer::singleShot(0,&progress,[&]{
            QString program=QCoreApplication::applicationDirPath()+"/anvil-pack-translation";
#ifdef Q_OS_WIN
            program+=".exe";
#endif
            QStringList arguments{"--mode","pack","--project",project,"--game-dir",root,"--output-dir",destination};
            if(!full->isChecked())arguments<<"--delta-only";
            if(onlyReviewed->isChecked())arguments<<"--reviewed-only";
#ifndef Q_OS_WIN
            arguments<<"--wine-prefix"<<QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/breakpoint-wine";
#endif
            process.start(program,arguments);
        });
        progress.exec();
        if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(3000);}
        if(cancelled){error(QCoreApplication::translate("Anvil", "Пакування зупинено. Файли гри не змінено. Результат із позначкою INCOMPLETE не готовий до використання."));return;}
        if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0||process.error()==QProcess::FailedToStart){
            const auto details=QString::fromUtf8(process.readAllStandardError()).left(4000);
            error(QCoreApplication::translate("Anvil", "Не вдалося зібрати пакет. Файли гри не змінено.\n")+(details.isEmpty()?process.errorString():details));return;
        }
        QMessageBox message(QMessageBox::Information,QCoreApplication::translate("Anvil", "Пакет створено"),
            QCoreApplication::translate("Anvil", "Переклад запаковано у:\n")+destination+QCoreApplication::translate("Anvil", "\n\nЦе експериментальний пакет; він не встановлений у гру. "
            "Перед ігровим тестом потрібні резервні копії та сумісний шрифт. Дивіться ПРОЧИТАЙТЕ.txt і manifest.json."),QMessageBox::Ok,this);
        auto *open=message.addButton(QCoreApplication::translate("Anvil", "Відкрити папку"),QMessageBox::ActionRole);message.exec();
        if(message.clickedButton()==open)QDesktopServices::openUrl(QUrl::fromLocalFile(destination));
    }catch(const std::exception &e){error(exceptionText(e));}
}
