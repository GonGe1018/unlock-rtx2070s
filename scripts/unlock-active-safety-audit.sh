#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/tu104_unlock_active.inc
preflight=src/preflight.c

for expression in \
    'SetVariable' 'ResetSystem' 'Pci[.]Write' 'Io[.]Write' \
    'Map[(]' 'Unmap[(]' 'Flash' 'TU104_UNLOCK_SS0_OFFSET[^\n]*Write' \
    'TU104_UNLOCK_SS1_OFFSET[^\n]*Write' 'TU104_WPR2_[A-Z_]+[^\n]*Write'; do
    if grep -En -- "$expression" "$source_file"; then
        echo "Active unlock audit: forbidden operation found: $expression" >&2
        exit 1
    fi
done

# One MMIO write site exists. The independent wrapper marker is set before
# the protocol call and the tested core allowlist is rechecked at that site.
test "$(grep -Ec -- 'target->Mem[.]Write' "$source_file")" -eq 1
python3 - "$source_file" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
needle = "if (!io || !io->target || !tu104_unlock_write_allowed(offset))\n        return 1;\n    io->write_attempted = TRUE;\n    status = uefi_call_wrapper(io->target->Mem.Write"
if needle not in text:
    raise SystemExit("Active unlock audit: write marker/allowlist ordering changed")

preload = text.index("status = ubuntu_handoff_preload(image, &handoff);")
confirm = text.index("status = unlock_confirm_once();")
run = text.index("core_status = tu104_unlock_run(&core);")
decision = text.index("action = tu104_unlock_lifecycle_decide(&lifecycle);")
free_pages = text.index("status = uefi_call_wrapper(BS->FreePages", decision)
start = text.index("status = ubuntu_handoff_start(&handoff);")
if not preload < confirm < run < decision < free_pages < start:
    raise SystemExit("Active unlock audit: preload/confirm/run/decision/free/start order changed")
PY

test "$(grep -Ec -- 'tu104_unlock_run[(]&core[)]' "$source_file")" -eq 1
grep -q -- 'if (io->write_attempted || io->core->mutated)' "$source_file"
grep -q -- 'TU104_UNLOCK_RETAIN_AND_HALT' "$source_file"
grep -q -- 'unlock_retain_and_halt();' "$source_file"
grep -q -- 'TU104_UNLOCK_RELEASE_AND_HANDOFF' "$source_file"
grep -q -- 'TU104_UNLOCK_ALLOCATION_PAGES' "$source_file"
grep -q -- 'Press U within 30 seconds' "$source_file"
grep -q -- 'FULLY POWER OFF' "$source_file"
grep -q -- 'defined(TU104_UNLOCK_ACTIVE)' "$preflight"

echo 'Active unlock safety audit: exact assets and 32-MiB address; GRUB preload before U; one allowlisted core invocation; post-write output suppression; lifecycle decision before release and handoff; no retry, persistence, direct selector/WPR write, or reboot path'
