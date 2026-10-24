#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
wrapper=src/fwsec_frts_probe.inc
core=include/tu104_fwsec_probe_core.h
makefile=Makefile

for expression in \
    'Pci[.]Write' 'Io[.]Write' 'root->Map[(]' 'root->Unmap[(]' \
    'EfiPciOperationBusMaster' 'SetBarAttributes' \
    'EfiPciIoAttributeOperation(Set|Enable|Disable)' \
    'SetVariable' 'ResetSystem' 'LoadImage' 'StartImage' \
    'ExitBootServices'; do
    if grep -En -- "$expression" "$wrapper"; then
        echo "Active FRTS audit: forbidden operation found: $expression" >&2
        exit 1
    fi
done

# All active MMIO writes funnel through one callback, which repeats the
# tested core allowlist check before invoking EFI PCI Mem.Write.
test "$(grep -Ec -- 'target->Mem[.]Write' "$wrapper")" -eq 1
grep -q -- '!tu104_fwsec_probe_write_allowed(offset)' "$wrapper"
test "$(grep -Ec -- 'BS->AllocatePages' "$wrapper")" -eq 1
test "$(grep -Ec -- 'BS->FreePages' "$wrapper")" -eq 1
grep -q -- 'AllocateAddress' "$wrapper"
grep -q -- 'pages_allocated && !probe.mutated' "$wrapper"
grep -q -- 'EfiConventionalMemory, L"pre-allocation"' "$wrapper"
grep -q -- 'EfiBootServicesData, L"owned-post-allocation"' "$wrapper"

# The only post-run disposition retains and halts after every write attempt;
# the halt is non-returning and contains no retry/reset path.
grep -q -- '__attribute__((noreturn))' "$wrapper"
grep -q -- 'tu104_fwsec_page_disposition(probe.mutated)' "$wrapper"
grep -q -- 'TU104_FWSEC_RETAIN_AND_HALT' "$wrapper"
grep -q -- 'frts_retain_and_halt(&probe, core_status)' "$wrapper"
grep -q -- 'for (;;)' "$wrapper"
test "$(grep -Ec -- 'tu104_fwsec_probe_run[(]&probe[)]' "$wrapper")" -eq 1
test "$(grep -Ec -- 'BS->SetWatchdogTimer' "$wrapper")" -eq 1
grep -q -- 'Pre-write watchdog disable status' "$wrapper"

# Pin the one-shot ordering: confirmation, full immediate revalidation,
# watchdog disable, bounded read-only SEC2 scrub wait, and only then the
# tested core call.
test "$(grep -Ec -- 'status = frts_revalidate_platform[(]target[)]' "$wrapper")" -eq 2
confirm_line="$(grep -n -- 'status = frts_confirm_once[(][)]' "$wrapper" | cut -d: -f1)"
revalidate_line="$(grep -n -- 'status = frts_revalidate_platform[(]target[)]' "$wrapper" | tail -n 1 | cut -d: -f1)"
watchdog_line="$(grep -n -- 'BS->SetWatchdogTimer' "$wrapper" | cut -d: -f1)"
scrub_line="$(grep -n -- 'status = frts_wait_sec2_scrub[(]target[)]' "$wrapper" | cut -d: -f1)"
run_line="$(grep -n -- 'tu104_fwsec_probe_run[(]&probe[)]' "$wrapper" | cut -d: -f1)"
test "$confirm_line" -lt "$revalidate_line"
test "$revalidate_line" -lt "$watchdog_line"
test "$watchdog_line" -lt "$scrub_line"
test "$scrub_line" -lt "$run_line"
sed -n "${watchdog_line},$((watchdog_line + 4))p" "$wrapper" |
    grep -q -- 'if (EFI_ERROR(status))'
sed -n "${watchdog_line},$((watchdog_line + 4))p" "$wrapper" |
    grep -q -- 'goto cleanup;'
test "$(grep -Ec -- 'status = frts_wait_sec2_scrub[(]target[)]' "$wrapper")" -eq 1
grep -q -- 'TU104_SEC2_BASE + TU104_FALCON_DMACTL_OFFSET' "$wrapper"
grep -q -- 'index < TU104_SCRUB_POLL_LIMIT' "$wrapper"
grep -q -- '#define TU104_SCRUB_POLL_LIMIT 3000u' "$core"
grep -q -- '#define TU104_POLL_DELAY_US UINT32_C(1000)' "$core"
grep -q -- 'value & ~TU104_FALCON_DMACTL_SCRUBBING' "$wrapper"
grep -q -- 'value & TU104_FALCON_DMACTL_SCRUBBING' "$wrapper"
sed -n "${scrub_line},$((scrub_line + 3))p" "$wrapper" |
    grep -q -- 'if (EFI_ERROR(status))'
sed -n "${scrub_line},$((scrub_line + 3))p" "$wrapper" |
    grep -q -- 'goto cleanup;'

# Accidental unattended execution is impossible, and there is no Makefile
# install target for this first active image.
grep -q -- "key.UnicodeChar == L'u'" "$wrapper"
grep -q -- '#define FRTS_CONFIRM_TICKS 300u' "$wrapper"
grep -q -- 'Press U within 30 seconds' "$wrapper"
if grep -Eq -- '^install-fwsec|install-.*frts' "$makefile"; then
    echo 'Active FRTS audit: automatic USB install target is forbidden' >&2
    exit 1
fi

# Exact target artifacts and the single observed high allocation are pinned.
grep -q -- '#define FRTS_PROM_CRC UINT32_C(0x6fe8d04c)' "$wrapper"
grep -q -- '#define FRTS_ROOT_MAP_CRC32 UINT32_C(0x509162cd)' "$wrapper"
grep -q -- 'UINT32_C(0x19018086)' "$wrapper"
grep -q -- 'TU104_FWSEC_OBSERVED_HIGH_BASE' "$wrapper"
grep -q -- 'TU104_FWSEC_NATIVE_CRC32' "$wrapper"
grep -q -- 'TU104_GENERIC_BL_CRC32' "$wrapper"

# Reuse the stricter platform-free core audit as part of active-stage review.
bash scripts/platform-dma-probe-safety-audit.sh >/dev/null
bash scripts/fwsec-probe-core-safety-audit.sh >/dev/null

echo 'Active FRTS safety audit: validated bounded SEC2 scrub wait, one allowlisted GSP write callback; no DMA Map, SEC2/WPR/PCI/flash write, auto-run, retry, or USB install path'
