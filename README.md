# Anvil Localization Studio · 0.8.0-beta.1

**Beta software.** C++20 / Qt 6 desktop localization editor for **Ghost Recon Wildlands** and **Ghost Recon Breakpoint**. Ukrainian and English interface. Local English → Ukrainian / Belarusian translation with TranslateGemma 4B, plus a font kit covering both alphabets.

**No game files, extracted game text, completed game translations, patches, or model weights are included.** The user supplies a legally obtained game installation. Test fixtures and examples contain synthetic data only. Extraction reads game archives; packaging writes separate output outside the installation.

[Українська інструкція](docs/USER-GUIDE.md) · [English user guide](docs/USER-GUIDE.en.md) · [Build instructions](docs/BUILD.md) · [Beta validation](docs/VALIDATION.md)

## Download and start

Get the Windows x64 ZIP from [Releases](https://github.com/SmailWolf/anvil-localization-studio/releases). Extract the entire archive and open **Start-Anvil-Studio.cmd** (or `bin/anvil-studio.exe`). Qt/LZO/compiler runtime libraries and a CPU translation engine are included. Windows 10/11 x64; no Qt SDK, MSYS2 or Wine installation is needed. [Windows guide](docs/WINDOWS.md).

The [previous Linux beta](https://github.com/SmailWolf/anvil-localization-studio/releases/tag/v0.7.0-beta.1) remains available. Run `start-studio.sh` and keep its folders together; compatible system Qt libraries are required.

Select **Мова / Language → Українська / English**, save your project, then restart the app. Interface language does not change the project's source or target language.

Choose **Translation → Download model…**. TranslateGemma 4B Q4_K_M is downloaded from Hugging Face into the chosen folder (2,489,909,760 bytes). Progress and cancellation are available; file size and SHA-256 are verified before the file is accepted. The model is separate from the program and governed by the [Gemma terms](https://ai.google.dev/gemma/terms). Translation then runs locally. The model download sends a standard file request to Hugging Face/CDN; game text is not uploaded.

## Features

- Separate game profiles and format plugins; JSON project import/export, CSV import/export, glossary, translation statuses, autosaves and backups.
- English package extraction and project update with translation carry-over by ID/source.
- Local TranslateGemma batch translation, all untranslated rows, exact duplicate grouping, placeholder checks, service-text filtering, progress cache and review before applying.
- Bundled Noto Sans (OFL-1.1), Ukrainian/Belarusian glyph checks, font preview/export, and optional experimental raw GFOF v3.3 compilation on Linux and Windows. [Font guide](docs/FONTS.md).
- Experimental translation packaging into a new external folder, with optional full modified archive copies. It does not install the result.

## Beta limits

This is **not general `.forge` support**. The readers cover the observed Wildlands v27 / CFD v1 LZO and Breakpoint LocalizationPackage / CFD v3 variants described in the format documentation. Future game updates may need reader changes.

Packaging has been checked by decoding its output and comparing resources. **Newly packaged archives have not been verified to run in either game.** UMAC is not recalculated and The translation packer does not insert fonts. Font coverage and an experimental raw GFOF compiler are available separately; in-game rendering remains unverified. A structurally valid package is not a confirmed playable mod. The Windows package includes CPU inference only. CUDA is included in the earlier Linux package but has not been runtime tested for this release. Choose Ukrainian (`uk`) or Belarusian (`be`) in Project → Project languages before translating. Keep separate projects for each target. Model output is a draft requiring human review.

## Build and tests

See [docs/BUILD.md](docs/BUILD.md). Source build requires CMake 3.21+, C++20 compiler, Qt 6.5+ Core/Widgets/Network/Test/LinguistTools and LZO2. A compatible `llama-server` is required separately when using a source build.

## License

Application source: **GPL-2.0-or-later**, see [COPYING](COPYING) and [LICENSE-NOTICE.txt](LICENSE-NOTICE.txt). Bundled llama.cpp engines retain their own MIT notices and corresponding source reference. TranslateGemma model weights use separate Gemma terms and are downloaded only on request. Proprietary game decompression libraries are not distributed.

This independent project is not affiliated with Ubisoft, Google, or the llama.cpp authors.
