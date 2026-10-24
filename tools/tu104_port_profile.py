#!/usr/bin/env python3
"""Derive a target-locked TU104 FWSEC/FRTS port profile offline.

This tool consumes saved PROM captures and register-probe logs.  It never
opens a device, executes firmware, or emits a patched firmware image.  The
only mutation it performs is on a temporary in-memory copy of FWSEC DMEM so
that the exact command bytes and changed ranges can be reviewed.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

try:
    from tools.analyze_tu104_prom import analyze, ParseError
except ModuleNotFoundError:  # Support direct execution from tools/.
    from analyze_tu104_prom import analyze, ParseError


DRIVER_TAG = "610.57.04"
DRIVER_COMMIT = "e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb"
PROM_SHA256 = "df1b9144a31dda10e0b2ae65ecf80af6e1a0b5bf23981a98884159df5af7b9cc"
FWSEC_SHA256 = "92c083ef66bf5a4975b357dc674b7c345b378338453bd4ca9ab56c90c64e490a"

FB_SIZE = 8 * 1024 * 1024 * 1024
PRAMIN_SIZE = 0x100000
VBIOS_WORKSPACE_SIZE = 0x20000
WPR_ALIGNMENT = 0x20000
FRTS_SIZE = 0x100000
FRTS_COMMAND = 0x15
FRTS_MEDIA_FB = 2

REGISTER_SPEC = (
    ("FECS_FEATURE_PLM", 0x409650, 0xFFFFFF8F),
    ("FECS_FEATURE_READOUT", 0x409660, 0x000002F3),
    ("FECS_SS0", 0x409664, 0x00000000),
    ("FECS_SPEED_READOUT", 0x409668, 0x00400080),
    ("FECS_SS1", 0x40966C, 0x00000006),
    ("WPR2_LO", 0x1FA824, 0x1FFFFE00),
    ("WPR2_HI", 0x1FA828, 0x00000000),
    ("SEC2_CPUCTL", 0x840100, 0x00000010),
    ("GSP_CPUCTL", 0x110100, 0x00000010),
    ("GFW_BOOT_OK", 0x118234, 0x000003FF),
    ("VGA_WORKSPACE_BASE", 0x625F04, 0x00020E09),
    ("VBIOS_SCRATCH14", 0x001438, 0x00000000),
)

EXPECTED_FWSEC_DESCRIPTOR = {
    "vDesc": 0x003C0201,
    "StoredSize": 0x9DF0,
    "UncompressedSize": 0x9DF0,
    "VirtualEntry": 0,
    "InterfaceOffset": 0xE0,
    "IMEMPhysBase": 0,
    "IMEMLoadSize": 0x9A00,
    "IMEMVirtBase": 0,
    "IMEMSecBase": 0x400,
    "IMEMSecSize": 0x9600,
    "DMEMOffset": 0x9A00,
    "DMEMPhysBase": 0,
    "DMEMLoadSize": 0x3F0,
    "altIMEMLoadSize": 0x9A00,
    "altDMEMLoadSize": 0x6638,
}


class ProfileError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ProfileError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def hx(value, width=0):
    return f"0x{value:0{width}x}" if width else f"0x{value:x}"


def as_int(value):
    return int(value, 0) if isinstance(value, str) else value


def parse_register_log(data):
    """Parse and strictly validate one 0.5.0 target register log."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProfileError(f"register log is not UTF-8: {error}") from error
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    require(text.count("RTX 2070 SUPER TU104 PREFLIGHT 0.5.0-REGISTER-PROBE") == 1,
            "register log has the wrong or duplicate probe banner")
    target = re.findall(
        r"^NVIDIA BDF=0000:01:00\.0 id=([0-9A-F]{8}) "
        r"sub=([0-9A-F]{8}) classrev=([0-9A-F]{8}) "
        r"command=([0-9A-F]{4}) status=([0-9A-F]{4})$",
        text, re.MULTILINE)
    require(target == [("1E8410DE", "3FF51458", "030000A1", "0007", "0010")],
            "target PCI identity/config does not match the captured card")
    require("Raw BAR dwords: A3000000 9000000C 00000000 A000000C "
            "00000000 00003001 ROM=A4000000" in text,
            "target BAR fingerprint is missing or changed")
    require("Register probe BAR/config guard status=Success" in text,
            "BAR/config guard did not succeed")

    boot = re.findall(
        r"^BOOT0 reads: first=([0-9A-F]{8}) second=([0-9A-F]{8}) "
        r"expected=([0-9A-F]{8}) status=Success$", text, re.MULTILINE)
    require(len(boot) == 1, "expected exactly one successful BOOT0 result")
    boot_values = tuple(int(value, 16) for value in boot[0])
    require(boot_values == (0x164000A1,) * 3,
            "BOOT0 is unstable or does not identify the captured TU104")

    rows = re.findall(
        r"^REG pass=([12]) name=([A-Z0-9_]+) offset=([0-9A-F]{8}) "
        r"value=([0-9A-F]{8}) status=Success$", text, re.MULTILINE)
    require(len(rows) == len(REGISTER_SPEC) * 2,
            "register log does not contain exactly two complete passes")
    by_pass = {1: {}, 2: {}}
    for pass_text, name, offset_text, value_text in rows:
        pass_number = int(pass_text)
        require(name not in by_pass[pass_number],
                f"duplicate {name} in register pass {pass_number}")
        by_pass[pass_number][name] = (int(offset_text, 16), int(value_text, 16))

    expected = {name: (offset, value)
                for name, offset, value in REGISTER_SPEC}
    require(by_pass[1] == expected and by_pass[2] == expected,
            "register passes differ from the target-locked TU104 snapshot")

    comparisons = re.findall(
        r"^REG compare name=([A-Z0-9_]+) offset=([0-9A-F]{8}) "
        r"first=([0-9A-F]{8}) second=([0-9A-F]{8}) stable=([01])$",
        text, re.MULTILINE)
    require(len(comparisons) == len(REGISTER_SPEC),
            "register comparison section is incomplete")
    compared = {}
    for name, offset_text, first_text, second_text, stable_text in comparisons:
        require(name not in compared, f"duplicate comparison for {name}")
        compared[name] = (int(offset_text, 16), int(first_text, 16),
                          int(second_text, 16), int(stable_text))
    for name, offset, value in REGISTER_SPEC:
        require(compared.get(name) == (offset, value, value, 1),
                f"comparison for {name} is missing, unstable, or changed")

    require("Register stability summary: stable=12 total=12." in text,
            "register stability summary did not pass")
    require("END bounded register probe status=Success" in text and
            "Bounded register probe status=Success" in text,
            "register probe did not finish successfully")
    return {
        "sha256": sha256(data),
        "boot0": boot_values[0],
        "registers": {name: {"offset": offset, "value": value}
                      for name, offset, value in REGISTER_SPEC},
    }


