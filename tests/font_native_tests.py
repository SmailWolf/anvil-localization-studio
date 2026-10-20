import math
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compile_gfof_font import Glyph, decode, encode

def fixture():
    header = bytearray(44)
    header[:8] = b"GFOF\x03\x03\0\0"
    struct.pack_into("<ffI", header, 8, .78, .28, 1)
    struct.pack_into("<I", header, 40, 1000)
    return encode(bytes(header), [Glyph(65, (20., -1., -2., 2., 2., 2., 2.), bytes([0,64,128,255])),
                                  Glyph(0x0406, (8., 0., 0., 1., 2., 1., 2.), bytes([127,255]))])

class NativeFontTests(unittest.TestCase):
    def test_unicode_and_exact_roundtrip(self):
        original = fixture()
        header, glyphs = decode(original)
        self.assertEqual([g.codepoint for g in glyphs], [65, 0x0406])
        self.assertEqual(encode(header, glyphs), original)

    def test_every_truncation(self):
        blob = fixture()
        for end in range(len(blob)):
            with self.subTest(end=end), self.assertRaises(ValueError):
                decode(blob[:end])

    def test_invalid_records(self):
        cases = []
        for value in [4097,0]:
            b = bytearray(fixture()); struct.pack_into("<I",b,20,value); cases.append(b)
        for value in [0xD800,0x110000]:
            b = bytearray(fixture()); struct.pack_into("<I",b,44,value); cases.append(b)
        b = bytearray(fixture()); struct.pack_into("<I",b,80,65); cases.append(b)
        b = bytearray(fixture()); struct.pack_into("<f",b,48,math.nan); cases.append(b)
        b = bytearray(fixture()); struct.pack_into("<f",b,68,1.5); cases.append(b)
        b = bytearray(fixture()); struct.pack_into("<I",b,76,0); cases.append(b)
        cases.append(fixture()+b"junk")
        for i, blob in enumerate(cases):
            with self.subTest(case=i), self.assertRaises(ValueError):
                decode(bytes(blob))

    def test_encoder_refuses_bitmap_mismatch(self):
        header, glyphs = decode(fixture())
        glyphs[0].pixels = b"bad"
        with self.assertRaises(ValueError): encode(header,glyphs)

if __name__ == "__main__":
    unittest.main()
