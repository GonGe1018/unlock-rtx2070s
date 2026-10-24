import hashlib
import struct
import unittest

from tools.tu104_port_profile import (
    FRTS_COMMAND,
    ProfileError,
    REGISTER_SPEC,
    changed_ranges,
    derive_layout,
    make_frts_command,
    parse_register_log,
)


def register_log(unstable=False):
    lines = [
        "RTX 2070 SUPER TU104 PREFLIGHT 0.5.0-REGISTER-PROBE",
        "NVIDIA BDF=0000:01:00.0 id=1E8410DE sub=3FF51458 "
        "classrev=030000A1 command=0007 status=0010",
        "Target profile MATCHED. Raw BAR dwords: A3000000 9000000C "
        "00000000 A000000C 00000000 00003001 ROM=A4000000",
        "Register probe BAR/config guard status=Success",
        "BOOT0 reads: first=164000A1 second=164000A1 "
        "expected=164000A1 status=Success",
    ]
    for pass_number in (1, 2):
        for name, offset, value in REGISTER_SPEC:
            if unstable and pass_number == 2 and name == "FECS_SS1":
                value ^= 1
            lines.append(
                f"REG pass={pass_number} name={name} offset={offset:08X} "
                f"value={value:08X} status=Success")
    for name, offset, value in REGISTER_SPEC:
        second = value ^ 1 if unstable and name == "FECS_SS1" else value
        stable = int(value == second)
        lines.append(
            f"REG compare name={name} offset={offset:08X} first={value:08X} "
            f"second={second:08X} stable={stable}")
    lines.extend([
        "Register stability summary: stable=12 total=12.",
        "END bounded register probe status=Success",
        "Bounded register probe status=Success",
    ])
    return ("\r\n".join(lines) + "\r\n").encode()


class Tu104PortProfileTests(unittest.TestCase):
    def test_register_log_and_exact_layout(self):
        result = parse_register_log(register_log())
        self.assertEqual(result["boot0"], 0x164000A1)
        layout = derive_layout(result["registers"])
        self.assertEqual(layout["vga_workspace_observed_offset"], "0x20e0000")
        self.assertTrue(layout["vga_workspace_relocation_required"])
        self.assertEqual(layout["vga_workspace_offset"], "0x1fffe0000")
        self.assertEqual(layout["gsp_fw_wpr_end"], "0x1fffe0000")
        self.assertEqual(layout["frts_offset"], "0x1ffee0000")
        self.assertEqual(layout["frts_offset_4k"], "0x1ffee0")
        self.assertFalse(layout["wpr2_before"]["valid_range"])
        self.assertEqual(
            layout["wpr2_expected_region_after_success"]["lo_field_raw"],
            "0x01ffee00")
        self.assertEqual(
            layout["wpr2_expected_region_after_success"]["hi_field_raw"],
            "0x01fffc00")

    def test_register_log_rejects_changed_second_pass(self):
        with self.assertRaises(ProfileError):
            parse_register_log(register_log(unstable=True))

    def test_frts_command_layout(self):
        command = make_frts_command(0x1FFEE0000)
        self.assertEqual(len(command), 48)
        self.assertEqual(struct.unpack_from("<IIQII", command),
                         (1, 24, 0, 0, 2))
        self.assertEqual(struct.unpack_from("<5I", command, 24),
                         (1, 20, 0x1FFEE0, 256, 2))
        self.assertEqual(command[44:], bytes(4))
        self.assertEqual(FRTS_COMMAND, 0x15)
        self.assertEqual(
            hashlib.sha256(command).hexdigest(),
            "38d69174cda2bd98d8b083b9058d524cb7f63cf23c89aac1e53f160a916ddae5")

    def test_changed_ranges(self):
        self.assertEqual(changed_ranges(b"abcdef", b"abXdeY"),
                         [(2, 3), (5, 6)])


if __name__ == "__main__":
    unittest.main()
