#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/falcon_state_probe.inc

forbidden=(
    'Pci[.]Wri''te'
    'Mem[.]Wri''te'
    'Io[.](Read|Wri''te)'
    '[-][>]Map[(]'
    '[-][>]Unmap[(]'
    'AllocatePages'
    'FreePages'
    'AllocateBuffer'
    'SetBarAttributes'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "Falcon-state audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

read_sites=$(grep -Ec -- 'target->Mem[.]Read' "$source_file")
test "$read_sites" -eq 3
grep -q -- '#define FALCON_STATE_PASSES 2u' "$source_file"
grep -q -- '#define FALCON_STATE_COUNT 26u' "$source_file"
grep -q -- '#define FALCON_STATE_EXPECTED_BOOT0 UINT32_C(0x164000a1)' "$source_file"
test "$(grep -Ec -- '^    \{ "[A-Z0-9_]+",[[:space:]]+UINT32_C\(0x[0-9a-f]+\) \},$' "$source_file")" -eq 26

echo 'Falcon-state safety audit: 54 bounded dword reads; write/DMA/reset paths absent'
