#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <anvil/breakpoint.h>
#include <limits>
#include <stdexcept>
namespace anvil::breakpoint {
namespace {
[[noreturn]] void fail(const char *text) { throw std::runtime_error(text); }
template <class T> T readBE(const QByteArray &b, qsizetype p) {
    if (p < 0 || p > b.size() || qsizetype(sizeof(T)) > b.size() - p)
        fail("Truncated localization input");
    quint64 v = 0;
    for (qsizetype i = 0; i < qsizetype(sizeof(T)); ++i)
        v = v << 8 | quint8(b.at(p + i));
    return static_cast<T>(v);
}
template <class T> void putBE(QByteArray &b, T v) {
    char bytes[sizeof(T)];
    qToBigEndian(v, bytes);
    b.append(bytes, sizeof(T));
}
struct Record {
    quint32 id;
    QByteArray encoded;
};
} // namespace
QByteArray replaceLocalization(const QByteArray &original, const QMap<quint32, QString> &replacements) {
    const auto before = decodeLocalization(original);
    if (replacements.isEmpty())
        return original;
    if (before.entries.isEmpty())
        fail("Cannot replace text in an empty package");
    const auto data = original.mid(41);
    const auto threshold = readBE<quint16>(data, 0), nodeCount = readBE<quint16>(data, 2);
    QVector<QPair<quint16, quint16>> dictionary;
    QMap<quint16, quint16> leaves;
    for (quint32 i = 0; i < nodeCount; ++i) {
        const auto a = readBE<quint16>(data, 4 + 4 * i), b = readBE<quint16>(data, 6 + 4 * i);
        dictionary.append({a, b});
        if (i > 0 && a != 0 && b == 0 && !leaves.contains(a))
            leaves[a] = i;
    }
    auto encode = [&](const QString &text) {
        if (!text.isValidUtf16() || text.contains(QChar(0)))
            fail("Replacement must contain valid UTF-16 without NUL");
        QByteArray bytes;
        for (QChar c : text) {
            const auto unit = c.unicode();
            if (!leaves.contains(unit)) {
                if (dictionary.size() >= 65535)
                    fail("Localization dictionary is full");
                leaves[unit] = quint16(dictionary.size());
                dictionary.append({unit, 0});
            }
            const quint32 code = leaves[unit] - 1;
            if (code < threshold)
                bytes.append(char(code));
            else if (quint32(threshold) * 255 + code <= 0xfeff)
                putBE<quint16>(bytes, quint16(quint32(threshold) * 255 + code));
            else {
                bytes.append(char(255));
                putBE<quint16>(bytes, quint16(code));
            }
        }
        if (bytes.size() > 65535)
            fail("Replacement exceeds one encoded localization block");
        return bytes;
    };
    QVector<Record> records;
    QSet<quint32> applied;
    qsizetype p = 4 + 4 * nodeCount;
    const auto groupCount = readBE<quint16>(data, p);
    p += 2;
    for (quint32 group = 0; group < groupCount; ++group) {
        const auto key = readBE<quint32>(data, p), chars = readBE<quint32>(data, p + 4),
                   limits = readBE<quint32>(data, p + 8);
        p += 12;
        const auto count = readBE<quint16>(data, limits);
        quint32 start = 0;
        for (quint32 j = 0; j <= count; ++j) {
            const quint16 delta = j == 0 ? 0 : readBE<quint16>(data, qsizetype(limits) + 4 * j);
            const quint16 end = readBE<quint16>(data, qsizetype(limits) + 2 + 4 * j);
            const quint64 wide = quint64(key) + delta;
            if (wide > std::numeric_limits<quint32>::max())
                fail("String ID overflow");
            const auto id = quint32(wide);
            auto bytes = data.mid(qsizetype(chars) + start, end - start);
            if (replacements.contains(id)) {
                bytes = encode(replacements[id]);
                applied.insert(id);
            }
            records.append({id, bytes});
            start = end;
        }
    }
    if (applied.size() != replacements.size())
        fail("Replacement ID was not found in the original package");
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    struct Group {
        quint32 base;
        QVector<Record> records;
        QByteArray text;
    };
    QVector<Group> groups;
    for (const auto &record : records) {
        if (groups.isEmpty() || quint64(record.id) - groups.back().base > 65535 ||
            groups.back().text.size() + record.encoded.size() > 65535 ||
            groups.back().records.size() >= 65536)
            groups.append({record.id, {}, {}});
        auto &g = groups.back();
        g.records.append(record);
        g.text += record.encoded;
    }
    if (groups.size() > 65535)
        fail("Too many localization groups");
    QByteArray rebuilt;
    putBE<quint16>(rebuilt, threshold);
    putBE<quint16>(rebuilt, dictionary.size());
    for (const auto &pair : dictionary) {
        putBE(rebuilt, pair.first);
        putBE(rebuilt, pair.second);
    }
    putBE<quint16>(rebuilt, groups.size());
    const auto table = rebuilt.size();
    rebuilt.append(QByteArray(groups.size() * 12, '\0'));
    QVector<quint32> limitsOffsets;
    for (const auto &g : groups) {
        limitsOffsets.append(rebuilt.size());
        putBE<quint16>(rebuilt, g.records.size() - 1);
        quint32 limit = 0;
        for (qsizetype i = 0; i < g.records.size(); ++i) {
            if (i)
                putBE<quint16>(rebuilt, g.records[i].id - g.base);
            limit += g.records[i].encoded.size();
            putBE<quint16>(rebuilt, limit);
        }
    }
    for (qsizetype i = 0; i < groups.size(); ++i) {
        qToBigEndian<quint32>(groups[i].base, rebuilt.data() + table + 12 * i);
        qToBigEndian<quint32>(rebuilt.size(), rebuilt.data() + table + 12 * i + 4);
        qToBigEndian<quint32>(limitsOffsets[i], rebuilt.data() + table + 12 * i + 8);
        rebuilt += groups[i].text;
    }
    auto payload = original.left(37);
    char size[4];
    qToLittleEndian<quint32>(rebuilt.size(), size);
    payload.append(size, 4);
    payload += rebuilt;
    const auto after = decodeLocalization(payload);
    if (after.entries.size() != before.entries.size())
        fail("Writer changed the string count");
    QMap<QString, QString> expected;
    for (const auto &e : before.entries) {
        const auto id = e.id.section(':', -1).toUInt();
        expected[e.id] = replacements.contains(id) ? replacements[id] : e.source;
    }
    for (const auto &e : after.entries)
        if (!expected.contains(e.id) || expected[e.id] != e.source)
            fail("Writer round-trip did not preserve an ID or text");
    return payload;
}
} // namespace anvil::breakpoint
