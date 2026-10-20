# Windows x64 · 0.7.0-beta.2

The Windows package is built natively on GitHub Actions using Windows Server 2022, MSYS2 UCRT64, Qt 6 and GCC. It targets Windows 10/11 x64. The package contains the application, readers, packager, decompression helper, Qt platform/TLS plugins, LZO2 and compiler runtime DLLs. No separate Qt SDK, MSYS2 or Wine installation is needed to run it.

## Run

Extract the entire ZIP outside the game installation. Double-click **Start-Anvil-Studio.cmd** or `bin/anvil-studio.exe`. Keep all bundled directories. The app is unsigned.

Choose **Мова / Language → Українська / English**, save and restart. Open **Translation → Download model…** to obtain the 2.49 GB TranslateGemma model. This Windows package includes a **CPU engine**, not CUDA. Model weights and completed game translations are not included.

Breakpoint uses your own `oo2core_7_win64.dll` from the selected game folder. The helper invokes a copy of that library natively; Wine is not used. Installed game files remain unchanged. Native game runtime compatibility of generated packages remains unverified.

## Build from source

In an updated MSYS2 **UCRT64** shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-qt6-translations mingw-w64-ucrt-x86_64-lzo2 mingw-w64-ucrt-x86_64-python git zip
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

The checked-in `.github/workflows/windows.yml` also builds the pinned llama.cpp CPU engine, deploys runtime dependencies with Qt's `windeployqt`, recursively copies non-system DLL imports, and creates a ZIP. The deployed GUI/readers/packager/engine are launched with a clean PATH, without MSYS2. Tests use synthetic data. The model inference test downloads the pinned model into the temporary CI workspace and verifies SHA-256; weights are excluded from artifacts.

The application has Windows version metadata and starts without a console window. The launcher preserves the complete extracted folder layout. Project/cache/settings/model directories use Qt's Windows standard user locations.

## Validation scope

The Windows build, automated test and deployment status is recorded in the release notes and linked GitHub Actions run. A successful build does not establish game compatibility or complete human desktop QA. CUDA is not supplied or verified in this Windows beta.

See [English guide](USER-GUIDE.en.md), [українська інструкція](USER-GUIDE.md), and [packaging limits](PACKAGING.md).
