import struct
import unittest
import zlib

from tools.extract_tu10x_bindata import extract_array


def fixture(label: str, data: bytes, compressed: bool) -> str:
    if compressed:
        compressor = zlib.compressobj(level=9, wbits=-15)
        body = compressor.compress(data) + compressor.flush()
        stored = body + struct.pack("<II", zlib.crc32(data), len(data))
    else:
        stored = data
    values = ", ".join(f"0x{byte:02x}" for byte in stored)
    return f"""
// COMPRESSION: {'YES' if compressed else 'NO'}
// DATA SIZE (bytes): {len(data)}
// COMPRESSED SIZE (bytes): {len(stored) if compressed else 'N/A'}
static unsigned char prefix_BINDATA_LABEL_{label}_data[] = {{ {values}, }};
"""


class ExtractTu10xBindataTests(unittest.TestCase):
    def test_uncompressed_array(self):
        self.assertEqual(extract_array(fixture("PLAIN", b"abcd", False), "PLAIN"), b"abcd")

    def test_raw_deflate_with_gzip_trailer(self):
        data = bytes(range(256)) * 4
        self.assertEqual(extract_array(fixture("PACKED", data, True), "PACKED"), data)

    def test_duplicate_label_refused(self):
        source = fixture("DUP", b"abcd", False)
        with self.assertRaisesRegex(ValueError, "missing or duplicate"):
            extract_array(source + source, "DUP")

    def test_corrupt_trailer_refused(self):
        source = fixture("BAD", b"payload", True)
        source = source.replace("0x07, 0x00, 0x00, 0x00", "0x08, 0x00, 0x00, 0x00")
        with self.assertRaisesRegex(ValueError, "trailer mismatch"):
            extract_array(source, "BAD")


if __name__ == "__main__":
    unittest.main()
