#include <QObject>
#include <anvil/breakpoint.h>
#include <anvil/formatplugin.h>
class BreakpointPlugin final : public QObject, public anvil::FormatPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AnvilFormatPlugin_iid)
    Q_INTERFACES(anvil::FormatPlugin)
  public:
    QString formatId() const override { return "grb.localizationpayload.v1"; }
    QString displayName() const override { return "Breakpoint — decoded LocalizationPackage (read-only)"; }
    bool recognizes(const QByteArray &prefix) const override {
        return prefix.size() >= 41 && prefix.mid(8, 4) == QByteArray::fromHex("6f9c3c6e") &&
               prefix.mid(33, 4) == QByteArray::fromHex("b58983d2");
    }
    anvil::Document decode(const QByteArray &bytes) const override {
        return anvil::breakpoint::decodeLocalization(bytes);
    }
};
#include "breakpoint.moc"
