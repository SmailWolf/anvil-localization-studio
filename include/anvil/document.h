#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
namespace anvil {
struct Entry {
    QString id, source, translation, context;
    QString status = QStringLiteral("todo");
    bool operator==(const Entry &) const = default;
};
struct Term {
    QString source, target, note;
    bool operator==(const Term &) const = default;
};
struct Document {
    QString game = QStringLiteral("wildlands");
    QString sourceLanguage = QStringLiteral("en");
    QString targetLanguage = QStringLiteral("uk");
    QVector<Entry> entries;
    QVector<Term> glossary;
    bool operator==(const Document &) const = default;
};
struct UpdateResult {
    Document document;
    qsizetype preserved = 0, added = 0, changed = 0, removed = 0;
};
UpdateResult mergeUpdated(const Document &previous, const Document &extracted);
// Validation throws std::runtime_error; callers retain the old document on failure.
void validate(const Document &document);
QByteArray toJson(const Document &document);
Document fromJson(const QByteArray &bytes);
QByteArray toCsv(const Document &document);
QVector<Entry> fromCsv(const QByteArray &bytes);
} // namespace anvil
