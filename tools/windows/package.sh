#!/usr/bin/env bash
# Run in the MSYS2 UCRT64 shell after native Windows builds and CTest.
set -euo pipefail
stage="dist/Anvil-Localization-Studio-0.7.0-beta.2-Windows-x64"
mkdir -p "$stage"/{bin,plugins,engine/cpu,docs,third-party}
for name in anvil-studio anvil-extract-wildlands anvil-extract-breakpoint anvil-pack-translation anvil-oodle-helper; do
    cp "build/$name.exe" "$stage/bin/"
done
find build/plugins -name '*.dll' -exec cp {} "$stage/plugins/" \;
cp -R docs/. "$stage/docs/"
cp COPYING LICENSE-NOTICE.txt "$stage/"
# Include the complete matching application source, without build output or game data.
git archive HEAD | tar -x -C "$(mkdir -p "$stage/source"; cd "$stage/source" && pwd)"
cp .build/llama-build/bin/llama-server.exe "$stage/engine/cpu/"
find .build/llama-build -name '*.dll' -exec cp {} "$stage/engine/cpu/" \;
cp .build/llama.cpp/LICENSE "$stage/third-party/llama.cpp-LICENSE"
printf '%s\n' 'llama.cpp CPU engine, MIT license' 'Source: https://github.com/ggml-org/llama.cpp/tree/bd4eeaa047006cb1fe71999fbd11134b5836e167' 'Built on Windows using MSYS2 UCRT64. No CUDA engine or model weights included.' > "$stage/third-party/llama.cpp-PROVENANCE.txt"
# Let Qt deploy its platform/image/TLS plugins and Qt dependencies.
deploy=$(command -v windeployqt6 || command -v windeployqt)
"$deploy" --release --compiler-runtime --dir "$stage/bin" "$stage/bin/anvil-studio.exe"
qtpaths=$(command -v qtpaths6 || command -v qtpaths)
qt_plugins=$("$qtpaths" --query QT_INSTALL_PLUGINS | tr -d '\r')
qt_plugins=$(cygpath -u "$qt_plugins")
mkdir -p "$stage/bin/tls"
cp "$qt_plugins/tls/qschannelbackend.dll" "$stage/bin/tls/"
# windeployqt normally ships qwindows only. Offscreen is required for the
# deployment smoke test; ship it explicitly rather than loading the SDK copy.
cp "$qt_plugins/platforms/qoffscreen.dll" "$stage/bin/platforms/"
# A relocatable Qt configuration prevents falling back to the build-machine paths.
printf '[Paths]\nPrefix=.\nPlugins=.\nTranslations=translations\n' > "$stage/bin/qt.conf"
# Recursively deploy non-system DLL imports for every executable and plugin.
# Each imported DLL is copied beside its importer, including each engine DLL.
python tools/windows/copy-runtime.py "$stage" /ucrt64/bin
mkdir -p "$stage/third-party/licenses"
cp -R /ucrt64/share/licenses/. "$stage/third-party/licenses/"
pacman -Q > "$stage/third-party/msys2-build-packages.txt"
cp tools/windows/start-studio.cmd "$stage/Start-Anvil-Studio.cmd"
cp tools/windows/README-WINDOWS.txt "$stage/README-WINDOWS.txt"
# Never redistribute owned game libraries, archives, databases or models.
if find "$stage" -type f \( -iname '*.forge' -o -iname '*.gguf' -o -iname 'oo2core*.dll' -o -iname '*.anvildelta' \) | grep -q .; then
    echo 'Forbidden game/model payload in package' >&2; exit 1
fi
printf '%s\n' "$stage" > dist/windows-package-path.txt
