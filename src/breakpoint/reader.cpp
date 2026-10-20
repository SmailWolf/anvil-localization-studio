#include <QSet>
#include <QStringConverter>
#include <QtEndian>
#include <algorithm>
#include <anvil/breakpoint.h>
#include <functional>
#include <limits>
#include <stdexcept>
namespace anvil::breakpoint {
namespace {
[[noreturn]] void fail(const char *text) { throw std::runtime_error(text); }
void range(qint64 size, qint64 offset, qint64 count) {
    if (offset < 0 || count < 0 || offset > size || count > size - offset)
        fail("Truncated or out-of-range Breakpoint data");
}
class Bytes {
  public:
    const QByteArray &b;
    explicit Bytes(const QByteArray &bytes) : b(bytes) {}
    template <class T> T le(qint64 off) const {
        range(b.size(), off, sizeof(T));
        quint64 value = 0;
        for (qsizetype i = 0; i < qsizetype(sizeof(T)); ++i)
            value |= quint64(quint8(b.at(off + i))) << (8 * i);
        return static_cast<T>(value);
    }
    template <class T> T be(qint64 off) const {
        range(b.size(), off, sizeof(T));
        quint64 value = 0;
        for (qsizetype i = 0; i < qsizetype(sizeof(T)); ++i)
            value = (value << 8) | quint8(b.at(off + i));
        return static_cast<T>(value);
    }
    QByteArray slice(qint64 off, qint64 count) const {
        range(b.size(), off, count);
        return b.mid(off, count);
    }
};
quint32 adler0(const QByteArray &bytes) {
    quint32 a = 0, b = 0;
    for (unsigned char c : bytes) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}
} // namespace
QVector<ArchiveEntry> englishIndex(QIODevice &input) {
    if (!input.isOpen() || !input.isReadable() || input.isSequential())
        fail("Archive must be open for random-access reading");
    const auto size = input.size();
    auto read = [&](qint64 off, qint64 count) {
        range(size, off, count);
        if (!input.seek(off))
            fail("Archive seek failed");
        const auto b = input.read(count);
        if (b.size() != count)
            fail("Archive read failed");
        return b;
    };
    auto header = read(0, 21);
    Bytes h(header);
    if (header.left(9) != QByteArray("scimitar\0", 9) || h.le<quint32>(9) != 27 || h.le<quint64>(13) != 1050)
        fail("Only the observed Breakpoint v27 archive layout is supported");
    auto info = read(1050, 44);
    Bytes hi(info);
    const auto expected = hi.le<quint32>(0), sets = hi.le<quint32>(32);
    auto position = hi.le<qint64>(36);
    if (expected > 2000000 || sets > 10000)
        fail("Archive index exceeds supported limits");
    QSet<qint64> visited;
    QSet<quint64> ids;
    quint64 total = 0;
    QVector<ArchiveEntry> result;
    for (quint32 s = 0; s < sets; ++s) {
        if (visited.contains(position))
            fail("Cyclic FileSet list");
        visited.insert(position);
        const auto set = read(position, 40);
        Bytes sh(set);
        const auto count = sh.le<quint32>(0);
        if (count > expected || total + count > expected)
            fail("FileSet count mismatch");
        const auto offsets = read(sh.le<qint64>(8), qint64(count) * 20);
        const auto metadata = read(sh.le<qint64>(32), qint64(count) * 192);
        Bytes ob(offsets), mb(metadata);
        for (quint32 i = 0; i < count; ++i) {
            const qint64 o = qint64(i) * 20, m = qint64(i) * 192;
            const auto offset = ob.le<qint64>(o);
            const auto id = ob.le<quint64>(o + 8);
            const auto length = ob.le<qint32>(o + 16);
            range(size, offset, length);
            const auto field = mb.slice(m + 44, 128);
            const auto zero = field.indexOf('\0');
            const QString name = QString::fromLatin1(zero < 0 ? field : field.left(zero));
            if (mb.le<quint32>(m + 16) == 0x6e3c9c6f && name.startsWith("LocalizationPackage_English(US)")) {
                if (ids.contains(id))
                    fail("Duplicate localization package ID inside an archive");
                ids.insert(id);
                result.append({id, name, offset, length});
            }
        }
        total += count;
        position = sh.le<qint64>(16);
    }
    if (total != expected || position != -1)
        fail("Incomplete archive index");
    return result;
}
Container parseContainer(const QByteArray &bytes) {
    Bytes in(bytes);
    qint64 p = 0;
    Container result;
    quint64 rawTotal = 0;
    for (int descriptor = 0; descriptor < 2; ++descriptor) {
        if (in.le<quint64>(p) != 0x1004fa9957fbaa33ULL || in.le<quint16>(p + 8) != 3 ||
            in.le<quint8>(p + 10) != 3)
            fail("Unsupported compression container (expected v3 / algorithm 3)");
        if (in.le<quint16>(p + 11) != 32768 || in.le<quint16>(p + 13) != 32768)
            fail("Unverified compression block size");
        const auto count = in.le<quint32>(p + 15);
        p += 19;
        if (count == 0 || count > 8192)
            fail("Invalid compression block count");
        QVector<QPair<quint32, quint32>> sizes;
        for (quint32 i = 0; i < count; ++i) {
            const auto raw = in.le<quint32>(p), stored = in.le<quint32>(p + 4);
            p += 8;
            if (raw == 0 || raw > 32768 || stored == 0 || stored > 65536)
                fail("Invalid compression block lengths");
            rawTotal += raw;
            if (rawTotal > 64 * 1024 * 1024)
                fail("Decoded container exceeds 64 MiB");
            sizes.append({raw, stored});
        }
        for (const auto &sizes : sizes) {
            const auto checksum = in.le<quint32>(p);
            p += 4;
            const auto block = in.slice(p, sizes.second);
            p += sizes.second;
            if (adler0(block) != checksum)
                fail("Compressed block checksum mismatch");
            result.blocks.append({sizes.first, block});
        }
        if (descriptor == 0)
            result.metadataBlocks = result.blocks.size();
    }
    if (p != bytes.size())
        fail("Trailing compression container bytes");
    return result;
}
QByteArray unwrapResource(const QByteArray &metadata, const QByteArray &files, quint64 expectedId,
                          const QString &expectedName) {
    Bytes m(metadata), f(files);
    if (metadata.size() != 16 || m.le<quint16>(0) != 1 || m.le<quint64>(2) != expectedId ||
        m.le<quint32>(10) != quint32(files.size()) || m.le<quint16>(14) != 0)
        fail("Unverified resource index");
    if (f.le<quint32>(0) != 0x6e3c9c6f)
        fail("Expected LocalizationPackage resource");
    const auto length = f.le<qint32>(4), nameLength = f.le<qint32>(8);
    if (length < 0 || nameLength < 0 || nameLength > 4096)
        fail("Invalid resource lengths");
    const auto name = QString::fromLatin1(f.slice(12, nameLength));
    if (name != expectedName || f.le<quint8>(12 + nameLength) != 0)
        fail("Unverified resource name or FileHeader");
    const auto p = 13 + qint64(nameLength);
    if (p + length != files.size())
        fail("Incomplete resource walk or trailing resources");
    const auto payload = f.slice(p, length);
    Bytes b(payload);
    if (b.le<quint64>(0) != expectedId)
        fail("Archive and resource IDs differ");
    return payload;
}
Document decodeLocalization(const QByteArray &payload) {
    Bytes wrapper(payload);
    const auto fields = wrapper.slice(12, 25);
    if (wrapper.le<quint32>(8) != 0x6e3c9c6f ||
        (fields != QByteArray::fromHex("01000000000100000000000000000000f800000000b58983d2") &&
         fields != QByteArray::fromHex("01010000000100000000000000000000f800000000b58983d2")))
        fail("Unverified Breakpoint LocalizationPackage wrapper");
    const auto classId = wrapper.le<quint64>(0);
    const auto size = wrapper.le<quint32>(37);
    if (payload.size() != 41 + qint64(size))
        fail("Localization payload size mismatch");
    Document doc;
    doc.game = "breakpoint";
    doc.sourceLanguage = "en-US";
    if (size == 0)
        return doc;
    const auto data = wrapper.slice(41, size);
    Bytes in(data);
    const auto threshold = in.be<quint16>(0), nodes = in.be<quint16>(2);
    if (threshold < 1 || threshold > 255 || nodes < 1)
        fail("Invalid grammar dictionary");
    QVector<QPair<quint16, quint16>> grammar;
    for (quint32 i = 0; i < nodes; ++i)
        grammar.append({in.be<quint16>(4 + 4 * i), in.be<quint16>(6 + 4 * i)});
    qint64 p = 4 + 4 * qint64(nodes);
    const auto blocks = in.be<quint16>(p);
    p += 2;
    const qint64 directoryEnd = p + 12 * qint64(blocks);
    range(data.size(), p, 12 * qint64(blocks));
    QVector<QString> cache(nodes);
    QVector<int> state(nodes, 0);
    qsizetype cacheUnits = 0;
    std::function<QString(quint32, int)> expand = [&](quint32 i, int depth) -> QString {
        if (i >= nodes || depth > 400 || state[i] == 1)
            fail("Invalid, cyclic or overly deep grammar");
        if (state[i] == 2)
            return cache[i];
        state[i] = 1;
        const auto [a, b] = grammar[i];
        QString result;
        if (b == 0) {
            if (a)
                result.append(QChar(a));
        } else
            result = expand(b, depth + 1) + expand(a, depth + 1);
        cacheUnits += result.size();
        if (result.size() > 1024 * 1024 || cacheUnits > 16 * 1024 * 1024)
            fail("Grammar expansion limit exceeded");
        state[i] = 2;
        return cache[i] = result;
    };
    QVector<QPair<qint64, qint64>> ranges;
    QSet<quint64> ids;
    qsizetype textUnits = 0;
    for (quint32 block = 0; block < blocks; ++block) {
        const auto key = in.be<quint32>(p), chars = in.be<quint32>(p + 4), limits = in.be<quint32>(p + 8);
        p += 12;
        const auto count = in.be<quint16>(limits);
        QVector<quint16> deltas{0}, ends{in.be<quint16>(qint64(limits) + 2)};
        const qint64 limitsEnd = qint64(limits) + 4 + 4 * qint64(count);
        range(data.size(), limits, limitsEnd - limits);
        for (quint32 j = 0; j < count; ++j) {
            deltas.append(in.be<quint16>(qint64(limits) + 4 + 4 * j));
            ends.append(in.be<quint16>(qint64(limits) + 6 + 4 * j));
        }
        if (chars < directoryEnd || limits < directoryEnd)
            fail("Text block overlaps directory");
        quint32 start = 0;
        for (qsizetype j = 0; j < ends.size(); ++j) {
            const auto limit = ends[j];
            if (limit < start)
                fail("Unsorted string boundaries");
            auto encoded = in.slice(qint64(chars) + start, limit - start);
            qsizetype at = 0;
            QString text;
            while (at < encoded.size()) {
                const auto byte = quint8(encoded[at++]);
                quint32 code;
                if (byte < threshold)
                    code = byte;
                else if (byte == 255) {
                    if (encoded.size() - at < 2)
                        fail("Truncated long grammar symbol");
                    code = qFromBigEndian<quint16>(encoded.constData() + at);
                    at += 2;
                } else {
                    if (at >= encoded.size())
                        fail("Truncated grammar symbol");
                    const quint32 value = (quint32(byte) << 8) | quint8(encoded[at++]);
                    if (value < quint32(threshold) * 255)
                        fail("Invalid grammar code");
                    code = value - quint32(threshold) * 255;
                }
                text += expand(code + 1, 0);
                if (text.size() > 1024 * 1024)
                    fail("String expansion limit exceeded");
            }
            if (!text.isValidUtf16())
                fail("Invalid UTF-16 in grammar output");
            const auto id = quint64(key) + deltas[j];
            if (id > std::numeric_limits<quint32>::max() || ids.contains(id))
                fail("Invalid or duplicate string ID");
            ids.insert(id);
            textUnits += text.size();
            if (textUnits > 32 * 1024 * 1024 || doc.entries.size() >= 1000000)
                fail("Localization expansion limit exceeded");
            const auto idText = QString::number(id);
            doc.entries.append({QString("grb:%1:%2").arg(classId).arg(id),
                                text,
                                {},
                                QString("Breakpoint | package=%1 | string=%2 | block=%3 | delta=%4")
                                    .arg(classId)
                                    .arg(idText)
                                    .arg(key)
                                    .arg(deltas[j]),
                                "todo"});
            start = limit;
        }
        ranges.append({chars, qint64(chars) + ends.back()});
        ranges.append({limits, limitsEnd});
    }
    std::sort(ranges.begin(), ranges.end());
    qint64 cursor = directoryEnd;
    for (const auto &r : ranges) {
        if (r.first != cursor)
            fail("Overlapping or unaccounted localization bytes");
        cursor = r.second;
    }
    if (cursor != data.size())
        fail("Trailing localization bytes");
    validate(doc);
    return doc;
}
} // namespace anvil::breakpoint
