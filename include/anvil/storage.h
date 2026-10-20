#pragma once
#include "document.h"
namespace anvil {
// Destination must be an app-owned project/export path, never a game asset.
void saveWithBackup(const QString &path, const QByteArray &bytes);
QByteArray readFile(const QString &path);
QString recoveryDirectory();
// Resolves symlinks and missing suffixes; throws before writing inside a selected game.
QString requireOutsideGame(const QString &path, const QString &gameRoot);
// Optional directory keeps tests and embedded callers isolated from user data.
QString writeRecovery(const Document &document, const QString &sessionId,
                      const QString &directory = recoveryDirectory());
} // namespace anvil
