"""Experimental GFOF bitmap-font compiler; never installs into a game.

Requires Pillow, numpy, scipy and fontTools for compilation. The bounded codec itself uses
only the standard library. The donor must be a separately extracted GFOF blob,
not a forge or a PHXFD wrapper. Engine compatibility needs a separate game test.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct

UKRAINIAN = "АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя"
BELARUSIAN = "АБВГДЕЁЖЗІЙКЛМНОПРСТУЎФХЦЧШЫЬЭЮЯабвгдеёжзійклмнопрстуўфхцчшыьэюя"
APOSTROPHES = "’ʼ'"
def required_characters(language="both"):
    alphabets = {"uk": UKRAINIAN, "be": BELARUSIAN, "both": UKRAINIAN + BELARUSIAN}
    if language not in alphabets:
        raise ValueError("Choose uk, be or both")
    return set(map(ord, alphabets[language] + APOSTROPHES)) | set(range(32, 127))
RECORD = struct.Struct("<I7fI")
MAX_BYTES = 16 * 1024 * 1024

@dataclass
class Glyph:
    codepoint: int
    metrics: tuple[float, ...]
    pixels: bytes

def decode(blob: bytes) -> tuple[bytes, list[Glyph]]:
    if not 44 <= len(blob) <= MAX_BYTES or blob[:8] != b"GFOF\x03\x03\0\0":
        raise ValueError("Unsupported or truncated GFOF header")
    count = struct.unpack_from("<I", blob, 20)[0]
    if not 1 <= count <= 4096 or 44 + count * RECORD.size > len(blob):
        raise ValueError("Invalid GFOF glyph count")
    cursor = 44 + count * RECORD.size
    glyphs, seen = [], set()
    for i in range(count):
        cp, *fields = RECORD.unpack_from(blob, 44 + i * RECORD.size)
        metrics, offset = tuple(fields[:7]), fields[7]
        if cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF or cp in seen:
            raise ValueError("Invalid or duplicate codepoint")
        if not all(math.isfinite(v) and abs(v) <= 65536 for v in metrics):
            raise ValueError("Invalid glyph metrics")
        width, height = metrics[5:7]
        if width != int(width) or height != int(height) or not (0 <= width <= 1024 and 0 <= height <= 1024):
            raise ValueError("Invalid bitmap dimensions")
        length = int(width) * int(height)
        if offset != cursor or length > len(blob) - cursor:
            raise ValueError("Overlapping, noncontiguous or truncated glyph bitmap")
        glyphs.append(Glyph(cp, metrics, blob[cursor:cursor + length]))
        seen.add(cp)
        cursor += length
    if cursor != len(blob):
        raise ValueError("Trailing GFOF bytes")
    return blob[:44], glyphs

def encode(header: bytes, glyphs: list[Glyph]) -> bytes:
    if len(header) != 44 or not 1 <= len(glyphs) <= 4096:
        raise ValueError("Invalid header or glyph list")
    header = bytearray(header)
    struct.pack_into("<I", header, 20, len(glyphs))
    cursor = 44 + len(glyphs) * RECORD.size
    table, pixels = bytearray(), bytearray()
    for g in glyphs:
        if len(g.metrics) != 7:
            raise ValueError("Expected seven metrics")
        w, h = g.metrics[5:7]
        if int(w) * int(h) != len(g.pixels):
            raise ValueError("Bitmap length mismatch")
        table += RECORD.pack(g.codepoint, *g.metrics, cursor)
        pixels += g.pixels
        cursor += len(g.pixels)
        if cursor > MAX_BYTES:
            raise ValueError("Font exceeds output limit")
    blob = bytes(header + table + pixels)
    decode(blob)
    return blob

def compile_font(font_path: Path, donor_blob: bytes, pixel_size=50, spread=5.0, scale=4, language="both"):
    from PIL import ImageFont
    import numpy as np
    from scipy.ndimage import distance_transform_edt
    from fontTools.ttLib import TTFont
    header, donor = decode(donor_blob)
    if encode(header, donor) != donor_blob:
        raise ValueError("Donor codec does not round-trip exactly")
    if not 20 <= pixel_size <= 100 or not 1 <= spread <= 16 or not 1 <= scale <= 8:
        raise ValueError("Invalid rasterization parameters")
    cmap = TTFont(font_path).getBestCmap()
    required = required_characters(language)
    missing = sorted(cp for cp in required if cp not in cmap or cmap[cp] == ".notdef")
    if missing:
        raise ValueError(f"Font lacks required characters: {missing}")
    old = {g.codepoint: g for g in donor}
    points = sorted(required | set(old))
    face = ImageFont.truetype(str(font_path), pixel_size * scale)
    padding = math.ceil(spread) + 1
    output, inherited = [], []
    for cp in points:
        if cp not in cmap or cmap[cp] == ".notdef":
            output.append(old[cp])
            inherited.append(cp)
            continue
        char = chr(cp)
        advance = face.getlength(char) / scale
        mask, offset = face.getmask2(char, mode="L", anchor="ls")
        if not mask.size[0] or not mask.size[1] or not any(mask):
            output.append(Glyph(cp, (advance, 0., 0., 0., 0., 0., 0.), b""))
            continue
        width = math.ceil(mask.size[0] / scale) + 2 * padding
        height = math.ceil(mask.size[1] / scale) + 2 * padding
        alpha = np.zeros((height * scale, width * scale), dtype=np.uint8)
        alpha[padding*scale:padding*scale+mask.size[1], padding*scale:padding*scale+mask.size[0]] = np.asarray(mask, dtype=np.uint8).reshape(mask.size[1], mask.size[0])
        inside = alpha >= 128
        distance = distance_transform_edt(inside) - distance_transform_edt(~inside)
        distance = (distance - np.sign(distance) * .5) / scale
        # Sample each output pixel's centre. Edge=128; range +/-spread pixels.
        distance = distance[scale//2::scale, scale//2::scale]
        bitmap = np.clip(np.rint(128 + distance * (127.5 / spread)), 0, 255).astype(np.uint8)
        left = offset[0] / scale - padding
        bottom = -(offset[1] / scale + height - padding)
        metrics = (advance, left, bottom, float(width), float(height), float(width), float(height))
        output.append(Glyph(cp, metrics, bitmap.tobytes()))
    blob = encode(header, output)
    _, checked = decode(blob)
    assert {g.codepoint for g in checked} >= required
    return blob, {"glyph_count": len(checked), "languages": ["uk", "be"] if language == "both" else [language], "required_characters_verified": len(required),
                  "legacy_glyphs_preserved": inherited, "pixel_size": pixel_size,
                  "sdf_spread": spread, "supersampling": scale,
                  "donor_roundtrip_exact": True, "generated_roundtrip_exact": encode(*decode(blob)) == blob,
                  "engine_rendering_tested": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--donor", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--language", choices=["uk", "be", "both"], default="both")
    args = parser.parse_args()
    # A new working directory only; no game discovery or in-place replacement.
    if args.output_dir.exists():
        raise SystemExit("Output directory already exists")
    # Resolve existing ancestors, including symlinks; reject game installations.
    for parent in [args.output_dir.resolve(), *args.output_dir.resolve().parents]:
        if any((parent / name).exists() for name in ("GRB.exe", "GRW.exe", "DataPC.forge")):
            raise SystemExit("Output must be outside a game directory")
    blob, report = compile_font(args.font, args.donor.read_bytes(), language=args.language)
    args.output_dir.mkdir()
    (args.output_dir / "font.gfof").write_bytes(blob)
    report.update({"font": args.font.name, "sha256": hashlib.sha256(blob).hexdigest(),
                   "game_files_written": False})
    (args.output_dir / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
