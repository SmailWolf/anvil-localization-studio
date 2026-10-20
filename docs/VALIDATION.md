# Validation · 0.8.0-beta.1 · 2026-10-20

## Windows x64

[Native Windows build and tests](https://github.com/SmailWolf/anvil-localization-studio/actions/runs/39460011377) from code commit `33890f18aaa8d0da9f851e530c7067919c7abc37` completed successfully. All 16 CTest suites passed, including font alphabet/codec checks, Belarusian project UI, packaging for both languages, and the clean-path deployment probes. The packaged app and font dialog start with system paths only.

GitHub downloaded and SHA-256 verified the pinned TranslateGemma GGUF in temporary CI storage. The real CPU model test produced and validated results for English → Ukrainian and English → Belarusian, resumed the cache, then changed language in the same cache to confirm results stay separate. The UI preview/apply test also passed with duplicate IDs and placeholders. The model test took about 6 minutes on the Windows runner. Model output remains a draft and needs proofreading.

## Linux

[Arch Linux build and tests](https://github.com/SmailWolf/anvil-localization-studio/actions/runs/39460011422) completed successfully, all 16 CTest suites passed. The same local TranslateGemma model was exercised on CPU for both targets. New Linux app and CLI smoke tests passed.

The bundled Noto Sans covers both full alphabets, ASCII and apostrophes without font fallback. A C++ generated GFOF test resource round-tripped through the independent Python decoder; every Ukrainian and Belarusian letter produced a non-empty SDF bitmap. The font UI was opened in English and Ukrainian, with both language selectors, preview and export. These tests validate character coverage and resource encoding, not game rendering.

## Limits and distribution

No installed game files were changed. Distribution excludes game archives, extracted game text databases, completed game translations, proprietary game DLLs, game-owned donor fonts, native game patch payloads and model weights. Synthetic examples and tests remain included. Noto Sans is redistributed under SIL OFL 1.1 with its license; the application remains GPL-2.0-or-later.

Font coverage and round trips do not confirm game rendering. Newly packaged archives have not been verified to run in either game; UMAC is not recalculated. The packer does not insert font resources. Wildlands native font containers remain unverified. CUDA is not runtime verified.
