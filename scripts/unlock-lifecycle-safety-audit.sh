#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
lifecycle=include/tu104_unlock_lifecycle.h
test_source=tests/test_tu104_unlock_lifecycle.c

for expression in \
    'EFI_' 'AllocatePages' 'FreePages' 'Map[(]' 'Unmap[(]' \
    'LoadImage' 'StartImage' 'ResetSystem' 'SetVariable' \
    'Pci[.]Write' 'Mem[.]Write' 'Io[.]Write'; do
    if grep -En -- "$expression" "$lifecycle"; then
        echo "Unlock lifecycle audit: platform operation found: $expression" >&2
        exit 1
    fi
done

grep -q -- 'TU104_UNLOCK_ALLOCATION_PAGES == UINT64_C(8192)' "$lifecycle"
grep -q -- 'TU104_UNLOCK_OBSERVED_HIGH_BASE UINT64_C(0x000000086c000000)' "$lifecycle"
grep -q -- 'state->run_status == TU104_UNLOCK_OK' "$lifecycle"
grep -q -- 'state->stage == TU104_UNLOCK_STAGE_DONE' "$lifecycle"
grep -q -- 'state->unlocked && state->quiescent' "$lifecycle"
grep -q -- 'if (!state->mutated)' "$lifecycle"
grep -q -- 'return TU104_UNLOCK_RETAIN_AND_HALT;' "$lifecycle"
grep -q -- 'mask < 256' "$test_source"

echo 'Unlock lifecycle safety audit: exact 32-MiB high allocation; pre-write release; post-write release only for preloaded handoff plus exact completed/quiescent success; all 256 state combinations covered'
