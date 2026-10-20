#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace anvil::fonts {
QString bundledFont();
QString requiredCharacters(const QString &language = "both");
QJsonObject inspect(const QString &font, const QString &language = "both");
// The observed raw GFOF v3.3 layout only; rejects wrappers/archives/other versions.
QJsonObject inspectGfof(const QByteArray &blob, const QString &language = "both");
QByteArray compileGfof(const QString &font, const QByteArray &donor,
                      const QString &language = "both", int pixelSize = 50, int spread = 5);
// New directory only, never an installed game. Empty donor exports a portable font kit.
void exportKit(const QString &font, const QString &output, const QString &language = "both",
               const QString &donor = {}, int pixelSize = 50, int spread = 5);
}
