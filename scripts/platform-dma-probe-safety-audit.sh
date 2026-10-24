#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/platform_dma_probe.inc

forbidden=(
    'Pci[.]Wri''te'
    'Mem[.]Wri''te'
    'Io[.](Read|Wri''te)'
    'AllocatePages'
    'FreePages'
    'EfiPciOperationBusMaster'
    'EfiPciIoOperationBusMaster'
    'EfiPciIoAttributeOperation(Set|Enable|Disable)'
    'SetBarAttributes'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "Platform DMA probe audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

test "$(grep -Ec -- 'root->Mem[.]Read' "$source_file")" -eq 2
test "$(grep -Ec -- 'root->Pci[.]Read' "$source_file")" -eq 2
grep -q -- 'MCHBAR VT-d2 READ ONLY' "$source_file"
grep -q -- 'VT-d2 status READ ONLY' "$source_file"
grep -q -- 'function is NOT invoked or patched' "$source_file"
grep -q -- 'no VT-d engine MMIO was read' "$source_file"
grep -q -- '#define TU104_MCHBAR_VTD2_OFFSET UINT64_C(0x5410)' \
    include/platform_dma_policy.h
grep -q -- '#define TU104_EXPECTED_VTD2_BASE UINT64_C(0xfed91000)' \
    include/platform_dma_policy.h

echo 'Platform DMA probe safety audit: bounded CPU/config/chipset reads; no DMA, GPU BAR access, or hardware write path'
