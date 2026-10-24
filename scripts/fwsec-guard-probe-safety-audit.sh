#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
wrapper=src/fwsec_guard_probe.inc
common=src/fwsec_frts_probe.inc
preflight=src/preflight.c

for expression in \
    'Pci[.]Write' 'target->Mem[.]Write' 'Io[.]Write' \
    'root->Map[(]' 'root->Unmap[(]' 'EfiPciOperationBusMaster' \
    'EfiPciIoOperationBusMaster' \
    'EfiPciIoAttributeOperation(Set|Enable|Disable)' \
    'SetBarAttributes' 'SetVariable' 'ResetSystem' \
    'LoadImage' 'StartImage' 'ExitBootServices' \
    'tu104_fwsec_probe_run[(]' 'tu104_fwsec_probe_write[(]' \
    'tu104_fwsec_probe_reset_gsp[(]' 'tu104_fwsec_probe_load[(]' \
    'tu104_fwsec_probe_start[(]' 'frts_confirm_once[(]'; do
    if grep -En -- "$expression" "$wrapper"; then
        echo "FWSEC guard audit: forbidden operation found: $expression" >&2
        exit 1
    fi
done

# The common active wrapper is included for its read-only platform/PROM/page
# guards.  Its active confirmation/write/run half must preprocess away in the
# diagnostic build.  The diagnostic's single watchdog-disable call is audited
# separately below because reproducing that boundary is required by 0.6.7.
for expression in 'target->Mem[.]Write' 'run_fwsec_frts_probe[(]'; do
    if gcc -E -P -DGNU_EFI_USE_MS_ABI \
            -I/usr/include/efi -I/usr/include/efi/x86_64 -Iinclude \
            -DTU104_FWSEC_GUARD_PROBE "$preflight" | \
            grep -En -- "$expression"; then
        echo "FWSEC guard audit: active path survived preprocessing: $expression" >&2
        exit 1
    fi
done

grep -q -- '#ifndef TU104_FWSEC_GUARD_PROBE' "$common"
grep -q -- '#endif /[*] !TU104_FWSEC_GUARD_PROBE [*]/' "$common"
grep -q -- '#define FWSEC_GUARD_CONTROL_COUNT 11u' "$wrapper"
grep -q -- '#define FWSEC_GUARD_DESCRIPTOR_DWORDS 15u' "$wrapper"
grep -q -- '#define FWSEC_GUARD_EXPECTED_READS' "$wrapper"
grep -q -- 'FWSEC_GUARD_EXPECTED_READS == 10197u' "$wrapper"
grep -q -- 'core_status = tu104_fwsec_probe_guards(&probe)' "$wrapper"
grep -q -- 'CORE-GUARD REPLAY SUMMARY' "$wrapper"
grep -q -- 'CORE-GUARD FIRST FAILURE' "$wrapper"
grep -q -- 'SEC2 scrub wait:' "$common"
grep -q -- 'status = frts_wait_sec2_scrub(target)' "$wrapper"
grep -q -- 'value & ~TU104_FALCON_DMACTL_SCRUBBING' "$common"
grep -q -- 'value & TU104_FALCON_DMACTL_SCRUBBING' "$common"
grep -q -- 'Guard-replay high-page release status' "$wrapper"
grep -q -- 'GPU-read-only core-guard replay complete status' "$wrapper"
grep -q -- 'probe.write32 = fwsec_guard_trace_write' "$wrapper"
test "$(grep -Ec -- '^static int fwsec_guard_trace_write[(]' "$wrapper")" -eq 1
test "$(grep -Ec -- 'probe.write32 = fwsec_guard_trace_write' "$wrapper")" -eq 1
test "$(grep -Ec -- 'BS->SetWatchdogTimer' "$wrapper")" -eq 1
test "$(grep -Ec -- 'status = fwsec_guard_confirm_once[(][)]' "$wrapper")" -eq 1
test "$(grep -Ec -- 'core_status = tu104_fwsec_probe_guards[(]&probe[)]' "$wrapper")" -eq 1

# The pure-memory preparation helper receives a fail-closed write stub.  The
# diagnostic invokes only the core guard helper, which performs reads/events;
# any unexpected write attempt is counted and rejected without hardware I/O.
grep -A12 -- '^static int fwsec_guard_trace_write' "$wrapper" |
    grep -q -- 'return 1;'

bash scripts/platform-dma-probe-safety-audit.sh >/dev/null
bash scripts/fwsec-probe-core-safety-audit.sh >/dev/null

echo 'FWSEC guard safety audit: bounded SEC2 scrub wait plus exact core-guard replay; GPU/PCI write, reset, firmware start, DMA Map, and flash paths absent; one post-confirmation firmware-watchdog disable pinned'
