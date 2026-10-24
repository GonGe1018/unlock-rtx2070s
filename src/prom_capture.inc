/* Included only by the explicitly selected TU104_PROM_CAPTURE build. */
#include "pci_bar_resource.h"

#define PROM_WINDOW_OFFSET UINT64_C(0x00300000)
#define PROM_CAPTURE_BYTES 0x00080000u
#define PROM_CAPTURE_CHUNK_BYTES 0x00010000u
#define PROM_CAPTURE_CHUNKS 8u
#define PROM_CAPTURE_PASSES 2u
#define PROM_EXPECTED_BOOT0 UINT32_C(0x164000a1)
#define PROM_EXPECTED_FIRST_DWORD UINT32_C(0xeb77aa55)
#define PROM_EXPECTED_FIRST_4K_CRC UINT32_C(0xc4fabcb7)
#define PROM_EXPECTED_LEGACY_BYTES 0x0000ee00u
#define PROM_EXPECTED_LEGACY_CRC UINT32_C(0x2054ce3a)
#define PROM_EXPECTED_BAR0 UINT32_C(0xa3000000)
#define PROM_EXPECTED_BAR0_BYTES UINT64_C(0x01000000)

#if PROM_CAPTURE_BYTES != (PROM_CAPTURE_CHUNK_BYTES * PROM_CAPTURE_CHUNKS)
#error "PROM capture geometry must be exact"
#endif

static EFI_STATUS save_physical_prom(const EFI_TIME *time,
                                     const UINT8 *data, UINTN size)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_SYSTEM_INFO *info = NULL;
    EFI_STATUS status;
    UINTN attempt;
    UINTN offset = 0;
    CHAR16 path[192];

    if (!time || !data || size != PROM_CAPTURE_BYTES || !boot_volume_device)
        return EFI_INVALID_PARAMETER;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, boot_volume_device,
                               &simple_fs_guid, (VOID **)&file_system);
    if (EFI_ERROR(status) || !file_system)
        return EFI_NOT_FOUND;
    status = uefi_call_wrapper(file_system->OpenVolume, 2,
                               file_system, &root);
    if (EFI_ERROR(status) || !root)
        return status;

    info = LibFileSystemInfo(root);
    if (!info) {
        status = EFI_DEVICE_ERROR;
        goto finished;
    }
    if (info->ReadOnly || StrCmp(info->VolumeLabel, L"RTX2070S") != 0) {
        status = EFI_ACCESS_DENIED;
        goto finished;
    }

    for (attempt = 0; attempt < 100; ++attempt) {
        SPrint(path, sizeof(path),
               L"\\TU104-UNLOCK\\CAPTURES\\PROM512K-%04d%02d%02d-"
               L"%02d%02d%02d-%02d.BIN",
               time->Year, time->Month, time->Day,
               time->Hour, time->Minute, time->Second, attempt);
        status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                                   EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(file->Close, 1, file);
            file = NULL;
            continue;
        }
        if (status != EFI_NOT_FOUND)
            goto finished;
        status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                                       EFI_FILE_MODE_CREATE,
                                   0);
        break;
    }
    if (!file) {
        if (!EFI_ERROR(status))
            status = EFI_ACCESS_DENIED;
        goto finished;
    }

    while (offset < size) {
        UINTN chunk = size - offset;
        UINTN written;

        if (chunk > 4096u)
            chunk = 4096u;
        written = chunk;
        status = uefi_call_wrapper(file->Write, 3, file, &written,
                                   (VOID *)(data + offset));
        if (EFI_ERROR(status) || written != chunk) {
            if (!EFI_ERROR(status))
                status = EFI_DEVICE_ERROR;
            goto finished;
        }
        offset += written;
    }
    status = uefi_call_wrapper(file->Flush, 1, file);
    if (!EFI_ERROR(status))
        note(L"Saved verified 512 KiB PROM capture: %s bytes=%d\r\n",
             path, offset);

finished:
    if (file) {
        EFI_STATUS close_status =
            uefi_call_wrapper(file->Close, 1, file);
        if (!EFI_ERROR(status))
            status = close_status;
    }
    if (info)
        FreePool(info);
    if (root)
        uefi_call_wrapper(root->Close, 1, root);
    return status;
}