def decode_wpr2(raw):
    """Decode the bits 31:4 WPR2 field with its 4 KiB address alignment."""
    return ((raw >> 4) & 0x0FFFFFFF) << 12


def encode_wpr2(address):
    require(address % 0x1000 == 0, "WPR2 address is not 4 KiB aligned")
    field = address >> 12
    require(field <= 0x0FFFFFFF, "WPR2 address does not fit the Turing field")
    return field << 4


def derive_layout(registers, fb_size=FB_SIZE):
    require(fb_size == FB_SIZE,
            "this target-locked profile only accepts the observed 8 GiB FB")
    raw_workspace = registers["VGA_WORKSPACE_BASE"]["value"]
    workspace_valid = bool((raw_workspace >> 3) & 1)
    require(workspace_valid, "VGA workspace register is not marked valid")
    observed_workspace = ((raw_workspace >> 8) & 0xFFFFFF) << 16
    require(observed_workspace < fb_size,
            "observed VGA workspace lies outside framebuffer")

    relocation_required = observed_workspace < fb_size - PRAMIN_SIZE
    workspace = (fb_size - VBIOS_WORKSPACE_SIZE
                 if relocation_required else observed_workspace)
    vbios_reserved = workspace  # TU102/TU104 dispatch reports no MMU lock.
    wpr_end_margin = 0  # Normal first boot, with no apply-always override.
    wpr_end = ((vbios_reserved - wpr_end_margin) // WPR_ALIGNMENT
               * WPR_ALIGNMENT)
    require(wpr_end >= FRTS_SIZE, "not enough FB space for FRTS")
    frts_offset = wpr_end - FRTS_SIZE
    require(frts_offset % 0x1000 == 0, "FRTS offset is not 4 KiB aligned")

    current_lo_raw = registers["WPR2_LO"]["value"]
    current_hi_raw = registers["WPR2_HI"]["value"]
    return {
        "fb_size": hx(fb_size),
        "pramin_size": hx(PRAMIN_SIZE),
        "vga_workspace_register": hx(raw_workspace, 8),
        "vga_workspace_valid": workspace_valid,
        "vga_workspace_observed_offset": hx(observed_workspace),
        "vga_workspace_relocation_required": relocation_required,
        "vga_workspace_offset": hx(workspace),
        "vga_workspace_size": hx(fb_size - workspace),
        "mmu_lock_valid_for_tu102_tu104_dispatch": False,
        "vbios_reserved_offset": hx(vbios_reserved),
        "normal_boot_wpr_end_margin": hx(wpr_end_margin),
        "gsp_fw_wpr_end": hx(wpr_end),
        "frts_size": hx(FRTS_SIZE),
        "frts_offset": hx(frts_offset),
        "frts_offset_4k": hx(frts_offset >> 12),
        "wpr2_before": {
            "lo_raw": hx(current_lo_raw, 8),
            "hi_raw": hx(current_hi_raw, 8),
            "lo_address_if_decoded": hx(decode_wpr2(current_lo_raw)),
            "hi_address_if_decoded": hx(decode_wpr2(current_hi_raw)),
            "valid_range": decode_wpr2(current_hi_raw) >=
                           decode_wpr2(current_lo_raw),
        },
        "wpr2_expected_region_after_success": {
            "lo_address": hx(frts_offset),
            "hi_address": hx(wpr_end - WPR_ALIGNMENT),
            "lo_field_raw": hx(encode_wpr2(frts_offset), 8),
            "hi_field_raw": hx(encode_wpr2(wpr_end - WPR_ALIGNMENT), 8),
            "high_semantics": "base of the final 128 KiB protected block",
            "note": "Derived expectation only; this tool performs no MMIO write.",
        },
    }


