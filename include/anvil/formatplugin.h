#pragma once
#include "document.h"
#include <QtPlugin>
namespace anvil {
// Versioned read-only parsing contract. No filenames, write APIs or game discovery.
// Native plugins are trusted executable code, NOT sandboxed by this interface.
class FormatPlugin {
  public:
    virtual ~FormatPlugin() = default;
    virtual QString formatId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool recognizes(const QByteArray &prefix) const = 0;
    virtual Document decode(const QByteArray &bytes) const = 0;
};
} // namespace anvil
#define AnvilFormatPlugin_iid "org.anvil-localization-studio.FormatPlugin/1.0"
Q_DECLARE_INTERFACE(anvil::FormatPlugin, AnvilFormatPlugin_iid)
