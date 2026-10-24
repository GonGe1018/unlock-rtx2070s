import struct
import unittest

from tools.analyze_tu104_prom import ParseError, analyze


def put16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def make_image(data, offset, signature, code_type, last):
    data[offset:offset + 2] = signature
    put16(data, offset + 0x18, 0x20)
    pcir = offset + 0x20
    data[pcir:pcir + 4] = b"PCIR"
    put16(data, pcir + 0x0A, 0x18)
    put16(data, pcir + 0x10, 1)
    data[pcir + 0x14] = code_type
    data[pcir + 0x15] = 0x80 if last else 0


def fixture():
    data = bytearray(b"\xff" * 0x80000)
    make_image(data, 0x000, b"\x55\xaa", 0x00, False)
    make_image(data, 0x200, b"\x55\xaa", 0x03, False)
    make_image(data, 0x400, b"VN", 0xE0, True)

    bit = 0x80
    data[bit:bit + 12] = b"\xff\xb8BIT\x00\x00\x01\x0c\x06\x01\x00"
    data[bit + 11] = (-sum(data[bit:bit + 11])) & 0xFF
    data[bit + 12:bit + 18] = b"p\x02\x04\x00\xc0\x00"
    put32(data, 0xC0, 0x500)

    table = 0x700
    data[table:table + 6] = b"\x01\x06\x06\x01\x00\x00"
    data[table + 6:table + 12] = b"\x85\x07\x00\x08\x00\x00"

    desc = 0xA00
    fields = (
        0x003C0201, 0x200, 0x200, 0, 0x20, 0, 0x100, 0,
        0x20, 0xE0, 0x100, 0, 0x100, 0x100, 0x100,
    )
    struct.pack_into("<15I", data, desc, *fields)
    image = desc + 0x3C
    for index in range(0x200):
        data[image + index] = (index * 17 + 3) & 0xFF
    dmem = image + 0x100
    data[dmem + 0x20:dmem + 0x24] = b"\x01\x04\x08\x01"
    struct.pack_into("<II", data, dmem + 0x24, 4, 0x40)
    mapper = [0] * 16
    mapper[0] = 0x50414D44
    mapper[1] = 0x00400003
    mapper[2] = 0x80
    mapper[3] = 0x40
    struct.pack_into("<16I", data, dmem + 0x40, *mapper)
    return bytes(data)


class Tu104PromTests(unittest.TestCase):
    def test_complete_structural_walk(self):
        data = fixture()
        result = analyze(data, second=data, efi_rom=data[:0x400])
        self.assertEqual(result["expansion_rom"]["adjustment"], "0x200")
        self.assertEqual(result["bit_p_token"]["table_offset"], "0x700")
        self.assertEqual(result["production_fwsec"]["descriptor_offset"],
                         "0xa00")
        self.assertEqual(result["production_fwsec"]["image"]["bytes"],
                         0x200)
        self.assertEqual(
            result["production_fwsec"]["mapper"]["command_offset"],
            "0x80")
        self.assertTrue(result["cross_capture"]["byte_identical"])
        self.assertTrue(result["efi_rom_comparison"]["legacy_prefix_identical"])

    def test_rejects_truncated_capture(self):
        with self.assertRaises(ParseError):
            analyze(fixture()[:0x40000])

    def test_rejects_fwsec_descriptor_outside_capture(self):
        data = bytearray(fixture())
        put32(data, 0x708, 0xFFF00)
        with self.assertRaises(ParseError):
            analyze(bytes(data))


if __name__ == "__main__":
    unittest.main()
