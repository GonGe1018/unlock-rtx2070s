#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/mmio_probe.inc

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
        echo "probe audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

read_calls=$(grep -Ec -- 'target->Mem[.]Read' "$source_file")
test "$read_calls" -eq 4
grep -q -- '#define PROM_WINDOW_OFFSET UINT64_C(0x00300000)' "$source_file"
grep -q -- '#define PROM_PROBE_BYTES 4096u' "$source_file"
grep -q -- '#define PROM_PROBE_EXPECTED_CRC UINT32_C(0xc4fabcb7)' "$source_file"
grep -q -- 'config\[4\] != PROM_PROBE_EXPECTED_BAR0' "$source_file"

echo 'probe safety audit: exactly four bounded reads; write paths absent'
