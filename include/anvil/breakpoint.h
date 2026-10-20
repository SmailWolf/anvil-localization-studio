#pragma once
#include "document.h"
#include <QIODevice>
#include <QMap>
namespace anvil::breakpoint {
struct ArchiveEntry {
    quint64 id;
    QString name;
    qint64 offset, size;
};
struct Block {
    quint32 rawSize;
    QByteArray bytes;
};
struct Container {
    QVector<Block> blocks;
    qsizetype metadataBlocks;
};
// Narrow, read-only readers for the observed Breakpoint Steam build 25349072.
// They reject unverified versions, invalid ranges, partial walks and trailing bytes.
QVector<ArchiveEntry> englishIndex(QIODevice &input);
Container parseContainer(const QByteArray &bytes);
QByteArray unwrapResource(const QByteArray &metadata, const QByteArray &files, quint64 expectedId,
                          const QString &expectedName);
Document decodeLocalization(const QByteArray &payload);
// Experimental resource writer: preserves IDs and original encoded strings except replacements.
// Always decodes the result and verifies every row before returning. No file writes.
QByteArray replaceLocalization(const QByteArray &original, const QMap<quint32, QString> &replacements);
} // namespace anvil::breakpoint
