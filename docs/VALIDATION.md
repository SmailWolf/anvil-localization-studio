# Validation · 0.7.0-beta.1 · 2026-10-20

Arch Linux x86_64; GCC 16; Qt 6.12; Release CMake build.

- **15/15 CTest suites passed**, including core storage/CSV, readers/plugins, output guards, experimental packaging, translation checks, UI and model downloading.
- Downloader fixtures: successful streamed transfer, SHA-256 mismatch, invalid signature/HTML, HTTP error, truncated/excessive body, cancellation cleanup, existing file preservation and rejection of new output beneath a game folder.
- English GUI test: translated project menu, search placeholder and model-download dialog. Translation placeholder numbers verified across the complete English catalog. Both startup languages checked with headless GUI smoke tests.
- Live Hugging Face metadata confirms the pinned revision, 2,489,909,760 bytes and SHA-256. A real HTTPS range request returned HTTP 206 and the expected GGUF v3 prefix. Full model was not downloaded again.
- Existing CPU inference validation uses the same model checksum. This beta does not add a new inference model. CUDA and Windows runtime are **not verified**.
- Previous packaging verification covers observed Wildlands v27/CFD1/LZO and Breakpoint CFD3/raw resources using synthetic fixtures and copies outside the game installation. Re-decoding packaged output does **not** confirm game runtime compatibility. UMAC and font coverage remain unresolved.

Distribution contains no game-owned data, complete game translation, proprietary DLL, game archive, native patch payload or model weights. Synthetic test fixtures remain included for reproducibility. No installed game files were changed for this beta.
