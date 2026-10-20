#include <QStringConverter>
#include <QStringList>
#include <anvil/document.h>
#include <stdexcept>
namespace anvil {
static void fail(const char *message) { throw std::runtime_error(message); }
QByteArray toCsv(const Document &d) {
    validate(d);
    QString out = "id,source,translation,context,status\r\n";
    for (const auto &e : d.entries) {
        QStringList row{e.id, e.source, e.translation, e.context, e.status};
        for (auto &s : row) {
            s.replace('"', "\"\"");
            s = '"' + s + '"';
        }
        out += row.join(',') + "\r\n";
    }
    return out.toUtf8();
}
QVector<Entry> fromCsv(const QByteArray &bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(bytes);
    if (decoder.hasError())
        fail("CSV must use UTF-8");
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    enum State { Start, Plain, Quoted, Closed } state = Start;
    auto endField = [&] {
        row.append(field);
        field.clear();
        state = Start;
    };
    auto endRow = [&] {
        endField();
        rows.append(row);
        row.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (state == Quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else
                    state = Closed;
            } else
                field += c;
        } else if (c == ',')
            endField();
        else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            endRow();
        } else if (c == '"') {
            if (state != Start)
                fail("Unexpected quote in CSV");
            state = Quoted;
        } else {
            if (state == Closed)
                fail("Unexpected text after CSV quote");
            field += c;
            state = Plain;
        }
    }
    if (state == Quoted)
        fail("Unclosed CSV quote");
    if (state != Start || !row.isEmpty() || !field.isEmpty())
        endRow();
    if (rows.isEmpty() || rows.front() != QStringList{"id", "source", "translation", "context", "status"})
        fail("Expected CSV header: id,source,translation,context,status");
    Document d;
    for (qsizetype i = 1; i < rows.size(); ++i) {
        const auto &r = rows[i];
        if (r.size() != 5)
            fail("CSV row must contain five fields");
        d.entries.append({r[0], r[1], r[2], r[3], r[4]});
    }
    validate(d);
    return d.entries;
}
} // namespace anvil
