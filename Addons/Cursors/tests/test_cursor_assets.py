import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cursor_converter", ROOT / "convert.py")
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


def decode_blp(path):
    data = path.read_bytes()
    magic, version, encoding, alpha, preferred, mips, width, height = struct.unpack_from("<4sI4BII", data)
    if (magic, version, alpha, preferred, mips, width, height) != (b"BLP2", 1, 8, 2, 0, 32, 32):
        raise AssertionError("Unexpected native cursor texture header")
    offset = struct.unpack_from("<I", data, 20)[0]
    if encoding == 3:
        return Image.frombytes("RGBA", (width, height), data[offset:], "raw", "BGRA")
    if encoding != 1:
        raise AssertionError("Unsupported native cursor encoding")
    pixels = []
    for i in range(width * height):
        index = data[offset + i]
        b, g, r, _ = struct.unpack_from("4B", data, 148 + index * 4)
        pixels.append((r, g, b, data[offset + width * height + i]))
    image = Image.new("RGBA", (width, height))
    image.putdata(pixels)
    return image


class CursorAssetsTest(unittest.TestCase):
    def test_starcraft_uses_original_ani_sequence_and_delays(self):
        self.assertEqual(converter.read_ani_timing(ROOT / "Raw/Starcraft/Cursor.ani"),
                         (5, [0, 0, 1, 2, 3, 4], [25, 5, 5, 5, 5, 5]))

    def test_each_starcraft_frame_has_transparent_background_and_keeps_color(self):
        for frame in range(1, 6):
            with self.subTest(frame=frame):
                expected = Image.open(ROOT / "Raw/Starcraft" / f"normal cursor_page_{frame:02}.png").convert("RGBA")
                actual = decode_blp(ROOT / "Textures" / f"Starcraft{frame:02}.blp")
                self.assertEqual(actual.getpixel((31, 31))[3], 0)
                for (r, g, b, _), (r2, g2, b2, a2) in zip(expected.getdata(), actual.getdata()):
                    if a2:
                        self.assertEqual((r, g, b), (r2, g2, b2))

    def test_hearthstone_is_palettized_and_keeps_soft_alpha(self):
        actual = decode_blp(ROOT / "Textures/Hearthstone.blp")
        self.assertEqual((ROOT / "Textures/Hearthstone.blp").read_bytes()[8], 1)
        self.assertGreater(len(set(actual.getchannel("A").tobytes())), 2)

    def test_visible_pixels_cannot_be_trimmed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.png"
            Image.new("RGBA", (32, 36), (1, 2, 3, 255)).save(source)
            with self.assertRaisesRegex(ValueError, "visible pixels"):
                converter.convert(source, Path(directory) / "out.blp")

    def test_truncated_animation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.ani"
            source.write_bytes((ROOT / "Raw/Starcraft/Cursor.ani").read_bytes()[:-1])
            with self.assertRaises(ValueError):
                converter.read_ani_timing(source)


if __name__ == "__main__":
    unittest.main()