static EFI_STATUS verify_prom_capture_bar(EFI_PCI_IO_PROTOCOL *target)
{
    static const UINT32 expected_bars[7] = {
        UINT32_C(0xa3000000), UINT32_C(0x9000000c),
        UINT32_C(0x00000000), UINT32_C(0xa000000c),
        UINT32_C(0x00000000), UINT32_C(0x00003001),
        UINT32_C(0xa4000000)
    };
    UINT32 config[CONFIG_DWORDS];
    VOID *descriptor = NULL;
    struct pci_bar_resource resource;
    EFI_STATUS status;
    BOOLEAN maximum_ok;
    UINTN index;

    status = read_config_header(target, config);
    if (EFI_ERROR(status))
        return status;
    note(L"PROM capture config guard: id=%08x sub=%08x class=%08x "
         L"command=%04x BAR0=%08x ROM=%08x\r\n",
         config[0], config[11], config[2],
         config[1] & 0xffffu, config[4], config[12]);
    if (!tu104_identity_matches(config[0], config[11], config[2]) ||
        config[2] != UINT32_C(0x030000a1) ||
        (config[1] & 0xffffu) != 0x0007u)
        return EFI_UNSUPPORTED;
    for (index = 0; index < 6; ++index) {
        if (config[4 + index] != expected_bars[index])
            return EFI_UNSUPPORTED;
    }
    if (config[12] != expected_bars[6])
        return EFI_UNSUPPORTED;

    status = uefi_call_wrapper(target->GetBarAttributes, 4, target, 0,
                               NULL, &descriptor);
    note(L"BAR0 resource query status=%r pointer=%lx\r\n",
         status, (UINT64)(UINTN)descriptor);
    if (EFI_ERROR(status) || !descriptor)
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    if (!parse_pci_bar_resource((const uint8_t *)descriptor, 48,
                                &resource)) {
        FreePool(descriptor);
        return EFI_COMPROMISED_DATA;
    }

    note(L"BAR0 resource type=%d granularity=%ld min=%lx max=%lx "
         L"translation=%lx length=%lx\r\n",
         resource.resource_type, resource.granularity,
         resource.minimum, resource.maximum,
         resource.translation, resource.length);
    maximum_ok = resource.maximum == UINT64_C(0xa3ffffff) ||
                 resource.maximum == UINT64_C(0x00ffffff);
    if (resource.resource_type != 0 || resource.granularity != 32 ||
        resource.minimum != PROM_EXPECTED_BAR0 || !maximum_ok ||
        resource.translation != 0 ||
        resource.length != PROM_EXPECTED_BAR0_BYTES)
        status = EFI_UNSUPPORTED;
    else
        status = EFI_SUCCESS;
    FreePool(descriptor);
    return status;
}

