#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/high_dma_map_probe.inc

forbidden=(
    'Pci[.]Wri''te'
    'Mem[.](Read|Wri''te)'
    'Io[.](Read|Wri''te)'
    'CopyMem'
    'SetBarAttributes'
    'EfiPciIoAttributeOperation(Set|Enable|Disable)'
    'SetAttributes'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
    'EfiPciOperationBusMasterWrite'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "High DMA map probe audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

test "$(grep -Ec -- 'BS->AllocatePages' "$source_file")" -eq 1
test "$(grep -Ec -- 'BS->FreePages' "$source_file")" -eq 1
test "$(grep -Ec -- 'root->Map' "$source_file")" -eq 1
test "$(grep -Ec -- 'root->Unmap' "$source_file")" -eq 1
grep -q -- 'EfiPciOperationBusMasterRead64' "$source_file"
grep -q -- 'AllocateAddress' "$source_file"
grep -q -- 'Fresh EFI IOMMU guard' "$source_file"
grep -q -- 'device address was never programmed into the GPU' "$source_file"
grep -q -- '#define TU104_DMA_PROBE_BYTES UINT64_C(0x02000000)' include/dma_map_policy.h

echo 'High DMA map probe safety audit: one HIGH64 Read64 Map/Unmap lifecycle; no device transfer or hardware write path'
