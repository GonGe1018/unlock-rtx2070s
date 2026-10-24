#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/dma_map_probe.inc

forbidden=(
    'Pci[.]Wri''te'
    'Mem[.](Read|Wri''te)'
    'Io[.](Read|Wri''te)'
    'CopyMem'
    'SetBarAttributes'
    'EfiPciIoAttributeOperation(Set|Enable|Disable)'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "DMA map probe audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

test "$(grep -Ec -- 'target->AllocateBuffer' "$source_file")" -eq 1
test "$(grep -Ec -- 'target->Map' "$source_file")" -eq 1
test "$(grep -Ec -- 'target->Unmap' "$source_file")" -eq 1
test "$(grep -Ec -- 'target->FreeBuffer' "$source_file")" -eq 1
grep -q -- 'EfiPciIoOperationBusMasterCommonBuffer' "$source_file"
grep -q -- 'EfiPciIoAttributeOperationSupported' "$source_file"
grep -q -- 'EfiPciIoAttributeOperationGet' "$source_file"
grep -q -- 'allocate_attributes = 0' "$source_file"
grep -q -- 'DMA_MAP_EXPECTED_CURRENT_ATTRIBUTES UINT64_C(0x00000718)' "$source_file"
grep -q -- 'DMA_MAP_EXPECTED_SUPPORTED_ATTRIBUTES UINT64_C(0x0001277b)' "$source_file"
grep -q -- '#define TU104_DMA_PROBE_BYTES UINT64_C(0x02000000)' include/dma_map_policy.h
grep -q -- 'actual GPU DMA reachability was NOT tested' "$source_file"

echo 'DMA map probe safety audit: one LOW32 32 MiB Map/Unmap lifecycle; no device transfer or hardware write path'
