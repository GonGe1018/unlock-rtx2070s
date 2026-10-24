#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
core=include/tu104_fwsec_probe_core.h
test_source=tests/test_tu104_fwsec_probe_core.c

# The reusable core must remain platform-free: all reads, writes, delays,
# barriers, and logs pass through the five injected callbacks.
for expression in \
    'uefi_call_wrapper' 'EFI_PCI' 'AllocatePages' 'FreePages' \
    'Map[(]' 'Unmap[(]' 'ResetSystem' 'LoadImage' 'StartImage' \
    'SetVariable' 'Pci[.]Write' 'Mem[.]Write'; do
    if grep -En -- "$expression" "$core"; then
        echo "FWSEC core audit: platform operation found: $expression" >&2
        exit 1
    fi
done

# There is one and only one raw write callback site, and mutation is marked
# before that callback because an error return cannot prove the write missed.
test "$(grep -Ec -- 'probe->write32[(]' "$core")" -eq 1
python3 - "$core" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
needle = "probe->mutated = 1;\n    return probe->write32("
if needle not in text:
    raise SystemExit("FWSEC core audit: mutation is not marked before write")
PY

# The allowlist is GSP-only and contains exactly the 13 loader/reset ports.
grep -q -- '!= TU104_GSP_BASE' "$core"
test "$(sed -n '/switch (relative)/,/default:/p' "$core" | \
    grep -Ec 'case TU104_FALCON_[A-Z0-9_]+_OFFSET:')" -eq 13
if sed -n '/tu104_fwsec_probe_write_allowed/,/^}/p' "$core" | \
        grep -Eq 'WPR2|SEC2'; then
    echo 'FWSEC core audit: forbidden WPR2/SEC2 write target in allowlist' >&2
    exit 1
fi

# The host test must retain exhaustive callback-boundary injection loops.
for field in fail_read fail_write fail_delay fail_barrier fail_event_stage; do
    grep -q -- "model.$field = index" "$test_source"
done
grep -q -- 'assert(probe.mutated == (model.writes != 0))' "$test_source"
grep -q -- 'TU104_FWSEC_STAGE_DONE' "$test_source"

echo 'FWSEC core safety audit: injected platform I/O only; 13 GSP write ports; WPR2/SEC2 writes and platform services absent'
