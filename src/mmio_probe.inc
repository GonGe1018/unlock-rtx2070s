/* Included only by the explicitly selected TU104_MMIO_PROBE build. */
#include "pci_bar_resource.h"

#define PROM_WINDOW_OFFSET UINT64_C(0x00300000)
#define PROM_PROBE_BYTES 4096u
#define PROM_PROBE_EXPECTED_CRC UINT32_C(0xc4fabcb7)
#define PROM_PROBE_EXPECTED_FIRST_DWORD UINT32_C(0xeb77aa55)
#define PROM_PROBE_EXPECTED_BAR0 UINT32_C(0xa3000000)
#define PROM_PROBE_EXPECTED_BAR0_BYTES UINT64_C(0x01000000)

static EFI_STATUS save_prom_probe(const EFI_TIME *time,
                                  const UINT8 *data, UINTN size)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_SYSTEM_INFO *info = NULL;
    EFI_STATUS status;
    UINTN attempt;
    CHAR16 path[192];

    if (!time || !data || size != PROM_PROBE_BYTES || !boot_volume_device)
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
               L"\\TU104-UNLOCK\\CAPTURES\\PROMPROBE-%04d%02d%02d-"
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

    {
        UINTN written = size;

        status = uefi_call_wrapper(file->Write, 3, file, &written,
                                   (VOID *)data);
        if (!EFI_ERROR(status) && written != size)
            status = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(status))
            status = uefi_call_wrapper(file->Flush, 1, file);
    }
    if (!EFI_ERROR(status))
        note(L"Saved bounded PROM probe: %s bytes=%d\r\n", path, size);

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

static EFI_STATUS verify_probe_bar(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 config[CONFIG_DWORDS];
    VOID *descriptor = NULL;
    struct pci_bar_resource resource;
    EFI_STATUS status;
    BOOLEAN maximum_ok;

    status = read_config_header(target, config);
    if (EFI_ERROR(status))
        return status;
    note(L"MMIO probe config guard: id=%08x sub=%08x class=%08x "
         L"command=%04x BAR0=%08x\r\n",
         config[0], config[11], config[2],
         config[1] & 0xffffu, config[4]);
    if (!tu104_identity_matches(config[0], config[11], config[2]) ||
        (config[1] & 0xffffu) != 0x0007u ||
        config[4] != PROM_PROBE_EXPECTED_BAR0)
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
        resource.minimum != PROM_PROBE_EXPECTED_BAR0 || !maximum_ok ||
        resource.translation != 0 ||
        resource.length != PROM_PROBE_EXPECTED_BAR0_BYTES)
        status = EFI_UNSUPPORTED;
    else
        status = EFI_SUCCESS;
    FreePool(descriptor);
    return status;
}

static EFI_STATUS run_mmio_probe(EFI_PCI_IO_PROTOCOL *target,
                                 const EFI_TIME *time)
{
    UINT8 *first = NULL;
    UINT8 *second = NULL;
    UINT32 boot0_first = 0;
    UINT32 boot0_second = 0;
    UINT32 first_dword = 0;
    UINT32 first_crc;
    UINT32 second_crc;
    EFI_STATUS status;

    if (!target || !time || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    status = verify_probe_bar(target);
    note(L"MMIO BAR guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;

    note(L"BEGIN bounded MMIO probe: BOOT0 twice, PROM first 4096 bytes twice.\r\n");
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
    note(L"BOOT0 reads: first=%08x second=%08x status=%r\r\n",
         boot0_first, boot0_second, status);
    if (EFI_ERROR(status) || boot0_first != boot0_second ||
        boot0_first == 0 || boot0_first == 0xffffffffu)
        return EFI_COMPROMISED_DATA;

    first = AllocatePool(PROM_PROBE_BYTES);
    second = AllocatePool(PROM_PROBE_BYTES);
    if (!first || !second) {
        status = EFI_OUT_OF_RESOURCES;
        goto finished;
    }
    SetMem(first, PROM_PROBE_BYTES, 0xa5);
    SetMem(second, PROM_PROBE_BYTES, 0x5a);

    status = uefi_call_wrapper(target->Mem.Read, 6, target,
                               EfiPciIoWidthUint32, 0,
                               PROM_WINDOW_OFFSET,
                               PROM_PROBE_BYTES / sizeof(UINT32), first);
    if (EFI_ERROR(status))
        goto finished;
    status = uefi_call_wrapper(target->Mem.Read, 6, target,
                               EfiPciIoWidthUint32, 0,
                               PROM_WINDOW_OFFSET,
                               PROM_PROBE_BYTES / sizeof(UINT32), second);
    if (EFI_ERROR(status))
        goto finished;

    CopyMem(&first_dword, first, sizeof(first_dword));
    first_crc = option_rom_crc32((const uint8_t *)first, PROM_PROBE_BYTES);
    second_crc = option_rom_crc32((const uint8_t *)second, PROM_PROBE_BYTES);
    note(L"PROM probe results: first-dword=%08x crc1=%08x crc2=%08x "
         L"buffers-equal=%d expected-crc=%08x\r\n",
         first_dword, first_crc, second_crc,
         CompareMem(first, second, PROM_PROBE_BYTES) == 0,
         PROM_PROBE_EXPECTED_CRC);
    if (first_dword != PROM_PROBE_EXPECTED_FIRST_DWORD ||
        first_crc != PROM_PROBE_EXPECTED_CRC ||
        second_crc != PROM_PROBE_EXPECTED_CRC ||
        CompareMem(first, second, PROM_PROBE_BYTES) != 0) {
        status = EFI_CRC_ERROR;
        goto finished;
    }
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto finished;
    }
    status = save_prom_probe(time, first, PROM_PROBE_BYTES);

finished:
    if (first)
        FreePool(first);
    if (second)
        FreePool(second);
    note(L"END bounded MMIO probe status=%r\r\n", status);
    return status;
}
