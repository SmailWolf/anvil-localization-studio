# Validation · 0.7.0-beta.2 · 2026-10-20

## Windows x64

Native build: Windows Server 2022 GitHub runner, MSYS2 UCRT64, GCC 16, Qt 6.12, Release.

[Successful build and tests](https://github.com/SmailWolf/anvil-localization-studio/actions/runs/39437800774) · binary source commit `d64c07fba3b4d3b65dba6adc5cabc6190d02f23d`.

- **15/15 CTest suites passed** on Windows: core/CSV/storage, readers/plugins, packaging, guards, model download fixtures, translation validation and editor UI.
- Deployed folder starts with only Windows system directories on PATH, with Qt SDK/MSYS2 plugin paths removed. Both interface languages, reader/packager help and CPU engine startup passed. Qt Windows/offscreen/TLS plugins and non-system DLL dependencies are bundled.
- The pinned 2,489,909,760-byte TranslateGemma GGUF was downloaded into temporary CI storage and its SHA-256 verified. Real CPU model and GUI preview/apply tests completed successfully, including placeholders, duplicate IDs, a preserved Reviewed translation and more than 500 records.
- Windows helper parser output is kept on redirected pipes rather than opening hidden native help/error message boxes. Deployment checks have explicit time bounds.
- The package targets Windows 10/11 x64; the CPU engine requires SSE4.2/BMI2. It is unsigned. No CUDA engine is included. MSVC is an alternative source build, not the verified packaged toolchain.

## Linux regression

15/15 local CTest suites passed after Windows changes. The separate Arch Linux GitHub workflow also passed. The earlier Linux binary release remains available as 0.7.0-beta.1.

## Limits and distribution

No installed game files were changed. The package contains no game archives, extracted game text database, completed game translation, proprietary game DLL, native game patch payload or model weights. Synthetic examples/tests remain included. Model weights used for CI are not shipped.

Automated Windows tests are not a complete human desktop or game test. Actual extraction with a user's proprietary Breakpoint DLL on a Windows game installation has not been exercised in CI. Newly packaged archives have **not been confirmed to run in either game**; UMAC is not recalculated and Ukrainian font coverage is not supplied. These limits remain part of this beta.
