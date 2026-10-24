#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
wrapper=src/ubuntu_handoff_probe.inc
profile=include/ubuntu_handoff_profile.h
makefile=Makefile

for expression in \
    'Pci[.]Write' 'Mem[.]Write' 'Io[.]Write' \
    'AllocatePages' 'FreePages' 'Map[(]' 'Unmap[(]' \
    'SetBarAttributes' 'SetVariable' 'ResetSystem' 'ExitBootServices' \
    'file->Write' 'root->Write' 'EFI_FILE_MODE_WRITE' \
    'EFI_FILE_MODE_CREATE'; do
    if grep -En -- "$expression" "$wrapper"; then
        echo "Ubuntu handoff audit: forbidden operation found: $expression" >&2
        exit 1
    fi
done

test "$(grep -Ec -- 'BS->LoadImage' "$wrapper")" -eq 1
test "$(grep -Ec -- 'BS->StartImage' "$wrapper")" -eq 1
grep -Fq -- 'L"\\EFI\\ubuntu\\grubx64.efi"' "$wrapper"
grep -Fq -- 'L"\\EFI\\ubuntu\\grub.cfg"' "$wrapper"
grep -q -- 'matches != 1' "$wrapper"
grep -q -- 'loaded->ParentHandle != parent_image' "$wrapper"
grep -q -- 'loaded->DeviceHandle != volume' "$wrapper"
grep -q -- 'loaded->ImageCodeType != EfiLoaderCode' "$wrapper"
grep -q -- 'loaded->ImageDataType != EfiLoaderData' "$wrapper"
grep -q -- 'loaded->ImageSize != UBUNTU_GRUB_IMAGE_BYTES' "$wrapper"

grep -q -- 'UBUNTU_GRUB_BYTES ((size_t)2660232u)' "$profile"
grep -q -- 'UBUNTU_GRUB_CRC32 UINT32_C(0x9b31e4cd)' "$profile"
grep -q -- 'UBUNTU_GRUB_CFG_BYTES ((size_t)126u)' "$profile"
grep -q -- 'UBUNTU_GRUB_CFG_CRC32 UINT32_C(0x51c36111)' "$profile"
grep -q -- 'fc61af23-4f55-4ce9-9df1-dc547db5442d' "$profile"

load_line="$(grep -n -- 'BS->LoadImage' "$wrapper" | cut -d: -f1)"
start_line="$(grep -n -- 'BS->StartImage' "$wrapper" | cut -d: -f1)"
close_line="$(grep -n -- 'log_file->Close' "$wrapper" | cut -d: -f1)"
test "$load_line" -lt "$close_line"
test "$close_line" -lt "$start_line"

if grep -Eq -- '^install-.*handoff' "$makefile"; then
    echo 'Ubuntu handoff audit: automatic USB install target is forbidden' >&2
    exit 1
fi

echo 'Ubuntu handoff safety audit: exact single ESP image/config profile; preload-before-start; no GPU, persistence, boot-variable, or filesystem write path'
