import importlib.util
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_option_rom", ROOT / "tools" / "analyze_option_rom.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def image(code_type, last):
    data = bytearray(512)
    data[0:2] = b"\x55\xaa"
    data[2] = 1
    struct.pack_into("<H", data, 0x18, 0x20)
    data[0x20:0x24] = b"PCIR"
    struct.pack_into("<H", data, 0x24, 0x10DE)
    struct.pack_into("<H", data, 0x26, 0x1E84)
    struct.pack_into("<H", data, 0x2A, 0x18)
    data[0x2D:0x30] = bytes((0, 0, 3))
    struct.pack_into("<H", data, 0x30, 1)
    data[0x34] = code_type
    data[0x35] = 0x80 if last else 0
    if code_type == 3:
        struct.pack_into("<I", data, 4, 0x00000EF1)
        struct.pack_into("<H", data, 8, 0x000B)
        struct.pack_into("<H", data, 0x0A, 0x8664)
        struct.pack_into("<H", data, 0x0C, 1)
        struct.pack_into("<H", data, 0x16, 0x40)
    data[-1] = (-sum(data)) & 0xFF
    return bytes(data)


class OptionRomAnalysisTests(unittest.TestCase):
    def test_two_image_chain(self):
        result = MODULE.analyze(image(0, False) + image(3, True))
        self.assertTrue(result["chain_complete"])
        self.assertEqual(result["image_count"], 2)
        self.assertTrue(result["all_images_target_10de_1e84"])
        self.assertEqual(result["images"][0]["sum8"], "00")
        self.assertEqual(result["images"][1]["efi"]["machine_type"], "0x8664")

    def test_rejects_trailing_data(self):
        with self.assertRaises(MODULE.RomError):
            MODULE.analyze(image(0, True) + b"extra")

    def test_rejects_wrong_signature(self):
        bad = bytearray(image(0, True))
        bad[0] = 0
        with self.assertRaises(MODULE.RomError):
            MODULE.analyze(bytes(bad))


if __name__ == "__main__":
    unittest.main()
