#!/usr/bin/env python3
"""Parse a saved PCI option ROM without accessing any device."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


class RomError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RomError(message)


def u16(data: bytes, offset: int) -> int:
    require(0 <= offset <= len(data) - 2, "uint16 outside supplied data")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    require(0 <= offset <= len(data) - 4, "uint32 outside supplied data")
    return struct.unpack_from("<I", data, offset)[0]


def digest(data: bytes) -> dict[str, object]:
    return {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "crc32": f"{zlib.crc32(data):08x}",
        "sum8": f"{sum(data) & 0xff:02x}",
    }


def parse_image(data: bytes, offset: int, index: int) -> dict[str, object]:
    require(offset <= len(data) - 0x1c, f"image {index}: truncated header")
    require(data[offset:offset + 2] == b"\x55\xaa",
            f"image {index}: missing 55aa signature")
    pcir_relative = u16(data, offset + 0x18)
    pcir = offset + pcir_relative
    require(pcir <= len(data) - 0x18, f"image {index}: PCIR outside file")
    require(data[pcir:pcir + 4] == b"PCIR",
            f"image {index}: missing PCIR signature")

    structure_length = u16(data, pcir + 0x0a)
    require(structure_length >= 0x18,
            f"image {index}: undersized PCIR structure")
    require(pcir <= len(data) - structure_length,
            f"image {index}: truncated PCIR structure")
    image_units = u16(data, pcir + 0x10)
    require(image_units != 0, f"image {index}: zero image length")
    image_size = image_units * 512
    require(offset <= len(data) - image_size,
            f"image {index}: declared image exceeds file")

    vendor = u16(data, pcir + 4)
    device = u16(data, pcir + 6)
    revision = data[pcir + 0x0c]
    class_code = int.from_bytes(data[pcir + 0x0d:pcir + 0x10], "little")
    code_revision = u16(data, pcir + 0x12)
    code_type = data[pcir + 0x14]
    indicator = data[pcir + 0x15]
    image = data[offset:offset + image_size]

    result: dict[str, object] = {
        "index": index,
        "offset": f"0x{offset:x}",
        "pcir_offset": f"0x{pcir:x}",
        "pcir_relative_offset": f"0x{pcir_relative:x}",
        "pcir_structure_bytes": structure_length,
        "vendor_id": f"{vendor:04x}",
        "device_id": f"{device:04x}",
        "class_code": f"{class_code:06x}",
        "pcir_revision": revision,
        "code_revision": f"0x{code_revision:04x}",
        "code_type": code_type,
        "last_image": bool(indicator & 0x80),
        "indicator": f"0x{indicator:02x}",
        **digest(image),
    }

    if code_type == 3:
        require(offset <= len(data) - 0x1a,
                f"image {index}: truncated EFI ROM header")
        result["efi"] = {
            "signature": f"0x{u32(data, offset + 4):08x}",
            "subsystem": f"0x{u16(data, offset + 8):04x}",
            "machine_type": f"0x{u16(data, offset + 0x0a):04x}",
            "compression_type": u16(data, offset + 0x0c),
            "image_header_relative_offset": f"0x{u16(data, offset + 0x16):x}",
        }
    return result


def printable_strings(data: bytes, minimum: int = 8) -> list[dict[str, object]]:
    pattern = re.compile(rb"[ -~]{%d,}" % minimum)
    return [
        {"offset": f"0x{match.start():x}",
         "text": match.group().decode("ascii")}
        for match in pattern.finditer(data)
    ]


def analyze(data: bytes) -> dict[str, object]:
    images: list[dict[str, object]] = []
    offset = 0
    for index in range(16):
        image = parse_image(data, offset, index)
        images.append(image)
        offset += int(image["bytes"])
        if image["last_image"]:
            break
    else:
        raise RomError("image chain exceeded 16 entries")

    require(images[-1]["last_image"], "image chain has no last-image flag")
    require(offset == len(data), "bytes remain after declared image chain")

    target_matches = [
        image for image in images
        if image["vendor_id"] == "10de" and image["device_id"] == "1e84"
    ]
    return {
        "mode": "offline only; no device access or output mutation",
        "file": digest(data),
        "chain_complete": offset == len(data),
        "chain_bytes": offset,
        "image_count": len(images),
        "all_images_target_10de_1e84": len(target_matches) == len(images),
        "images": images,
        "notable_strings": [
            item for item in printable_strings(data)
            if re.search(r"NVIDIA|Version|GV-N207|Copyright|VGA", item["text"], re.I)
        ],
        "scope": {
            "pci_option_rom_chain": True,
            "physical_spi_prom_capture": False,
            "native_fwsec_presence_proven": False,
            "safe_to_treat_as_flash_backup": False,
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.rom.read_bytes())
    except RomError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
