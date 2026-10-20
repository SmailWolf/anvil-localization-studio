# User guide · Anvil Localization Studio 0.7.0-beta.2

**Beta software.** No game archives, extracted game text, completed game translations, patches or model weights are bundled. Supply your own game installation. All normal editor, extraction and packaging operations keep output outside the installed game.

## 1. Start and select the interface language

**Windows 10/11 x64:** extract the entire Windows ZIP and open **Start-Anvil-Studio.cmd** or `bin\anvil-studio.exe`. Qt/LZO/compiler DLLs and the CPU engine are bundled; no Qt SDK, MSYS2 or Wine installation is needed to run. See [WINDOWS.md](WINDOWS.md).

**Linux:** the previous Linux beta remains available in Releases. Run `start-studio.sh`; compatible system Qt libraries are required. Keep all subfolders together and do not install the app inside the game folder. See [BUILD.md](BUILD.md).

Choose **Мова / Language → English** or **Українська**. Save the open project and restart the application. This preference is remembered. The interface language is independent of the translation direction, which currently supports English → Ukrainian only.

## 2. Download the translation model

1. Open **Translation → Download model…**, even without opening a project. The same button is available in the translation dialog.
2. Review the linked Gemma terms. Choose a folder outside the game installation with at least 2.6 GB free. The default is the application's local data/models directory.
3. Click **Download**. The program downloads TranslateGemma 4B Q4_K_M from Hugging Face: **2,489,909,760 bytes**. A progress bar and MB counter are shown.
4. Wait for successful verification. GGUF signature, file size and SHA-256 must match before the file is accepted. The model path is saved automatically.

**Cancel download** stops the transfer and removes the incomplete file. A retry starts from the beginning; partial downloads are not resumed. Existing model files are never intentionally replaced: select an existing GGUF with **Browse…** in the translation dialog, or use a different download folder. Translation works offline after downloading. The model download does not send game text to Hugging Face.

## 3. Extract game texts

1. Select Wildlands or Breakpoint in the game selector.
2. Click **Extract from game…** and choose the main game folder containing `DataPC.forge` and the game executable. Do not choose `videos`, `sounddata` or `support`.
3. Save the new **Anvil Project** (`*.als.json`) in **Documents → Anvil Projects**, outside the game directory. This working file contains IDs, source strings, translations and the glossary.
4. Wait for extraction. English packages from the main archive and its patch are imported. Other source languages and all DLC archives are not covered yet.

Breakpoint requires the user's own `oo2core_7_win64.dll` from the game. On Linux it also requires Wine and the included decompression helper; the game DLL is not distributed. Wildlands uses LZO2. Extraction does not modify installed game files.

**Advanced — decoded resources** is for previously decoded payloads. For normal use, begin with **Extract from game…**; you do not need to find an unpacked LocalizationPackage folder yourself.

## 4. Edit and translate

Edit the **Ukrainian translation** column. **Ctrl+Enter** confirms an edited cell; Enter inserts a line break. Statuses are **To translate**, **Translated**, and **Reviewed**. Mark rows Reviewed after manual proofreading. The Glossary tab stores source terms and their target equivalents.

Use **Translate all untranslated…** for the whole project. There is no 500-row limit, and the search filter does not restrict this command. For a subset, select rows with Ctrl/Shift and choose **Translate selected rows…**.

Check the GGUF and `llama-server` paths. The Windows package includes a CPU engine. The previous Linux package includes CPU and CUDA engines. Disable CUDA if NVIDIA acceleration is unavailable; CPU has been verified, while CUDA has not been runtime tested for this release. Source builds require a separately supplied compatible engine.

Click **Translate**, review the proposed output, uncheck unwanted results, then click **Apply checked**. Results do not change the project until applied. The model is loaded into memory for the batch and unloaded when it finishes or stops.

Exact duplicate sources share one model request and are applied to the corresponding record IDs without deleting records. Existing translations and service prefixes `Name:`, `Context:`, `Acting:`, `//WARNING` are skipped by default. If identical text needs different meanings, edit the individual records afterwards.

Placeholder/tag checks help catch damaged output; meaning and grammar still need proofreading. Sources longer than 6000 characters are rejected. The optional intermediate cache reuses completed results when restarting a stopped queue; it is separate from model-file downloading.

## 5. Save, recover and update

Save regularly with **Save JSON…**. JSON keeps the complete project. CSV exchanges rows and does not contain the glossary; importing CSV replaces the current project's rows.

Autosaves run once per minute, and saving maintains backups. Use **Project → Recover autosave…** to recover one. Also keep your own copies of important projects.

After a game update, open the previous JSON and choose **Update project from game…**. Save under a new name. Translations carry over when the ID and original source match; new or changed source strings need translation. A game archive format change may require an updated reader.

## 6. Experimental packaging

Choose **Project → Package translation…**, select the original game installation and a new output folder outside it. You may restrict packaging to Reviewed rows. Enabling full archive copies writes modified `.forge` copies and may require many GB. Without that option, a compact package for later assembly is generated.

Output resources are decoded again and checked. **This does not establish a playable mod:** newly packaged archives have not been confirmed to run in either game, UMAC is not recalculated, and Ukrainian font coverage is not added. Installation is not automatic. Do not use output marked `INCOMPLETE`. See [PACKAGING.md](PACKAGING.md) for technical details in Ukrainian.

## Troubleshooting

- **Missing model:** download it from Translation or browse to an existing GGUF.
- **Download/checksum failure:** check internet access and free space, then retry. An incomplete file is not accepted as a model.
- **File already exists:** select it in the translation dialog or choose another download folder.
- **Engine missing:** extract the entire Linux package; for a source build select a compatible `llama-server`.
- **Output / Wine prefix must be outside game directory:** save projects and working files in Documents, outside the installation.
- **Squares instead of Ukrainian letters in game:** translating strings does not supply missing font glyphs. Automatic font replacement is not part of this beta.

For bug reports, include app version, OS, game, action and error message. Do not upload game archives, proprietary DLLs, complete extracted text databases or private information to public Issues.
