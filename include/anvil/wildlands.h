#pragma once
#include "breakpoint.h"
namespace anvil::wildlands {
// Read-only support for the observed Wildlands v27 / CFD v1 LZO containers.
QVector<breakpoint::ArchiveEntry> englishIndex(QIODevice &input);
QPair<QByteArray, QByteArray> decodeContainer(const QByteArray &bytes);
Document decodeLocalization(const QByteArray &payload);
Document extractEnglish(const QString &gameDirectory);
} // namespace anvil::wildlands
