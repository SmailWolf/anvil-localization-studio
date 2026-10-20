#pragma once
#include "document.h"
#include <QThread>
namespace anvil {
struct ProtectedText { QString masked; QVector<QPair<QString, QString>> replacements; };
ProtectedText protectTranslation(const QString &source, const QVector<Term> &glossary = {});
QString restoreTranslation(const QString &output, const ProtectedText &plan);
struct TranslationGroup { QString source; QVector<int> rows; };
QVector<TranslationGroup> groupTranslationEntries(const QVector<Entry> &entries);
bool isServiceTranslationText(const QString &source);
struct LocalModelSettings {
    QString server, model;
    bool cuda = true;
    int threads = 4;
    QString cacheFile = {};
    QString sourceLanguage = "en", targetLanguage = "uk";
};
// Owns one loopback-only llama.cpp process for a batch, then releases model memory.
class TranslationJob final : public QThread {
    Q_OBJECT
  public:
    LocalModelSettings settings;
    QVector<Entry> entries;
    QVector<Term> glossary;
    using QThread::QThread;
  signals:
    void progress(const QString &message);
    void result(int row, const QString &translation, const QString &error);
    void failure(const QString &message);
  protected:
    void run() override;
};
} // namespace anvil
