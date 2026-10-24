#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/prom_capture.inc

forbidden=(
    'Pci[.]Wri''te'
    'Mem[.]Wri''te'
    'Io[.](Read|Wri''te)'
    '[-][>]Map[(]'
    'AllocateBuffer'
    'SetBarAttributes'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "PROM capture audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

read_sites=$(grep -Ec -- 'target->Mem[.]Read' "$source_file")
test "$read_sites" -eq 3
grep -q -- '#define PROM_WINDOW_OFFSET UINT64_C(0x00300000)' "$source_file"
grep -q -- '#define PROM_CAPTURE_BYTES 0x00080000u' "$source_file"
grep -q -- '#define PROM_CAPTURE_CHUNK_BYTES 0x00010000u' "$source_file"
grep -q -- '#define PROM_CAPTURE_CHUNKS 8u' "$source_file"
grep -q -- '#define PROM_CAPTURE_PASSES 2u' "$source_file"
grep -q -- '#define PROM_EXPECTED_BOOT0 UINT32_C(0x164000a1)' "$source_file"
grep -q -- 'config\[12\] != expected_bars\[6\]' "$source_file"

echo 'PROM capture safety audit: 18 bounded runtime reads; write paths absent'
