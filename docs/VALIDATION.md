# Validation · 0.8.0-beta.1 · 2026-10-20

## Current Linux checks

16/16 CTest suites passed: font codec and native compilation, core/CSV/storage, both game readers/plugins, Ukrainian/Belarusian package round trips on synthetic archives, output guards, model download fixtures, translation validation and UI.

The real pinned TranslateGemma 4B Q4_K_M model was tested on CPU for English → Ukrainian and English → Belarusian, including placeholder preservation, resuming cached results and changing target language in the same cache. This checks the workflow, not professional linguistic quality; machine output requires review.

The bundled Noto Sans covers both complete alphabets, ASCII and apostrophes without font fallback. A synthetic C++-generated GFOF was decoded and round-tripped by the independent Python codec. Donor glyphs absent from the TTF are retained. New destination / game directory / symlink guards are covered. Font UI checks include both languages, preview and enabling export only with full coverage.

## Windows

The new native Windows build and all 16 suites run in the Windows x64 workflow. The release notes identify the successful run for each published binary.

The previous 0.7.0-beta.2 baseline passed native build/tests, clean-PATH deployment, real CPU inference and GUI preview/apply: [baseline run](https://github.com/SmailWolf/anvil-localization-studio/actions/runs/39437800774). This is historical evidence, not a result for the new font feature.

## Limits and distribution

No installed game files were changed. Distribution excludes game archives, extracted game text databases, completed game translations, proprietary game DLLs, game-owned donor fonts, native game patch payloads and model weights. Synthetic examples and tests are included. Noto Sans is redistributed under OFL 1.1 with its license; the application remains GPL-2.0-or-later.

Font coverage and codec round trips do not confirm game rendering. Newly packaged archives have not been verified to run in either game; UMAC is not recalculated. The translation packer does not insert font resources. Wildlands native font containers remain unverified. Automated Windows tests are not a full human desktop/game test; CUDA is not runtime verified. See [FONTS.md](FONTS.md).
