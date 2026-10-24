#!/usr/bin/env python3
"""Extract and verify the pinned TU10x boot assets from NVIDIA 610.57.04."""
from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
import zlib
from pathlib import Path


SOURCE_FILES = {
    "booter": (
        "g_bindata_kgspGetBinArchiveBooterLoadUcode_TU102.c",
        "e495483adf66798502667da137c4a24c55f471b86f3183020e3169ff88ad6c89",
    ),
    "generic": (
        "g_bindata_ksec2GetBinArchiveBlUcode_TU102.c",
        "fc0bc277eeee754ea3071ae0fe8360a52193565bba7c07dcea60b00e8d56d9c8",
    ),
    "gsp_boot": (
        "g_bindata_kgspGetBinArchiveGspRmBoot_TU102.c",
        "19628e0b148a5437b351ce9ecd4a04405ec4eb66521e1c318ca6ef5d1a0c2efe",
    ),
}

EXPECTED_OUTPUTS = {
    "booter_ucode_prod.bin": (59136, "e0f0fc93da097b5b68e5af3adb58b2235ed1fad5fce0b23592d3895af44cb743"),
    "booter_header_prod.bin": (36, "be56c8c4c07dfd9a44e3d52c6d092ef2baacffa7303a16bc11f5f4a8485dd4d6"),
    "booter_sig_prod.bin": (16, "eac2f717515ed7b336d2bd26215757d6ea73b87fdcfe09c67ceb07ae75a9c9b6"),
    "booter_patch_loc.bin": (4, "67afaacd65d13ffecc00dcb47ecac2b278a575f4fc8a2109f756ae5004e72bfd"),
    "booter_patch_meta.bin": (12, "bb1e37791d729f4e30f3d80e459779205111dd38250bc832d360b584804a3423"),
    "bl_gsp_tu102.bin": (768, "f21f1cfbb8fffa8067685314367fc4c50f3f84dd99eda7c19db4292d9269b9d9"),
    "gsp_rm_boot_tu102.bin": (4096, "8765694cd38beaa4e2b8b1b83a31fb7aee6cb9d8b7c6bc7a86e6d931a3a056c2"),
    "gsp_rm_boot_tu102_desc.bin": (76, "9e7cca2cfdc5ce601b0adba39967850046108df6c2d23fff95578ca3f0ccbe01"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract_array(source: str, label: str) -> bytes:
    marker = f"BINDATA_LABEL_{label}_data[]"
    if source.count(marker) != 1:
        raise ValueError(f"missing or duplicate array: {label}")
    start = source.index(marker)
    metadata = source[max(0, start - 600) : start]
    compression = re.findall(r"COMPRESSION: (YES|NO)", metadata)
    sizes = re.findall(r"DATA SIZE \(bytes\): (\d+)", metadata)
    if not compression or not sizes:
        raise ValueError(f"missing numeric storage metadata: {label}")
    expected = int(sizes[-1])
    if not 0 < expected <= 16 * 1024 * 1024:
        raise ValueError(f"invalid expanded size: {label}")
    declaration = re.match(
        re.escape(marker) + r"\s*=\s*\{([^}]*)\};", source[start:], re.S
    )
    if declaration is None:
        raise ValueError(f"malformed byte array: {label}")
    body = declaration.group(1).strip().rstrip(",")
    tokens = body.split(",") if body else []
    if any(re.fullmatch(r"\s*0x[0-9a-fA-F]{2}\s*", token) is None for token in tokens):
        raise ValueError(f"non-byte initializer: {label}")
    stored = bytes(int(token, 16) for token in tokens)

    if compression[-1] == "YES":
        compressed_sizes = re.findall(r"COMPRESSED SIZE \(bytes\): (\d+)", metadata)
        if not compressed_sizes or len(stored) != int(compressed_sizes[-1]):
            raise ValueError(f"compressed size mismatch: {label}")
        inflater = zlib.decompressobj(-15)
        data = inflater.decompress(stored, expected + 1)
        if (
            not inflater.eof
            or inflater.unconsumed_tail
            or len(inflater.unused_data) != 8
        ):
            raise ValueError(f"invalid raw-DEFLATE/trailer geometry: {label}")
        crc32, expanded_size = struct.unpack("<II", inflater.unused_data)
        if expanded_size != expected or crc32 != zlib.crc32(data):
            raise ValueError(f"DEFLATE trailer mismatch: {label}")
    else:
        data = stored
    if len(data) != expected:
        raise ValueError(f"expanded size mismatch: {label}")
    return data


def _load_pinned_sources(generated: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, (filename, digest) in SOURCE_FILES.items():
        raw = (generated / filename).read_bytes()
        if sha256(raw) != digest:
            raise ValueError(f"610.57.04 source hash mismatch: {filename}")
        result[key] = raw.decode("ascii")
    return result


def extract_assets(generated: Path) -> dict[str, bytes]:
    sources = _load_pinned_sources(generated)
    assets = {
        "booter_ucode_prod.bin": extract_array(sources["booter"], "IMAGE_PROD"),
        "booter_header_prod.bin": extract_array(sources["booter"], "HEADER_PROD"),
        "booter_sig_prod.bin": extract_array(sources["booter"], "SIG_PROD"),
        "booter_patch_loc.bin": extract_array(sources["booter"], "PATCH_LOC"),
        "booter_patch_meta.bin": extract_array(sources["booter"], "PATCH_META"),
        "bl_gsp_tu102.bin": extract_array(sources["generic"], "UCODE_IMAGE"),
        "gsp_rm_boot_tu102.bin": extract_array(sources["gsp_boot"], "UCODE_IMAGE"),
        "gsp_rm_boot_tu102_desc.bin": extract_array(sources["gsp_boot"], "UCODE_DESC"),
    }
    for name, (size, digest) in EXPECTED_OUTPUTS.items():
        data = assets[name]
        if len(data) != size or sha256(data) != digest:
            raise ValueError(f"derived asset mismatch: {name}")

    header = struct.unpack("<9I", assets["booter_header_prod.bin"])
    if header != (0, 0x100, 0x8500, 0x6200, 1, 0x100, 0x8400, 0x100, 0):
        raise ValueError("unexpected TU10x production Booter geometry")
    if struct.unpack("<I", assets["booter_patch_loc.bin"])[0] != 0x8700:
        raise ValueError("unexpected TU10x signature patch location")
    if struct.unpack("<3I", assets["booter_patch_meta.bin"]) != (0, 1, 13):
        raise ValueError("unexpected TU10x production patch metadata")
    return assets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "generated",
        type=Path,
        help="NVIDIA 610.57.04 src/nvidia/generated directory",
    )
    parser.add_argument("--output", type=Path, help="optional output directory")
    args = parser.parse_args()
    assets = extract_assets(args.generated)
    if args.output is not None:
        args.output.mkdir(parents=True, exist_ok=True)
        for name, data in assets.items():
            (args.output / name).write_bytes(data)
    for name, data in assets.items():
        print(f"{sha256(data)}  {len(data):7d}  {name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, zlib.error) as error:
        sys.exit(f"extract failed: {error}")
