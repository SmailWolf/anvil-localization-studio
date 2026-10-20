# Local translation · 0.8.0-beta.1

The application uses TranslateGemma 4B Q4_K_M GGUF with a separately owned llama.cpp `llama-server` process. The source distribution does not include the engine or weights; the Linux bundle includes CPU/CUDA engines with MIT notices. No AION 2 installation is required.

## Model download

Translation → Download model… downloads one curated model over HTTPS. The request uses an immutable Hugging Face revision and permits redirects without reducing TLS security. No game text is transmitted during download. Progress is streamed to a temporary QSaveFile; cancellation, disk errors, wrong HTTP status, bad GGUF magic, excessive/truncated bytes and SHA-256 mismatch prevent accepting the file. Existing destination files and game installation ancestors are refused. Interrupted downloads restart from zero. No arbitrary URL or Hugging Face API-token field is exposed.

- Repository: [mradermacher/translategemma-4b-it-GGUF](https://huggingface.co/mradermacher/translategemma-4b-it-GGUF)
- Revision: `d37868f759d2f817a43d28dd80cf9f982ffa62fd`
- File: `translategemma-4b-it.Q4_K_M.gguf`
- Bytes: `2489909760`
- SHA-256: `81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330`
- Original: [google/translategemma-4b-it](https://huggingface.co/google/translategemma-4b-it)
- Terms: [Gemma terms](https://ai.google.dev/gemma/terms)

The default directory is QStandardPaths::AppLocalDataLocation/models; the selected verified path is stored in QSettings. The model is not bundled in ZIPs or the Git repository.

## Inference

A QThread owns one QProcess per batch, using a temporary loopback port, random API key, `--offline`, `--no-webui`, `--parallel 1`, Jinja template, context 8192, max output 4096 and temperature 0. Health and template metadata are checked before English → Ukrainian / Belarusian chat requests. Local inference requests do not use a proxy or redirects. The process exits when finished/stopped; Linux also sets parent-death signal.

Exact source duplicates share a request while preserving every target ID. Placeholders/tags are masked and validated after restoration; incomplete responses are rejected. Sources exceeding 6000 characters are rejected. Glossary matches are literal and require proofreading. Applying preview results is explicit.

Optional JSONL cache keys include source, target language, model identity, glossary and processing format. A QLockFile protects the cache; bounded reads reject malformed/oversized records and all recovered output is revalidated. Manual edits in a project do not alter cached model responses.

CPU inference was verified on Linux and in the native Windows beta CI using this exact model with synthetic Settings/Main menu/Hello {name}/Press [M] strings. The Windows job downloaded and SHA-256-verified the full GGUF into temporary CI storage, then ran both real-model and GUI preview/apply tests. Weights are excluded from artifacts. Downloading was also tested with synthetic network fixtures (including corruption/cancellation) and a live HTTPS byte range. The Windows package includes CPU only; CUDA remains unverified.


0.8 beta: English → Ukrainian (`uk`) and Belarusian (`be`) projects are supported. Keep one target language per project. Font kit preparation for both alphabets is separate from archive packaging; native game font rendering remains unverified. See [font guide](FONTS.md).
