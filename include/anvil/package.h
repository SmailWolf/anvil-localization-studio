#pragma once
#include "document.h"
#include <functional>
namespace anvil::packaging {
struct Options {
    QString gameDirectory, outputDirectory, helper, codecDll, winePrefix;
    bool reviewedOnly = false, materialize = true;
    std::function<void(const QString &)> progress;
};
// Always exports outside the selected game; never installs or overwrites game assets.
void pack(const Document &document, const Options &options);
void assemble(const QString &packageDirectory, const Options &options);
} // namespace anvil::packaging
