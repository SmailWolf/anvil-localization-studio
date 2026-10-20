Anvil Localization Studio 0.7.0-beta.2 — Windows x64
BETA / БЕТА

1. Extract the whole ZIP into a folder outside the installed game.
2. Open Start-Anvil-Studio.cmd (or bin\anvil-studio.exe).
3. Choose Мова / Language -> Українська or English; save and restart.
4. Translation -> Download model... downloads TranslateGemma 4B (2.49 GB).
   CPU inference engine is included. This Windows package does not include CUDA.
5. Project -> Extract from game... reads your own original game installation.
   Save your project in Documents\Anvil Projects, outside the game.

Windows 10/11 x64. Qt, compiler and LZO runtime DLLs are bundled.
No Qt SDK, MSYS2 installation, Wine or administrator access is required to run.
The application is not code signed. Only obtain it from the project release.
No game files, game translations, patches, model weights or game DLLs included.
Breakpoint uses your own game's oo2core_7_win64.dll; it is not distributed.
The model runs locally after download; game text is not sent to a cloud API.

Docs\USER-GUIDE.md       Ukrainian instructions
Docs\USER-GUIDE.en.md    English instructions
Docs\WINDOWS.md          Windows build and validation details

Experimental packaging is NOT a confirmed playable mod. UMAC is not
recalculated and Ukrainian fonts are not added. The utility does not install
its generated packages into the game.

https://github.com/SmailWolf/anvil-localization-studio