def make_frts_command(frts_offset):
    require(frts_offset % 0x1000 == 0,
            "FRTS command offset is not 4 KiB aligned")
    offset_4k = frts_offset >> 12
    require(offset_4k <= 0xFFFFFFFF,
            "FRTS command offset does not fit its 32-bit field")
    read_vbios = struct.pack("<IIQII", 1, 24, 0, 0, 2)
    region = struct.pack("<5I", 1, 20, offset_4k,
                         FRTS_SIZE >> 12, FRTS_MEDIA_FB)
    # FWSECLIC_FRTS_CMD is READ_VBIOS followed by FRTS_REGION.  Its NvU64
    # member gives the outer C structure 8-byte alignment, so sizeof is 48
    # and the final four padding bytes are made deterministic here.
    command = read_vbios + region + bytes(4)
    require(len(command) == 48, "internal FRTS command-size error")
    return command


def changed_ranges(before, after):
    require(len(before) == len(after), "changed-range inputs differ in size")
    ranges = []
    start = None
    for index, (old, new) in enumerate(zip(before, after)):
        if old != new and start is None:
            start = index
        elif old == new and start is not None:
            ranges.append((start, index))
            start = None
    if start is not None:
        ranges.append((start, len(before)))
    return ranges


def simulate_dmem(data, prom_analysis, frts_offset):
    fwsec = prom_analysis["production_fwsec"]
    descriptor = {name: as_int(value)
                  for name, value in fwsec["descriptor"].items()}
    image_offset = as_int(fwsec["image_offset"])
    dmem_offset = descriptor["DMEMOffset"]
    dmem_size = descriptor["DMEMLoadSize"]
    dmem = data[image_offset + dmem_offset:
                image_offset + dmem_offset + dmem_size]
    require(len(dmem) == dmem_size, "FWSEC DMEM is truncated")
    require(sha256(dmem) == fwsec["dmem"]["sha256"],
            "FWSEC DMEM digest changed during extraction")

    mapper = fwsec["mapper"]
    mapper_offset = as_int(mapper["mapper_offset"])
    command_offset = as_int(mapper["command_offset"])
    command_capacity = mapper["command_bytes"]
    init_offset = as_int(mapper["init_command_offset"])
    command = make_frts_command(frts_offset)
    require(len(command) <= command_capacity,
            "FWSEC command buffer is too small for FRTS command")
    require(init_offset + 4 <= len(dmem) and
            command_offset + len(command) <= len(dmem),
            "FWSEC patch ranges exceed DMEM")
    require(not (command_offset < mapper_offset + 64 and
                 mapper_offset < command_offset + len(command)),
            "FWSEC command overlaps the mapper")

    simulated = bytearray(dmem)
    init_before = struct.unpack_from("<I", dmem, init_offset)[0]
    struct.pack_into("<I", simulated, init_offset, FRTS_COMMAND)
    command_before = bytes(dmem[command_offset:command_offset + len(command)])
    simulated[command_offset:command_offset + len(command)] = command
    ranges = changed_ranges(dmem, simulated)
    return {
        "mode": "temporary in-memory copy only",
        "patched_binary_emitted": False,
        "dmem_bytes": len(dmem),
        "dmem_sha256_before": sha256(dmem),
        "dmem_sha256_simulated": sha256(simulated),
        "changed_byte_count": sum(old != new
                                  for old, new in zip(dmem, simulated)),
        "changed_ranges": [{"offset": hx(start), "bytes": end - start}
                           for start, end in ranges],
        "init_command": {
            "offset": hx(init_offset),
            "before": hx(init_before, 8),
            "simulated": hx(FRTS_COMMAND, 8),
        },
        "command_buffer": {
            "offset": hx(command_offset),
            "capacity_bytes": command_capacity,
            "copied_bytes": len(command),
            "before_sha256": sha256(command_before),
            "simulated_sha256": sha256(command),
            "simulated_hex": command.hex(),
            "decoded": {
                "region": {
                    "version": 1,
                    "size": 20,
                    "offset_4k": hx(frts_offset >> 12),
                    "size_4k": FRTS_SIZE >> 12,
                    "media_type": FRTS_MEDIA_FB,
                },
                "read_vbios": {
                    "version": 1,
                    "size": 24,
                    "gfw_image_offset": hx(0),
                    "gfw_image_size": 0,
                    "flags": 2,
                },
            },
        },
    }


