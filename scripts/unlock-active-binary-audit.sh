#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
image=build/TU104UNLOCK.EFI
elf=build/tu104_unlock_active.so
v67_binary="${V67_BINARY:-references/xrip__cmp50hx-unlock/efi-unlock/blobs/v67_payload.bin}"
boot_container_binary="${BOOT_CONTAINER_BINARY:-references/xrip__cmp50hx-unlock/efi-unlock/blobs/gsp_rm_boot_dbg.bin}"
audit_tmp="$(mktemp -d)"
trap 'rm -rf -- "$audit_tmp"' EXIT

test -s "$image"
test -s "$elf"
file "$image" | grep -q -- 'PE32+ executable (EFI application) x86-64'
objdump -p "$image" | grep -q -- $'Subsystem[[:space:]]*0000000a[[:space:]]*(EFI application)'
objdump -p "$image" | grep -q -- \
    'Entry 1 0000000000000000 00000000 Import Directory'
test -z "$(nm -u "$elf")"

objcopy --dump-section .data="$audit_tmp/data.bin" "$elf"
data_vma_hex="$(readelf -SW "$elf" | awk '$3 == ".data" {print $5}')"
test -n "$data_vma_hex"
data_vma=$((16#$data_vma_hex))

check_blob() {
    local source_path="$1"
    local symbol_base="$2"
    local start_hex end_hex start end expected actual offset

    start_hex="$(nm -n "$elf" | awk -v name="${symbol_base}_start" \
        '$3 == name {print $1}')"
    end_hex="$(nm -n "$elf" | awk -v name="${symbol_base}_end" \
        '$3 == name {print $1}')"
    test -n "$start_hex" && test -n "$end_hex"
    start=$((16#$start_hex))
    end=$((16#$end_hex))
    expected="$(stat -c '%s' "$source_path")"
    actual=$((end - start))
    offset=$((start - data_vma))
    test "$actual" -eq "$expected"
    test "$offset" -ge 0
    dd if="$audit_tmp/data.bin" of="$audit_tmp/blob.bin" bs=1 \
        skip="$offset" count="$actual" status=none
    cmp -s -- "$source_path" "$audit_tmp/blob.bin"
}

check_blob build/tu10x-assets/bl_gsp_tu102.bin \
    _binary_bl_gsp_tu102_bin
check_blob build/tu10x-assets/booter_ucode_prod.bin \
    _binary_booter_ucode_prod_bin
check_blob build/tu10x-assets/booter_sig_prod.bin \
    _binary_booter_sig_prod_bin
check_blob "$v67_binary" \
    _binary_v67_payload_bin
check_blob "$boot_container_binary" \
    _binary_gsp_rm_boot_dbg_bin

strings -el "$image" >"$audit_tmp/utf16-strings.txt"
grep -q -- '0.8.0-TU104-VOLATILE-UNLOCK-CANDIDATE' \
    "$audit_tmp/utf16-strings.txt"
grep -q -- 'Press U within 30 seconds' "$audit_tmp/utf16-strings.txt"
grep -q -- 'FULLY POWER OFF' "$audit_tmp/utf16-strings.txt"
grep -q -- 'SUCCESS: volatile TU104 unlock verified' \
    "$audit_tmp/utf16-strings.txt"
grep -q -- 'Starting exact Ubuntu GRUB now' "$audit_tmp/utf16-strings.txt"

echo 'Active unlock binary audit: PE32+ EFI image has no imports; all five embedded assets are byte-exact; confirmation, fail-closed, success, and Ubuntu-handoff contracts are present'
