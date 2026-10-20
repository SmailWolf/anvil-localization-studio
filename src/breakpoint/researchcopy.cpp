// Offline experiment only: two copies outside the game, never an installer.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <anvil/breakpoint.h>
#include <stdexcept>

namespace {
constexpr quint64 UiId = 715061952218ULL;
constexpr qint64 Chunk = 2 * 1024 * 1024;
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
template <class T> T integer(const QByteArray &b, qint64 p) {
    if (p < 0 || p > b.size() || qint64(sizeof(T)) > b.size() - p)
        fail("Truncated integer");
    quint64 v = 0;
    for (qsizetype i = 0; i < qsizetype(sizeof(T)); ++i)
        v |= quint64(quint8(b.at(p + i))) << (8 * i);
    return static_cast<T>(v);
}
struct Span { qint64 offset, length; };
bool overlap(Span a, Span b) {
    return a.length && b.length && a.offset < b.offset + b.length && b.offset < a.offset + a.length;
}
QByteArray read(QFile &file, qint64 offset, qint64 count) {
    if (offset < 0 || count < 0 || offset > file.size() || count > file.size() - offset ||
        !file.seek(offset))
        fail("Invalid archive range");
    auto b = file.read(count);
    if (b.size() != count)
        fail("Short archive read");
    return b;
}
void outside(const QString &path, const QString &game) {
    QFileInfo item(QFileInfo(path).absoluteFilePath());
    QString tail;
    while (!item.exists()) {
        tail = "/" + item.fileName() + tail;
        const auto parent = item.absolutePath();
        if (parent == item.absoluteFilePath())
            fail("Cannot resolve output path");
        item.setFile(parent);
    }
    const auto resolved = QDir::cleanPath(item.canonicalFilePath() + tail);
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    if (resolved.compare(game, cs) == 0 || resolved.startsWith(game + "/", cs))
        fail("Refusing writes inside the game directory");
}
struct Index {
    quint32 count;
    Span ui;
    qint64 offsetLength, infoLength;
    QByteArray opaqueHash;
};
Index inspect(QFile &f) {
    const auto signature = read(f, 0, 21);
    if (signature.left(9) != QByteArray("scimitar\0", 9) || integer<quint32>(signature, 9) != 27 ||
        integer<quint64>(signature, 13) != 1050)
        fail("Only the observed v27 layout is accepted");
    const auto root = read(f, 1050, 44);
    Index result{};
    result.count = integer<quint32>(root, 0);
    const auto sets = integer<quint32>(root, 32);
    auto next = integer<qint64>(root, 36);
    if (!result.count || result.count > 2000000 || !sets || sets > 10000 ||
        integer<qint32>(root, 20) != -1 || integer<qint32>(root, 24) != -1)
        fail("Unverified root index");
    QSet<qint64> visited;
    QSet<quint64> ids;
    QVector<Span> tables{{0, 1094}}, data;
    quint64 total = 0;
    bool found = false;
    for (quint32 s = 0; s < sets; ++s) {
        if (visited.contains(next))
            fail("Cyclic FileSet");
        visited.insert(next);
        tables.append({next, 48});
        const auto h = read(f, next, 48);
        const auto n = integer<quint32>(h, 0);
        if (total + n > result.count)
            fail("Invalid index count");
        const auto offsets = integer<qint64>(h, 8), info = integer<qint64>(h, 32);
        const auto rows = read(f, offsets, qint64(n) * 20), metas = read(f, info, qint64(n) * 192);
        tables.append({offsets, qint64(n) * 20});
        tables.append({info, qint64(n) * 192});
        for (quint32 i = 0; i < n; ++i) {
            const auto at = qint64(i) * 20, m = qint64(i) * 192;
            const auto id = integer<quint64>(rows, at + 8);
            const auto start = integer<qint64>(rows, at);
            const auto length = integer<qint32>(rows, at + 16);
            if (ids.contains(id) || start < 0 || length <= 0 || start > f.size() ||
                length > f.size() - start || integer<qint32>(metas, m) != length)
                fail("Duplicate ID, invalid range or inconsistent lengths");
            ids.insert(id);
            data.append({start, length});
            if (id == UiId) {
                auto name = metas.mid(m + 44, 128);
                name = name.left(name.indexOf('\0') < 0 ? name.size() : name.indexOf('\0'));
                if (name != "LocalizationPackage_English(US)" ||
                    integer<quint32>(metas, m + 16) != 0x6e3c9c6f)
                    fail("Unexpected UI entry type or name");
                result.ui = {start, length};
                result.offsetLength = offsets + at + 16;
                result.infoLength = info + m;
                result.opaqueHash = metas.mid(m + 4, 8);
                found = true;
            }
        }
        total += n;
        next = integer<qint64>(h, 16);
    }
    if (total != result.count || next != -1 || !found || !ids.contains(16) || !ids.contains(145))
        fail("Incomplete index or missing sidecars");
    tables.erase(std::remove_if(tables.begin(), tables.end(), [](Span span) { return !span.length; }),
                 tables.end());
    std::sort(tables.begin(), tables.end(), [](Span a, Span b) { return a.offset < b.offset; });
    std::sort(data.begin(), data.end(), [](Span a, Span b) { return a.offset < b.offset; });
    for (qsizetype i = 1; i < tables.size(); ++i)
        if (overlap(tables[i - 1], tables[i]))
            fail("Overlapping index tables");
    for (qsizetype i = 1; i < data.size(); ++i)
        if (overlap(data[i - 1], data[i]))
            fail("Overlapping archive entries");
    for (const auto span : data)
        for (const auto table : tables)
            if (overlap(span, table))
                fail("Data overlaps an index table");
    return result;
}
QString digest(QFile &f) {
    if (!f.seek(0))
        fail("Hash seek failed");
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const auto bytes = f.read(Chunk);
        if (bytes.isEmpty())
            fail("Hash read failed");
        h.addData(bytes);
    }
    return QString::fromLatin1(h.result().toHex());
}
void writeAt(QFile &f, qint64 offset, const QByteArray &b) {
    if (!f.seek(offset) || f.write(b) != b.size())
        fail("Copy patch write failed");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("anvil-research-breakpoint-copy");
    QCommandLineParser args;
    args.addHelpOption();
    args.setApplicationDescription("OFFLINE RESEARCH ONLY. Creates .forge.research copies, never installs. "
                                   "UMAC is preserved but not recomputed; output is not game-validated.");
    args.addOptions({{"game-dir", "Read-only Breakpoint directory", "path"},
                     {"replacement", "Previously validated UI .data container", "path"},
                     {"output-dir", "New research directory outside game", "path"}});
    args.process(app);
    try {
        const auto game = QFileInfo(args.value("game-dir")).canonicalFilePath();
        if (game.isEmpty() || !QFileInfo(game).isDir())
            fail("Specify --game-dir");
        if (args.value("output-dir").isEmpty())
            fail("Specify --output-dir");
        const auto output = QFileInfo(args.value("output-dir")).absoluteFilePath();
        outside(output, game);
        if (QFileInfo::exists(output))
            fail("Output directory already exists");
        const QFileInfo outInfo(output);
        if (!QFileInfo(outInfo.absolutePath()).isDir())
            fail("Output parent must exist");
        QFile replacementFile(args.value("replacement"));
        if (!replacementFile.open(QIODevice::ReadOnly) || replacementFile.size() > 4 * 1024 * 1024)
            fail("Specify a UI replacement smaller than 4 MiB");
        const auto replacement = replacementFile.readAll();
        const auto container = anvil::breakpoint::parseContainer(replacement);
        for (const auto &block : container.blocks)
            if (block.bytes.size() > block.rawSize)
                fail("Replacement contains an expanded compressed block");
        // The observed UI metadata stream is a 16-byte raw resource index. Check
        // its ID before accepting an externally prepared compressed container.
        if (container.metadataBlocks != 1 || container.blocks[0].rawSize != 16 ||
            container.blocks[0].bytes.size() != 16 ||
            integer<quint16>(container.blocks[0].bytes, 0) != 1 ||
            integer<quint64>(container.blocks[0].bytes, 2) != UiId)
            fail("Replacement does not identify the expected UI package");
        quint64 fileBytes = 0;
        for (qsizetype i = container.metadataBlocks; i < container.blocks.size(); ++i)
            fileBytes += container.blocks[i].rawSize;
        if (integer<quint32>(container.blocks[0].bytes, 10) != fileBytes ||
            integer<quint16>(container.blocks[0].bytes, 14) != 0)
            fail("Replacement resource index length mismatch");
        QFile source(game + "/DataPC_patch_01.forge");
        if (!source.open(QIODevice::ReadOnly))
            fail("Cannot open original archive read-only");
        const QFileInfo before(source);
        const auto beforeSize = before.size();
        const auto beforeTime = before.lastModified();
        const auto index = inspect(source);
        if (replacement.size() > index.ui.length)
            fail("Replacement exceeds original slot; no archive reflow is implemented");
        if (!QDir(outInfo.absolutePath()).mkdir(outInfo.fileName()))
            fail("Cannot exclusively create output directory");
        QFile state(output + "/DO-NOT-INSTALL.txt");
        if (!state.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            state.write("OFFLINE RESEARCH. Not a working patch. UMAC is not recomputed. "
                        "Do not rename or install these files. See report.json.\n") < 0)
            fail("Cannot write research notice");
        state.close();
        QFile control(output + "/control.forge.research"), changed(output + "/modified.forge.research");
        if (!control.open(QIODevice::ReadWrite | QIODevice::NewOnly) ||
            !changed.open(QIODevice::ReadWrite | QIODevice::NewOnly) || !source.seek(0))
            fail("Cannot exclusively create archive copies");
        QTextStream(stdout) << "Copying original archive into two offline research files...\n" << Qt::flush;
        QCryptographicHash sourceHash(QCryptographicHash::Sha256);
        while (!source.atEnd()) {
            const auto b = source.read(Chunk);
            if (b.isEmpty() || control.write(b) != b.size() || changed.write(b) != b.size())
                fail("Copy read/write failed");
            sourceHash.addData(b);
        }
        const auto originalHash = QString::fromLatin1(sourceHash.result().toHex());
        if (!control.flush() || !changed.flush() || digest(control) != originalHash)
            fail("Control copy SHA-256 verification failed");
        QByteArray length(4, '\0');
        qToLittleEndian<qint32>(replacement.size(), length.data());
        writeAt(changed, index.offsetLength, length);
        writeAt(changed, index.infoLength, length);
        writeAt(changed, index.ui.offset, replacement);
        qint64 padding = index.ui.length - replacement.size();
        for (qint64 at = index.ui.offset + replacement.size(); padding;) {
            const auto n = std::min(padding, Chunk);
            writeAt(changed, at, QByteArray(n, '\0'));
            at += n;
            padding -= n;
        }
        if (!changed.flush() || changed.size() != control.size())
            fail("Modified copy size/flush failed");
        const auto updated = inspect(changed);
        if (updated.count != index.count || updated.ui.offset != index.ui.offset ||
            updated.ui.length != replacement.size() || updated.opaqueHash != index.opaqueHash ||
            read(changed, index.ui.offset, replacement.size()) != replacement)
            fail("Modified copy index verification failed");
        // Compare every byte to its expected value, including all entries, gaps,
        // tables and sidecars; only these three spans may differ from control.
        const QVector<Span> allowed{{index.offsetLength, 4}, {index.infoLength, 4}, index.ui};
        QCryptographicHash modifiedHash(QCryptographicHash::Sha256);
        qint64 differingBytes = 0;
        QTextStream(stdout) << "Comparing every byte and rehashing the original...\n" << Qt::flush;
        for (qint64 at = 0; at < control.size(); at += Chunk) {
            const auto n = std::min(Chunk, control.size() - at);
            auto expected = read(control, at, n);
            const auto actual = read(changed, at, n);
            for (qsizetype s = 0; s < allowed.size(); ++s) {
                const auto span = allowed[s];
                const auto start = std::max(at, span.offset), end = std::min(at + n, span.offset + span.length);
                if (start >= end)
                    continue;
                for (qint64 p = start; p < end; ++p) {
                    const auto relative = p - span.offset;
                    expected[p - at] = s < 2 ? length.at(relative)
                        : relative < replacement.size() ? replacement.at(relative) : '\0';
                }
            }
            if (actual != expected)
                fail("Unexpected byte changes outside the permitted edit");
            const auto original = read(control, at, n);
            for (qsizetype i = 0; i < n; ++i)
                differingBytes += actual.at(i) != original.at(i);
            modifiedHash.addData(actual);
        }
        if (digest(source) != originalHash || QFileInfo(source).size() != beforeSize ||
            QFileInfo(source).lastModified() != beforeTime)
            fail("Source changed during experiment");
        const QJsonObject report{
            {"status", "offline-research-verified-not-for-installation"},
            {"sourcePath", before.canonicalFilePath()}, {"archiveBytes", control.size()},
            {"sourceSha256BeforeAndAfter", originalHash}, {"controlSha256", digest(control)},
            {"modifiedSha256", QString::fromLatin1(modifiedHash.result().toHex())},
            {"entryCount", qint64(index.count)}, {"unchangedEntryCount", qint64(index.count - 1)},
            {"packageId", QString::number(UiId)}, {"packageOffset", index.ui.offset},
            {"originalPackageBytes", index.ui.length}, {"replacementPackageBytes", replacement.size()},
            {"zeroPaddingBytes", index.ui.length - replacement.size()},
            {"offsetTableLengthField", index.offsetLength}, {"infoTableLengthField", index.infoLength},
            {"differingByteCount", differingBytes}, {"allBytesMatchExpected", true},
            {"rootAndFileSetHeadersUnchanged", true}, {"allEntryOffsetsUnchanged", true},
            {"sidecarsUnchanged", true}, {"opaqueHashBytesPreserved", QString::fromLatin1(index.opaqueHash.toHex())},
            {"umacRecomputed", false}, {"opaqueHashValidityForReplacement", "unverified"},
            {"payloadDecompressionVerifiedByThisTool", false},
            {"gameFilesWritten", false}, {"inGameTested", false}, {"doNotInstall", true}};
        QFile reportFile(output + "/report.json");
        const auto json = QJsonDocument(report).toJson();
        if (!reportFile.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            reportFile.write(json) != json.size() || !reportFile.flush())
            fail("Cannot write verification report");
        QTextStream(stdout) << "Offline verification passed: " << index.count - 1
                            << " other entries preserved. UMAC unresolved; DO NOT INSTALL.\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "Research copy failed: " << e.what() << "\n";
        return 1;
    }
}
