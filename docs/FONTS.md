# Ukrainian and Belarusian fonts · beta

The desktop font tool runs natively on Linux and Windows using Qt; Python is not required. Open **Project → Fonts — Ukrainian + Belarusian…**. Default: both alphabets, bundled **Noto Sans Regular**, copyright 2022 The Noto Project Authors, **SIL OFL 1.1**, redistributed unmodified with [license](../assets/fonts/OFL.txt). Font SHA-256: `478c538ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05df2b4f3823`.

Coverage includes full modern Ukrainian and Belarusian Cyrillic alphabets (Ґґ Єє Іі Її; Ёё Іі Ўў), ASCII 32–126 and apostrophes U+0027/U+2019/U+02BC. The test uses QRawFont's own character map, with no system-font fallback. The bundled font covers the union. Coverage does not establish linguistic quality or game-engine compatibility.

## Export a kit

Select both languages or just `uk`/`be`. Preview the letters; optionally select a custom TTF/OTF. Save to a **new directory outside the game**. The tool rejects existing destinations and detected game ancestors, including resolved symlinks. Outputs: original `font.ttf` or `font.otf`, `validation.json`, `README.txt`; bundled Noto also includes `OFL.txt`. Custom fonts need their owner's license before redistribution.

TTF/OTF files cannot be dropped into game archives to install a translation. The utility does not modify installed games.

## Experimental raw GFOF v3.3

An optional separately extracted donor enables generation of `experimental-font.gfof`. Only observed raw `GFOF 03 03 00 00`, 44-byte header / 36-byte records is accepted. Forge archives, PHXFD wrappers, other versions, malformed offsets, invalid Unicode/metrics and excess data are rejected. Donor round-trip is checked before generation; output is decoded again and coverage checked. The original header is retained except glyph count. Glyphs absent from the source font are preserved from the donor. Other donor glyphs and required characters are rasterized from the chosen font to SDF.

Match pixel size (default 50) and SDF spread (default 5) to the original asset. Regenerating glyphs changes visual metrics and may affect layout. No universal format support is claimed. This codec was researched using Breakpoint assets; **Wildlands native font containers remain unverified**. Exported blobs have not been verified in either game's renderer. The translation packer currently does not insert fonts. The kit must remain separate until archive insertion and a game test are implemented and verified.

The optional `tools/compile_gfof_font.py` compiler also supports `--language uk|be|both` (default both), requiring Pillow, NumPy, SciPy and fontTools. Its bounded codec tests need only Python's standard library. The native Qt compiler avoids these dependencies; both decoders agree on generated synthetic font fixtures.

No game-owned donor fonts or completed native font patches are distributed. Sources: [Noto](https://github.com/notofonts/latin-greek-cyrillic), [Unicode CLDR Belarusian exemplars](https://github.com/unicode-org/cldr/blob/main/common/main/be.xml).