def validate_prom(prom_analysis):
    require(prom_analysis["capture"]["bytes"] == 0x80000,
            "profile requires the complete 512 KiB target capture")
    require(prom_analysis["capture"]["sha256"] == PROM_SHA256,
            "PROM SHA-256 does not match the two verified target captures")
    require(prom_analysis["expansion_rom"]["adjustment"] == "0x11000",
            "unexpected expansion-ROM adjustment")
    require(prom_analysis["bit_p_token"]["table_offset"] == "0x1ff24",
            "unexpected Falcon table offset")
    require(prom_analysis["production_fwsec_entry_index"] == 9,
            "unexpected production FWSEC entry index")
    fwsec = prom_analysis["production_fwsec"]
    require(fwsec["descriptor_offset"] == "0x3e3f8" and
            fwsec["image_offset"] == "0x3e434",
            "unexpected target FWSEC location")
    descriptor = {name: as_int(value)
                  for name, value in fwsec["descriptor"].items()}
    require(descriptor == EXPECTED_FWSEC_DESCRIPTOR,
            "target FWSEC descriptor geometry changed")
    require(fwsec["image"]["sha256"] == FWSEC_SHA256,
            "target FWSEC image digest changed")
    mapper = fwsec["mapper"]
    require(mapper["mapper_offset"] == "0x360" and
            mapper["command_offset"] == "0x3b0" and
            mapper["command_bytes"] == 64 and
            mapper["init_command_offset"] == "0x38c",
            "target FWSEC mapper geometry changed")


