#include <QObject>
#include <anvil/wildlands.h>
#include <anvil/formatplugin.h>
class WildlandsPlugin final : public QObject, public anvil::FormatPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AnvilFormatPlugin_iid)
    Q_INTERFACES(anvil::FormatPlugin)
  public:
    QString formatId() const override { return "grw.localizationpayload.v1"; }
    QString displayName() const override { return "Wildlands — decoded LocalizationPackage (read-only)"; }
    bool recognizes(const QByteArray &prefix) const override {
        // A shared payload signature is NOT sufficient to autodetect which game it came from.
        return prefix.size() >= 41 && prefix.mid(8, 4) == QByteArray::fromHex("6f9c3c6e") &&
               prefix.mid(33, 4) == QByteArray::fromHex("b58983d2");
    }
    anvil::Document decode(const QByteArray &bytes) const override {
        return anvil::wildlands::decodeLocalization(bytes);
    }
};
#include "wildlands.moc"
