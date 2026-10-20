#pragma once
#include <anvil/translation.h>
#include <QDialog>
#include <QMap>
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;
class TranslationDialog final : public QDialog {
  public:
    TranslationDialog(const QVector<anvil::Entry> &entries, const QVector<anvil::Term> &glossary, QWidget *parent, const QString &targetLanguage = "uk");
    QMap<QString, QString> acceptedTranslations() const;
  protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
  private:
    QString targetLanguage_;
    QVector<anvil::Entry> entries_;
    QVector<anvil::Term> glossary_;
    QVector<int> jobRows_;
    QVector<anvil::TranslationGroup> groups_;
    QVector<QVector<QString>> targetIds_;
    bool hasResults_ = false;
    anvil::TranslationJob job_;
    QLineEdit *model_, *server_;
    QCheckBox *cuda_, *overwrite_, *useGlossary_, *skipService_, *useCache_;
    QSpinBox *threads_;
    QTableWidget *table_;
    QPushButton *start_, *stop_, *apply_, *close_;
    QLabel *status_;
    void start();
    void updateButtons();
};
