# Build · 0.7.0-beta.2

## Arch Linux

Install the build dependencies:

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools lzo wine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/anvil-studio
```

Qt 6.5+, C++20 and CMake 3.21+ are required. Qt LinguistTools compiles the English translation into the executable. Qt Network needs a working TLS backend and trusted CA certificates for model downloads. Tests use a loopback HTTP server and require permission to open local sockets.

For Breakpoint on Linux, the Windows decompression helper must also be built. Install `mingw-w64-gcc` from an appropriate Arch repository/AUR provider before configuring, or build `tools/oodle_helper.cpp` separately using a MinGW x86_64 compiler. CMake detects `x86_64-w64-mingw32-g++` and builds the helper automatically. Wine runs it using the user's existing `oo2core_7_win64.dll` from the game directory; the DLL is not included. Wildlands uses LZO2 and does not require this DLL.

## Local translation engine

The source tree does not contain llama.cpp binaries. Build a compatible llama.cpp version that supports TranslateGemma's Jinja chat template, then select its `llama-server` executable under Translation. The Linux binary package contains separate CPU/CUDA engine directories and their license notices. The model can be downloaded from the app or selected as an existing GGUF. No online translation API is used.

Previously verified CPU engine source revision: `bd4eeaa047006cb1fe71999fbd11134b5836e167` from [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp). When distributing engine binaries, retain the matching license and corresponding source reference.

## Windows (source build; not runtime verified)

Use Visual Studio 2022 with C++20, CMake, Qt 6.5+ MSVC x64 (including Network, Test, LinguistTools) and an x64 LZO2 installation. Use matching compiler architectures throughout.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64" -DLZO_INCLUDE_DIR="C:/deps/lzo/include" -DLZO_LIBRARY="C:/deps/lzo/lib/lzo2.lib"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
windeployqt build/Release/anvil-studio.exe
```

Deploy the extractor/packager/helper executables and plugins beside the application as required by the UI. A Windows-compatible llama-server and its libraries are needed for inference. A Windows release package has not been tested; the Linux ZIP cannot run on Windows.

## Interface and command-line options

- `--ui-language uk` or `--ui-language en`: override the stored interface preference for this launch.
- `--game wildlands` or `--game breakpoint`: choose a profile.
- `--open /absolute/project.als.json`: open a project.
- `--smoke-test`: briefly start and exit; for headless tests use `QT_QPA_PLATFORM=offscreen`.

The GUI language setting is applied on the next launch. Translating the interface does not translate user project content.
