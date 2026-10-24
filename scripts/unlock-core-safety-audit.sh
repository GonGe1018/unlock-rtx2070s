#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
core=include/tu104_unlock_core.h
layout=include/tu104_unlock_layout.h
test_source=tests/test_tu104_unlock_core.c

# The reusable state machine stays platform-free. Every hardware operation is
# injected so the exact same sequence can be exercised by the host model.
for expression in \
    'uefi_call_wrapper' 'EFI_PCI' 'AllocatePages' 'FreePages' \
    'Map[(]' 'Unmap[(]' 'ResetSystem' 'LoadImage' 'StartImage' \
    'SetVariable' 'Pci[.]Write' 'Mem[.]Write' 'Io[.]Write'; do
    if grep -En -- "$expression" "$core" "$layout"; then
        echo "Unlock core audit: platform operation found: $expression" >&2
        exit 1
    fi
done

# One raw write callback site exists, and uncertain writes mark mutation first.
test "$(grep -Ec -- 'core->write32[(]' "$core")" -eq 1
python3 - "$core" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
needle = "core->mutated = 1;\n    return core->write32("
if needle not in text:
    raise SystemExit("Unlock core audit: mutation is not marked before write")
PY

# The full path adds only SEC2 and its two mailboxes to the earlier Falcon
# port set. Host-side WPR2/selector writes remain structurally impossible.
allowlist="$(sed -n '/tu104_unlock_write_allowed/,/^}/p' "$core")"
test "$(grep -Ec 'case TU104_(UNLOCK|FALCON)_[A-Z0-9_]+_OFFSET:' <<<"$allowlist")" -eq 15
grep -q -- 'base != TU104_GSP_BASE && base != TU104_SEC2_BASE' <<<"$allowlist"
if grep -Eq -- 'WPR2|SS0|SS1|PLM' <<<"$allowlist"; then
    echo 'Unlock core audit: selector/WPR/PLM target entered write allowlist' >&2
    exit 1
fi
for forbidden in \
    'tu104_unlock_write[(][^;]*TU104_UNLOCK_SS0_OFFSET' \
    'tu104_unlock_write[(][^;]*TU104_UNLOCK_SS1_OFFSET' \
    'tu104_unlock_write[(][^;]*TU104_WPR2_LO_OFFSET' \
    'tu104_unlock_write[(][^;]*TU104_WPR2_HI_OFFSET' \
    'tu104_unlock_write[(][^;]*TU104_UNLOCK_PLM_OFFSET'; do
    if grep -En -- "$forbidden" "$core"; then
        echo "Unlock core audit: forbidden direct write: $forbidden" >&2
        exit 1
    fi
done

# Exact target assets and 8-GiB framebuffer geometry stay pinned.
grep -q -- 'TU104_UNLOCK_BOOTER_CRC32 UINT32_C(0x00568187)' "$layout"
grep -q -- 'TU104_UNLOCK_V67_CRC32 UINT32_C(0xf4a4754c)' "$layout"
grep -q -- 'TU104_UNLOCK_FB_BYTES UINT64_C(0x0000000200000000)' "$layout"
grep -q -- 'TU104_UNLOCK_FRTS_OFFSET UINT64_C(0x00000001ffee0000)' "$layout"
grep -q -- 'TU104_UNLOCK_HEAP_BYTES UINT64_C(0x0000000006900000)' "$layout"
grep -q -- 'TU104_UNLOCK_POOL_BYTES UINT64_C(0x02000000)' "$layout"

# Runtime invariants: one Booter start, exact result tuple, cold WPR2, then
# both engines reset before quiescence. There is no active retry loop.
test "$(grep -Ec -- 'tu104_unlock_start_falcon[(]core, TU104_SEC2_BASE[)]' "$core")" -eq 1
grep -q -- 'core->ss0_result != TU104_UNLOCK_SS0_RESULT' "$core"
grep -q -- 'core->ss1_result != TU104_UNLOCK_SS1_RESULT' "$core"
grep -q -- 'wpr2_lo != TU104_UNLOCK_COLD_WPR2_LO' "$core"
cleanup_line="$(grep -n -- 'TU104_UNLOCK_STAGE_CLEANUP' "$core" | tail -n 1 | cut -d: -f1)"
quiescent_line="$(grep -n -- 'core->quiescent = 1' "$core" | cut -d: -f1)"
test "$cleanup_line" -lt "$quiescent_line"
sed -n "${cleanup_line},${quiescent_line}p" "$core" |
    grep -q -- 'tu104_unlock_reset_falcon(core, TU104_SEC2_BASE)'
sed -n "${cleanup_line},${quiescent_line}p" "$core" |
    grep -q -- 'tu104_unlock_reset_falcon(core, TU104_GSP_BASE)'

# The model independently refuses forbidden addresses, checks all result
# classes, and injects every new Booter-load write and post-FRTS read failure.
grep -q -- 'boundary <= direct_load_writes' "$test_source"
grep -q -- 'boundary <= successful_reads' "$test_source"
grep -q -- 'boundary <= successful_delays' "$test_source"
grep -q -- 'boundary <= successful_barriers' "$test_source"
grep -q -- 'stage < TU104_UNLOCK_STAGE_COUNT' "$test_source"
grep -q -- 'offset != TU104_UNLOCK_SS0_OFFSET' "$test_source"
grep -q -- 'offset != TU104_WPR2_LO_OFFSET' "$test_source"

echo 'Unlock core safety audit: platform-free one-shot state machine; 15 Falcon-only ports; direct selector/WPR/flash writes absent; exhaustive new boundary injection retained'