static EFI_STATUS run_prom_capture(EFI_PCI_IO_PROTOCOL *target,
                                   const EFI_TIME *time)
{
    UINT8 *captures[PROM_CAPTURE_PASSES] = { NULL, NULL };
    UINT32 boot0_first = 0;
    UINT32 boot0_second = 0;
    UINT32 first_dword = 0;
    UINT32 first_4k_crc;
    UINT32 legacy_crc;
    UINT32 full_crc[PROM_CAPTURE_PASSES] = { 0, 0 };
    UINTN pass;
    UINTN chunk;
    UINTN first_difference = PROM_CAPTURE_BYTES;
    UINTN differing_bytes = 0;
    EFI_STATUS status;

    if (!target || !time || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    status = verify_prom_capture_bar(target);
    note(L"PROM capture BAR/config guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;

    if (!target->RomImage ||
        target->RomSize != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE ||
        !tu104_option_rom_header_matches(
            (const uint8_t *)target->RomImage,
            (size_t)target->RomSize, NULL) ||
        option_rom_crc32((const uint8_t *)target->RomImage,
                         (size_t)target->RomSize) != UINT32_C(0xbf35aca7)) {
        note(L"Firmware option-ROM guard failed; PROM capture refused.\r\n");
        return EFI_COMPROMISED_DATA;
    }

    note(L"BEGIN bounded PROM capture: two passes, eight 64 KiB reads per pass.\r\n");
    if (EFI_ERROR(flush_log()))
        return log_status;

    status = uefi_call_wrapper(target->Mem.Read, 6, target,
                               EfiPciIoWidthUint32, 0, 0, 1,
                               &boot0_first);
    if (EFI_ERROR(status))
        return status;
    status = uefi_call_wrapper(target->Mem.Read, 6, target,
                               EfiPciIoWidthUint32, 0, 0, 1,
                               &boot0_second);
    note(L"BOOT0 reads: first=%08x second=%08x expected=%08x status=%r\r\n",
         boot0_first, boot0_second, PROM_EXPECTED_BOOT0, status);
    if (EFI_ERROR(status) || boot0_first != PROM_EXPECTED_BOOT0 ||
        boot0_second != PROM_EXPECTED_BOOT0)
        return EFI_COMPROMISED_DATA;

    for (pass = 0; pass < PROM_CAPTURE_PASSES; ++pass) {
        captures[pass] = AllocatePool(PROM_CAPTURE_BYTES);
        if (!captures[pass]) {
            status = EFI_OUT_OF_RESOURCES;
            goto finished;
        }
        SetMem(captures[pass], PROM_CAPTURE_BYTES,
               pass == 0 ? 0xa5 : 0x5a);
    }

    for (pass = 0; pass < PROM_CAPTURE_PASSES; ++pass) {
        note(L"PROM pass %d begin.\r\n", pass + 1);
        for (chunk = 0; chunk < PROM_CAPTURE_CHUNKS; ++chunk) {
            UINTN offset = chunk * PROM_CAPTURE_CHUNK_BYTES;

            status = uefi_call_wrapper(target->Mem.Read, 6, target,
                                       EfiPciIoWidthUint32, 0,
                                       PROM_WINDOW_OFFSET + offset,
                                       PROM_CAPTURE_CHUNK_BYTES /
                                           sizeof(UINT32),
                                       captures[pass] + offset);
            if (EFI_ERROR(status)) {
                note(L"PROM pass=%d chunk=%d offset=%06x status=%r\r\n",
                     pass + 1, chunk, offset, status);
                goto finished;
            }
        }
        full_crc[pass] = option_rom_crc32(
            (const uint8_t *)captures[pass], PROM_CAPTURE_BYTES);
        note(L"PROM pass %d complete: bytes=%x CRC32=%08x\r\n",
             pass + 1, PROM_CAPTURE_BYTES, full_crc[pass]);
    }

    for (chunk = 0; chunk < PROM_CAPTURE_CHUNKS; ++chunk) {
        UINTN offset = chunk * PROM_CAPTURE_CHUNK_BYTES;
        UINT32 chunk_crc = option_rom_crc32(
            (const uint8_t *)captures[0] + offset,
            PROM_CAPTURE_CHUNK_BYTES);

        note(L"PROM chunk offset=%06x bytes=%x CRC32=%08x\r\n",
             offset, PROM_CAPTURE_CHUNK_BYTES, chunk_crc);
    }
    for (chunk = 0; chunk < PROM_CAPTURE_BYTES; ++chunk) {
        if (captures[0][chunk] != captures[1][chunk]) {
            if (first_difference == PROM_CAPTURE_BYTES)
                first_difference = chunk;
            ++differing_bytes;
        }
    }
    note(L"PROM pass comparison: differing-bytes=%d first-offset=%x "
         L"crc1=%08x crc2=%08x\r\n",
         differing_bytes, first_difference, full_crc[0], full_crc[1]);
    if (differing_bytes != 0 || full_crc[0] != full_crc[1]) {
        status = EFI_CRC_ERROR;
        goto finished;
    }

    CopyMem(&first_dword, captures[0], sizeof(first_dword));
    first_4k_crc = option_rom_crc32((const uint8_t *)captures[0], 4096u);
    legacy_crc = option_rom_crc32((const uint8_t *)captures[0],
                                  PROM_EXPECTED_LEGACY_BYTES);
    note(L"PROM prefix guards: first=%08x first4k-crc=%08x "
         L"legacy-bytes=%x legacy-crc=%08x legacy-equal-EFI=%d\r\n",
         first_dword, first_4k_crc, PROM_EXPECTED_LEGACY_BYTES,
         legacy_crc,
         CompareMem(captures[0], target->RomImage,
                    PROM_EXPECTED_LEGACY_BYTES) == 0);
    if (first_dword != PROM_EXPECTED_FIRST_DWORD ||
        first_4k_crc != PROM_EXPECTED_FIRST_4K_CRC ||
        legacy_crc != PROM_EXPECTED_LEGACY_CRC ||
        CompareMem(captures[0], target->RomImage,
                   PROM_EXPECTED_LEGACY_BYTES) != 0) {
        status = EFI_CRC_ERROR;
        goto finished;
    }
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto finished;
    }
    status = save_physical_prom(time, captures[0], PROM_CAPTURE_BYTES);

finished:
    for (pass = 0; pass < PROM_CAPTURE_PASSES; ++pass) {
        if (captures[pass])
            FreePool(captures[pass]);
    }
    note(L"END bounded PROM capture status=%r\r\n", status);
    return status;
}
