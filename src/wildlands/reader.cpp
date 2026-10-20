#include <anvil/wildlands.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QtEndian>
#include <lzo/lzo1x.h>
#include <stdexcept>
namespace anvil::wildlands {
namespace {
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
class Cursor {
  public:
    const QByteArray &bytes;
    qsizetype position = 0;
    QByteArray take(qsizetype count) {
        if (count < 0 || count > bytes.size() - position)
            fail("Truncated Wildlands compression container");
        auto result = bytes.mid(position, count);
        position += count;
        return result;
    }
    template<class T> T read() {
        const auto b = take(sizeof(T));
        return qFromLittleEndian<T>(b.constData());
    }
};
quint32 adler0(const QByteArray &bytes) {
    quint32 a = 0, b = 0;
    for (unsigned char c : bytes) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
}
QVector<breakpoint::ArchiveEntry> englishIndex(QIODevice &input) {
    // Both observed games have the exact same 1050-byte v27 header and index rows.
    return breakpoint::englishIndex(input);
}
QPair<QByteArray, QByteArray> decodeContainer(const QByteArray &bytes) {
    if (bytes.size() > 16 * 1024 * 1024)
        fail("Wildlands stored resource exceeds 16 MiB");
    Cursor in{bytes};
    QPair<QByteArray, QByteArray> result;
    quint64 total = 0;
    for (int descriptor = 0; descriptor < 2; ++descriptor) {
        if (in.read<quint64>() != 0x1004fa9957fbaa33ULL || in.read<quint16>() != 1 ||
            in.read<quint8>() != 1 || in.read<quint16>() != 32768 || in.read<quint16>() != 32768)
            fail("Unverified Wildlands container (expected v1 / LZO / 32768)");
        const auto count = in.read<quint32>();
        if (count == 0 || count > 2048)
            fail("Wildlands compression block count exceeds limits");
        QVector<QPair<quint16, quint16>> sizes;
        for (quint32 i = 0; i < count; ++i) {
            const auto raw = in.read<quint16>(), stored = in.read<quint16>();
            if (raw == 0 || raw > 32768 || stored == 0 || stored > raw)
                fail("Invalid Wildlands compression block size");
            total += raw;
            if (total > 64 * 1024 * 1024)
                fail("Decoded Wildlands resource exceeds 64 MiB");
            sizes.append({raw, stored});
        }
        auto &output = descriptor == 0 ? result.first : result.second;
        for (const auto &[raw, stored] : sizes) {
            const auto checksum = in.read<quint32>();
            const auto block = in.take(stored);
            if (adler0(block) != checksum)
                fail("Wildlands compressed block checksum mismatch");
            if (raw == stored) {
                output += block;
            } else {
                QByteArray decoded(raw, '\0');
                lzo_uint length = raw;
                const auto status = lzo1x_decompress_safe(
                    reinterpret_cast<const unsigned char *>(block.constData()), stored,
                    reinterpret_cast<unsigned char *>(decoded.data()), &length, nullptr);
                if (status != LZO_E_OK || length != raw)
                    fail("Wildlands LZO block failed safe decompression or size check");
                output += decoded;
            }
        }
    }
    if (in.position != bytes.size())
        fail("Trailing Wildlands compression container bytes");
    return result;
}
Document decodeLocalization(const QByteArray &payload) {
    // Wildlands additionally has an observed empty EManual package (category 2).
    if (payload.size() == 41 && payload.mid(8) == QByteArray::fromHex(
            "6f9c3c6e01020000000100000000000000000000f800000000b58983d200000000")) {
        Document empty;
        empty.game = "wildlands";
        empty.sourceLanguage = "en-US";
        return empty;
    }
    // The nonempty resource wrapper and grammar match the verified shared variant.
    auto document = breakpoint::decodeLocalization(payload);
    document.game = "wildlands";
    for (auto &entry : document.entries) {
        entry.id.replace(0, 4, "grw:");
        entry.context.replace(0, 10, "Wildlands");
    }
    validate(document);
    return document;
}
Document extractEnglish(const QString &gameDirectory) {
    const auto root = QFileInfo(gameDirectory).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root + "/GRW.exe").isFile())
        fail("Select the Wildlands directory containing GRW.exe");
    struct Package { QString archive; breakpoint::ArchiveEntry entry; QByteArray bytes; };
    QMap<quint64, Package> packages;
    quint64 total = 0;
    for (const auto &name : {QString("DataPC.forge"), QString("DataPC_patch_01.forge")}) {
        QFile input(root + "/" + name);
        if (!input.open(QIODevice::ReadOnly))
            fail("Cannot open Wildlands archive for reading");
        const QFileInfo before(input.fileName());
        const auto entries = englishIndex(input);
        for (const auto &entry : entries) {
            if (entry.size > 16 * 1024 * 1024 || total + entry.size > 64 * 1024 * 1024)
                fail("Wildlands localization samples exceed limits");
            if (!input.seek(entry.offset))
                fail("Cannot seek Wildlands localization entry");
            const auto bytes = input.read(entry.size);
            if (bytes.size() != entry.size)
                fail("Short Wildlands localization read");
            total += entry.size;
            packages.insert(entry.id, {name, entry, bytes});
        }
        const QFileInfo after(input.fileName());
        if (before.size() != after.size() || before.lastModified() != after.lastModified())
            fail("Wildlands archive changed during extraction");
    }
    if (packages.isEmpty())
        fail("No English Wildlands localization packages found");
    Document document;
    document.game = "wildlands";
    document.sourceLanguage = "en-US";
    for (const auto &package : packages) {
        const auto decoded = decodeContainer(package.bytes);
        const auto payload = breakpoint::unwrapResource(decoded.first, decoded.second, package.entry.id,
                                                       package.entry.name);
        auto part = decodeLocalization(payload);
        for (auto &entry : part.entries)
            entry.context = package.archive + " | " + package.entry.name + " | " + entry.context;
        document.entries += part.entries;
    }
    validate(document);
    return document;
}
} // namespace anvil::wildlands