def build_profile(prom, second, register_logs):
    require(len(register_logs) == 2,
            "exactly two independently saved register logs are required")
    try:
        prom_analysis = analyze(prom, second=second)
    except ParseError as error:
        raise ProfileError(str(error)) from error
    validate_prom(prom_analysis)
    require(prom_analysis["cross_capture"]["byte_identical"],
            "the two PROM captures are not byte-identical")

    parsed_logs = [parse_register_log(log) for log in register_logs]
    snapshot = parsed_logs[0]["registers"]
    require(parsed_logs[1]["registers"] == snapshot and
            parsed_logs[1]["boot0"] == parsed_logs[0]["boot0"],
            "register snapshots differ across boots")
    layout = derive_layout(snapshot)
    frts_offset = as_int(layout["frts_offset"])
    simulation = simulate_dmem(prom, prom_analysis, frts_offset)

    return {
        "mode": "OFFLINE PROFILE ONLY: no device/USB access, firmware execution, MMIO write, DMA, reset, or flash command",
        "target": {
            "gpu": "NVIDIA GeForce RTX 2070 SUPER (TU104)",
            "pci_id": "10de:1e84",
            "subsystem": "1458:3ff5",
            "boot0": hx(parsed_logs[0]["boot0"], 8),
            "framebuffer_bytes": FB_SIZE,
        },
        "evidence": {
            "prom": prom_analysis["capture"],
            "second_prom": prom_analysis["cross_capture"]["second"],
            "prom_captures_byte_identical": True,
            "register_logs": [{"sha256": item["sha256"]}
                              for item in parsed_logs],
            "register_snapshots_across_boots_identical": True,
            "production_fwsec": {
                "descriptor_offset": prom_analysis["production_fwsec"]["descriptor_offset"],
                "image_offset": prom_analysis["production_fwsec"]["image_offset"],
                "image": prom_analysis["production_fwsec"]["image"],
                "imem": prom_analysis["production_fwsec"]["imem"],
                "dmem": prom_analysis["production_fwsec"]["dmem"],
                "descriptor": prom_analysis["production_fwsec"]["descriptor"],
                "mapper": prom_analysis["production_fwsec"]["mapper"],
            },
        },
        "register_snapshot": {
            name: {"offset": hx(item["offset"], 8),
                   "value": hx(item["value"], 8)}
            for name, item in snapshot.items()
        },
        "official_driver_model": {
            "tag": DRIVER_TAG,
            "commit": DRIVER_COMMIT,
            "turing_mmu_lock_dispatch": "no valid MMU-lock range",
            "normal_first_boot_wpr_end_margin": 0,
            "frts_bytes": FRTS_SIZE,
            "source_urls": [
                "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.57.04/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_tu102.c",
                "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.57.04/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c",
                "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.57.04/src/nvidia/src/kernel/gpu/gsp/kernel_gsp.c",
            ],
        },
        "layout": layout,
        "fwsec_dmem_simulation": simulation,
        "scope": {
            "target_fingerprints_all_matched": True,
            "two_boot_stability_confirmed": True,
            "production_fwsec_signature_cryptographically_verified": False,
            "firmware_execution_safety_inferred": False,
            "system_dma_reachability_verified": False,
            "unlock_performed": False,
            "ready_for_firmware_execution": False,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prom", type=Path)
    parser.add_argument("--second-capture", type=Path, required=True)
    parser.add_argument("--register-log", type=Path, action="append",
                        required=True)
    args = parser.parse_args()
    try:
        prom = args.prom.read_bytes()
        second = args.second_capture.read_bytes()
        logs = [path.read_bytes() for path in args.register_log]
        result = build_profile(prom, second, logs)
    except (OSError, ProfileError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
