#pragma once
#include <QComboBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <anvil/document.h>
class EntryFilter : public QSortFilterProxyModel {
  public:
    QString status;
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setStatus(const QString &value) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        status = value;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        status = value;
        invalidateFilter();
#endif
    }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        return (status.isEmpty() || sourceModel()->index(row, 4, parent).data().toString() == status) &&
               QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};
class MainWindow : public QMainWindow {
  public:
    MainWindow();
    ~MainWindow() override;
    void loadExample(const anvil::Document &document);
    bool loadProjectPath(const QString &path);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    QStandardItemModel entries_, glossary_;
    EntryFilter filter_;
    QTableView *entryView_, *glossaryView_;
    QLineEdit *search_;
    QComboBox *status_, *game_;
    anvil::Document metadata_;
    QString path_, session_, recoveryPath_;
    bool dirty_ = false, loading_ = false;
    void populate(const anvil::Document &document);
    anvil::Document snapshot() const;
    void markDirty();
    void updateSummary();
    void error(const QString &message);
    bool save();
    bool mayDiscard();
    void openProject();
    void importCsv();
    void importBreakpoint(bool wildlands = false);
    void extractGame(bool wildlands, bool mergeCurrent = false);
    void translateSelected(bool allUntranslated = false);
    void packTranslation();
    void exportCsv();
    void editLanguages();
    void recover();
    void autosave();
    void commitEditor();
};
