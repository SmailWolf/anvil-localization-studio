#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QHash>
#include <QStringConverter>
#include <anvil/document.h>
#include <stdexcept>
namespace anvil {
static void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
static QString string(const QJsonObject &obj, const QString &key) {
    if (!obj.value(key).isString())
        fail("Expected string: " + key);
    return obj.value(key).toString();
}
void validate(const Document &d) {
    if (d.game != "wildlands" && d.game != "breakpoint")
        fail("Unknown game profile");
    if (d.sourceLanguage.trimmed().isEmpty() || d.targetLanguage.trimmed().isEmpty())
        fail("Language is empty");
    QSet<QString> ids;
    for (const auto &e : d.entries) {
        if (e.id.trimmed().isEmpty() || ids.contains(e.id))
            fail("Empty or duplicate ID: " + e.id);
        ids.insert(e.id);
        if (e.status != "todo" && e.status != "translated" && e.status != "reviewed")
            fail("Invalid status: " + e.status);
        if (e.status != "todo" && e.translation.trimmed().isEmpty())
            fail("Translation required for status: " + e.id);
    }
    QSet<QString> terms;
    for (const auto &t : d.glossary) {
        const auto key = t.source.trimmed().toCaseFolded();
        if (key.isEmpty() || t.target.trimmed().isEmpty() || terms.contains(key))
            fail("Empty or duplicate glossary term: " + t.source);
        terms.insert(key);
    }
}
UpdateResult mergeUpdated(const Document &previous, const Document &extracted) {
    validate(previous);
    validate(extracted);
    if (previous.game != extracted.game || previous.sourceLanguage != extracted.sourceLanguage)
        fail("Оновлення потребує тієї самої гри та мови оригіналу");
    UpdateResult result;
    result.document = extracted;
    result.document.targetLanguage = previous.targetLanguage;
    result.document.glossary = previous.glossary;
    QHash<QString, const Entry *> old;
    for (const auto &entry : previous.entries)
        old.insert(entry.id, &entry);
    for (auto &entry : result.document.entries) {
        const auto it = old.find(entry.id);
        entry.translation.clear();
        entry.status = "todo";
        if (it == old.end()) {
            ++result.added;
        } else {
            const auto &prior = **it;
            if (prior.source == entry.source) {
                entry.translation = prior.translation;
                entry.status = prior.status;
                if (!entry.translation.isEmpty()) ++result.preserved;
            } else {
                ++result.changed;
            }
            old.erase(it);
        }
    }
    result.removed = old.size();
    validate(result.document);
    return result;
}
QByteArray toJson(const Document &d) {
    validate(d);
    QJsonArray entries, glossary;
    for (const auto &e : d.entries)
        entries.append(QJsonObject{{"id", e.id},
                                   {"source", e.source},
                                   {"translation", e.translation},
                                   {"context", e.context},
                                   {"status", e.status}});
    for (const auto &t : d.glossary)
        glossary.append(QJsonObject{{"source", t.source}, {"target", t.target}, {"note", t.note}});
    return QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                     {"game", d.game},
                                     {"sourceLanguage", d.sourceLanguage},
                                     {"targetLanguage", d.targetLanguage},
                                     {"entries", entries},
                                     {"glossary", glossary}})
        .toJson();
}
Document fromJson(const QByteArray &bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(bytes);
    if (decoder.hasError())
        fail("Invalid UTF-8");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        fail("Invalid JSON: " + error.errorString());
    const auto obj = json.object();
    if (obj.value("schemaVersion") != QJsonValue(1))
        fail("Unsupported schemaVersion");
    if (!obj.value("entries").isArray() || !obj.value("glossary").isArray())
        fail("Expected entries and glossary arrays");
    Document d;
    d.game = string(obj, "game");
    d.sourceLanguage = string(obj, "sourceLanguage");
    d.targetLanguage = string(obj, "targetLanguage");
    for (const auto v : obj.value("entries").toArray()) {
        if (!v.isObject())
            fail("Expected entry object");
        const auto e = v.toObject();
        d.entries.append({string(e, "id"), string(e, "source"), string(e, "translation"),
                          string(e, "context"), string(e, "status")});
    }
    for (const auto v : obj.value("glossary").toArray()) {
        if (!v.isObject())
            fail("Expected glossary object");
        const auto t = v.toObject();
        d.glossary.append({string(t, "source"), string(t, "target"), string(t, "note")});
    }
    validate(d);
    return d;
}
} // namespace anvil
