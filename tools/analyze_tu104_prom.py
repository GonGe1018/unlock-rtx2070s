#!/usr/bin/env python3
"""Parse a saved TU104 PROM capture without accessing any device.

The format walk mirrors the public NVIDIA Turing flow at a structural level:
expansion-ROM chain -> BIT 'p' token -> Falcon ucode table -> production
FWSEC (entry type 0x85) -> version-2 descriptor. It emits JSON only.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


CAPTURE_SIZES = (0x80000, 0x100000)
ROM_SIGNATURES = (b"\x55\xaa", b"VN", b"\x77\xbb")
DATA_SIGNATURES = (b"PCIR", b"NPDS", b"RGIS")
BIT_SIGNATURE = b"\xff\xb8BIT\x00"
FWSEC_PRODUCTION_TYPE = 0x85
DESC_V2_FIELDS = (
    "vDesc",
    "StoredSize",
    "UncompressedSize",
    "VirtualEntry",
    "InterfaceOffset",
    "IMEMPhysBase",
    "IMEMLoadSize",
    "IMEMVirtBase",
    "IMEMSecBase",
    "IMEMSecSize",
    "DMEMOffset",
    "DMEMPhysBase",
    "DMEMLoadSize",
    "altIMEMLoadSize",
    "altDMEMLoadSize",
)


class ParseError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ParseError(message)


def region(data, offset, size, label="region"):
    require(isinstance(offset, int) and isinstance(size, int),
            f"{label}: non-integer range")
    require(offset >= 0 and size >= 0 and offset <= len(data) and
            size <= len(data) - offset,
            f"{label}: range 0x{offset:x}+0x{size:x} is outside capture")
    return data[offset:offset + size]


def u16(data, offset, label="u16"):
    return struct.unpack("<H", region(data, offset, 2, label))[0]


def u32(data, offset, label="u32"):
    return struct.unpack("<I", region(data, offset, 4, label))[0]


def digest(data):
    return {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "crc32": f"{zlib.crc32(data):08x}",
    }


def hex_fields(names, values):
    return {name: f"0x{value:x}" for name, value in zip(names, values)}


def parse_expansion_chain(data):
    position = 0
    base_bytes = 0
    expansion_offset = None
    images = []

    for index in range(16):
        signature = region(data, position, 2, "ROM image signature")
        require(signature in ROM_SIGNATURES,
                f"image {index}: unsupported signature at 0x{position:x}")
        data_offset = position + u16(data, position + 0x18,
                                     "ROM data-structure pointer")
        data_signature = region(data, data_offset, 4,
                                "ROM data-structure signature")
        require(data_signature in DATA_SIGNATURES,
                f"image {index}: unsupported data signature at 0x{data_offset:x}")
        structure_bytes = u16(data, data_offset + 0x0A,
                              "ROM data-structure length")
        image_bytes = u16(data, data_offset + 0x10,
                          "ROM image length") * 512
        code_type = data[data_offset + 0x14]
        indicator = data[data_offset + 0x15]
        last = bool(indicator & 0x80)
        npde_offset = (data_offset + structure_bytes + 15) & ~15
        npde = None

        if (region(data, npde_offset, 4, "NPDE probe") == b"NPDE"):
            revision = u16(data, npde_offset + 4, "NPDE revision")
            npde_bytes = u16(data, npde_offset + 6, "NPDE length")
            if revision in (0x100, 0x101):
                require(npde_bytes >= 10, "NPDE structure is too short")
                region(data, npde_offset, npde_bytes, "NPDE structure")
                subimage_bytes = u16(data, npde_offset + 8,
                                     "NPDE subimage length") * 512
                if npde_bytes > 10:
                    last = bool(data[npde_offset + 10] & 0x80)
                elif subimage_bytes < image_bytes:
                    last = False
                image_bytes = subimage_bytes
                npde = {
                    "offset": f"0x{npde_offset:x}",
                    "revision": f"0x{revision:x}",
                    "bytes": npde_bytes,
                }

        require(image_bytes > 0, f"image {index}: zero length")
        region(data, position, image_bytes, f"ROM image {index}")
        images.append({
            "index": index,
            "offset": f"0x{position:x}",
            "signature": signature.hex(),
            "data_signature": data_signature.decode("ascii"),
            "data_offset": f"0x{data_offset:x}",
            "code_type": f"0x{code_type:02x}",
            "indicator": f"0x{indicator:02x}",
            "bytes": image_bytes,
            "last": last,
            "npde": npde,
        })
        if base_bytes == 0 and code_type == 0:
            base_bytes = image_bytes
        if expansion_offset is None and code_type == 0xE0:
            expansion_offset = position
        if last:
            break
        position += image_bytes
    else:
        raise ParseError("expansion-ROM chain exceeded 16 images")

    require(images[-1]["last"], "expansion-ROM chain has no final image")
    require(base_bytes > 0, "legacy base image was not found")
    adjustment = 0
    if expansion_offset is not None:
        require(expansion_offset >= base_bytes,
                "expansion image precedes the end of the base image")
        adjustment = expansion_offset - base_bytes
    return images, adjustment


def find_bit_p_token(data):
    matches = []
    start = 0
    while True:
        bit = data.find(BIT_SIGNATURE, start)
        if bit < 0:
            break
        region(data, bit, 12, "BIT header")
        header_bytes = data[bit + 8]
        token_bytes = data[bit + 9]
        token_count = data[bit + 10]
        if (header_bytes >= 12 and token_bytes >= 6 and
                sum(region(data, bit, header_bytes, "BIT checksum")) & 0xFF == 0):
            region(data, bit + header_bytes, token_bytes * token_count,
                   "BIT token array")
            for index in range(token_count):
                token = bit + header_bytes + index * token_bytes
                if (data[token] == ord("p") and data[token + 1] == 2 and
                        u16(data, token + 2, "BIT p-token data size") >= 4):
                    pointer = (u32(data, token + 4, "BIT p-token pointer")
                               if token_bytes >= 8 else
                               u16(data, token + 4, "BIT p-token pointer"))
                    region(data, pointer,
                           u16(data, token + 2, "BIT p-token data size"),
                           "BIT p-token data")
                    matches.append({
                        "bit_offset": bit,
                        "header_bytes": header_bytes,
                        "token_bytes": token_bytes,
                        "token_count": token_count,
                        "token_index": index,
                        "data_offset": pointer,
                    })
        start = bit + 1
    require(len(matches) == 1,
            f"expected one valid BIT p token, found {len(matches)}")
    return matches[0]


def parse_ucode_table(data, table_offset, adjustment):
    header = region(data, table_offset, 6, "Falcon ucode table header")
    version, header_bytes, entry_bytes, entry_count = header[:4]
    require(version == 1, f"unsupported Falcon ucode table version {version}")
    require(header_bytes >= 6 and entry_bytes >= 6,
            "invalid Falcon ucode table geometry")
    region(data, table_offset, header_bytes + entry_bytes * entry_count,
           "Falcon ucode table")
    entries = []
    production = []
    for index in range(entry_count):
        offset = table_offset + header_bytes + index * entry_bytes
        entry_type = data[offset]
        entry_version = data[offset + 1]
        descriptor_relative = u32(data, offset + 2,
                                  "Falcon descriptor pointer")
        entry = {
            "index": index,
            "type": f"0x{entry_type:02x}",
            "version": entry_version,
            "descriptor_relative": f"0x{descriptor_relative:x}",
            "descriptor_offset": f"0x{adjustment + descriptor_relative:x}",
        }
        entries.append(entry)
        if entry_type == FWSEC_PRODUCTION_TYPE:
            production.append((index, adjustment + descriptor_relative))
    require(len(production) == 1,
            f"expected one production FWSEC entry, found {len(production)}")
    return {
        "offset": f"0x{table_offset:x}",
        "version": version,
        "header_bytes": header_bytes,
        "entry_bytes": entry_bytes,
        "entry_count": entry_count,
        "entries": entries,
    }, production[0]


def parse_mapper(dmem, interface_offset):
    header = region(dmem, interface_offset, 4, "FWSEC interface header")
    version, header_bytes, entry_bytes, entry_count = header
    require(version == 1 and header_bytes == 4 and entry_bytes == 8,
            "unsupported FWSEC interface-table geometry")
    require(1 <= entry_count <= 32, "invalid FWSEC interface entry count")
    region(dmem, interface_offset + header_bytes,
           entry_bytes * entry_count, "FWSEC interface entries")
    entries = []
    mapper_offsets = []
    for index in range(entry_count):
        entry = interface_offset + header_bytes + index * entry_bytes
        identifier, offset = struct.unpack(
            "<II", region(dmem, entry, 8, "FWSEC interface entry"))
        entries.append({"index": index, "id": identifier,
                        "offset": f"0x{offset:x}"})
        if identifier == 4:
            mapper_offsets.append(offset)
    require(len(mapper_offsets) == 1,
            f"expected one DMEM mapper interface, found {len(mapper_offsets)}")
    mapper_offset = mapper_offsets[0]
    require(mapper_offset % 4 == 0, "DMEM mapper is not aligned")
    mapper = struct.unpack(
        "<16I", region(dmem, mapper_offset, 64, "DMEM mapper"))
    require(mapper[0] == 0x50414D44 and mapper[1] == 0x00400003,
            "unsupported DMEM mapper signature/version")
    command_offset, command_bytes = mapper[2:4]
    require(command_offset % 4 == 0 and command_bytes >= 48,
            "invalid FWSEC command-buffer geometry")
    region(dmem, command_offset, command_bytes, "FWSEC command buffer")
    require(command_offset >= mapper_offset + 64 or
            command_offset + command_bytes <= mapper_offset,
            "FWSEC command buffer overlaps mapper")
    return {
        "interface_offset": f"0x{interface_offset:x}",
        "interface_version": version,
        "interface_header_bytes": header_bytes,
        "interface_entry_bytes": entry_bytes,
        "interface_entries": entries,
        "mapper_offset": f"0x{mapper_offset:x}",
        "mapper_magic": "DMAP",
        "mapper_version": f"0x{mapper[1]:08x}",
        "command_offset": f"0x{command_offset:x}",
        "command_bytes": command_bytes,
        "init_command_offset": f"0x{mapper_offset + 44:x}",
    }


def parse_fwsec_v2(data, descriptor_offset):
    vdesc = u32(data, descriptor_offset, "FWSEC vDesc")
    present = bool(vdesc & 1)
    version = (vdesc >> 8) & 0xFF
    descriptor_bytes = vdesc >> 16
    require(present and version == 2 and descriptor_bytes >= 60,
            f"unsupported FWSEC descriptor 0x{vdesc:08x}")
    values = struct.unpack(
        "<15I", region(data, descriptor_offset, 60,
                       "FWSEC version-2 descriptor"))
    fields = dict(zip(DESC_V2_FIELDS, values))
    image_offset = descriptor_offset + descriptor_bytes
    stored_size = fields["StoredSize"]
    require(stored_size > 0 and fields["UncompressedSize"] >= stored_size,
            "invalid FWSEC stored/uncompressed size")
    image = region(data, image_offset, stored_size, "FWSEC image")
    require(fields["IMEMLoadSize"] <= stored_size,
            "FWSEC IMEM exceeds stored image")
    require(fields["DMEMOffset"] <= stored_size and
            fields["DMEMLoadSize"] <= stored_size - fields["DMEMOffset"],
            "FWSEC DMEM exceeds stored image")
    require(fields["IMEMSecBase"] <= fields["IMEMLoadSize"] and
            fields["IMEMSecSize"] <=
            fields["IMEMLoadSize"] - fields["IMEMSecBase"],
            "FWSEC secure IMEM range exceeds IMEM")
    imem = region(image, 0, fields["IMEMLoadSize"], "FWSEC IMEM")
    dmem = region(image, fields["DMEMOffset"], fields["DMEMLoadSize"],
                  "FWSEC DMEM")
    mapper = parse_mapper(dmem, fields["InterfaceOffset"])
    return {
        "descriptor_offset": f"0x{descriptor_offset:x}",
        "descriptor_version": version,
        "descriptor_bytes": descriptor_bytes,
        "descriptor": hex_fields(DESC_V2_FIELDS, values),
        "image_offset": f"0x{image_offset:x}",
        "image": digest(image),
        "imem": {"offset": f"0x{image_offset:x}", **digest(imem)},
        "dmem": {
            "offset": f"0x{image_offset + fields['DMEMOffset']:x}",
            **digest(dmem),
        },
        "mapper": mapper,
    }


def analyze(data, second=None, efi_rom=None):
    require(len(data) in CAPTURE_SIZES,
            "expected a complete 512 KiB or 1 MiB PROM capture")
    images, adjustment = parse_expansion_chain(data)
    p_token = find_bit_p_token(data)
    table_relative = u32(data, p_token["data_offset"],
                         "Falcon ucode table pointer")
    table_offset = adjustment + table_relative
    ucode_table, (production_index, descriptor_offset) = parse_ucode_table(
        data, table_offset, adjustment)
    fwsec = parse_fwsec_v2(data, descriptor_offset)
    last_non_ff = next((index for index in range(len(data) - 1, -1, -1)
                        if data[index] != 0xFF), -1)

    result = {
        "mode": "OFFLINE ONLY: no device access, firmware execution, or output mutation",
        "capture": digest(data),
        "last_non_ff_offset": f"0x{last_non_ff:x}" if last_non_ff >= 0 else None,
        "trailing_ff_bytes": len(data) - last_non_ff - 1,
        "expansion_rom": {
            "images": images,
            "adjustment": f"0x{adjustment:x}",
        },
        "bit_p_token": {
            **{key: (f"0x{value:x}" if key.endswith("offset") else value)
               for key, value in p_token.items()},
            "table_relative": f"0x{table_relative:x}",
            "table_offset": f"0x{table_offset:x}",
        },
        "falcon_ucode_table": ucode_table,
        "production_fwsec_entry_index": production_index,
        "production_fwsec": fwsec,
        "scope": {
            "complete_first_512k": len(data) >= 0x80000,
            "production_fwsec_structurally_valid": True,
            "firmware_signature_cryptographically_verified": False,
            "safe_to_execute_inferred": False,
            "unlock_performed": False,
        },
    }
    if second is not None:
        result["cross_capture"] = {
            "second": digest(second),
            "same_size": len(second) == len(data),
            "byte_identical": second == data,
        }
    if efi_rom is not None:
        legacy_bytes = images[0]["bytes"]
        result["efi_rom_comparison"] = {
            "efi_rom": digest(efi_rom),
            "legacy_prefix_bytes": legacy_bytes,
            "legacy_prefix_identical":
                len(efi_rom) >= legacy_bytes and
                data[:legacy_bytes] == efi_rom[:legacy_bytes],
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prom", type=Path)
    parser.add_argument("--second-capture", type=Path)
    parser.add_argument("--efi-rom", type=Path)
    args = parser.parse_args()
    try:
        data = args.prom.read_bytes()
        second = (args.second_capture.read_bytes()
                  if args.second_capture else None)
        efi_rom = args.efi_rom.read_bytes() if args.efi_rom else None
        result = analyze(data, second=second, efi_rom=efi_rom)
    except (OSError, ParseError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
