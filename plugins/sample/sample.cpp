#include <QObject>
#include <anvil/formatplugin.h>
#include <stdexcept>
class SamplePlugin final : public QObject, public anvil::FormatPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AnvilFormatPlugin_iid)
    Q_INTERFACES(anvil::FormatPlugin)
  public:
    QString formatId() const override { return "studio.synthetic-json.v1"; }
    QString displayName() const override { return "Synthetic sample — NOT an Anvil game format"; }
    bool recognizes(const QByteArray &prefix) const override {
        return prefix.startsWith("ANVIL_STUDIO_SAMPLE\n");
    }
    anvil::Document decode(const QByteArray &bytes) const override {
        if (!recognizes(bytes))
            throw std::runtime_error("Invalid synthetic sample signature");
        return anvil::fromJson(bytes.mid(20));
    }
};
#include "sample.moc"
