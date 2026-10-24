/*
 * RTX 2070 SUPER / TU104 board-specific, read-only UEFI preflight.
 *
 * Normal/ROMCAP permitted hardware operations:
 *   - enumerate EFI PCI I/O handles;
 *   - read the first 64 bytes of PCI configuration space;
 *   - query whether the EFI IOMMU protocol is present.
 *
 * Deliberately absent from those builds:
 *   - PCI configuration writes;
 *   - BAR or other GPU MMIO access;
 *   - DMA mapping/allocation through PCI protocols;
 *   - GPU reset, firmware execution, or direct flash access;
 *   - boot-variable changes or automatic operating-system launch.
 *
 * The normal build writes only a text log. The separately selected ROMCAP
 * build may also copy EFI_PCI_IO_PROTOCOL.RomImage, a firmware-provided RAM
 * buffer, to that same FAT volume. Neither build reads the GPU ROM BAR.
 * The separately audited MMIO-PROBE build adds exactly two BOOT0 reads and
 * two 4 KiB PROM-window reads. The PROM-CAPTURE build adds two BOOT0 reads
 * and two passes of eight 64 KiB PROM-window reads. Neither contains a
 * hardware write path. The REGISTER-PROBE build adds two BOOT0 reads and
 * two passes over twelve named status registers, also without writes. The
 * DMA-MAP-PROBE build adds one temporary 32 MiB EFI PCI common-buffer
 * Map/Unmap lifecycle, but never gives the returned address to the GPU and
 * contains no device DMA command. The separately selected HIGH-DMA-MAP build
 * claims 32 MiB above 4 GiB and performs one parent-root Read64 Map/Unmap
 * lifecycle, again without giving the address to the GPU.
 * The PLATFORM-DMA-PROBE build adds only CPU/Setup, host-bridge, MCHBAR,
 * and bounded VT-d status reads.  It invokes no DMA service and performs no
 * GPU BAR access or hardware write.
 * The FALCON-STATE-PROBE build adds two BOOT0 reads and two passes over 26
 * named GSP/SEC2 control-state registers.  It does not read IMEM/DMEM ports.
 * The separately selected FWSEC-FRTS build is the first active stage.  It
 * requires a physical key confirmation, runs only the native signed FRTS
 * command, and permanently holds in UEFI after its first GPU write attempt.
 * The FWSEC-GUARD build replays the complete active pre-mutation sequence and
 * invokes only the core guard function with a trace-only read callback.  It
 * contains no GPU write callback and cannot enter reset or firmware execution.
 * The separately selected UNLOCK-ACTIVE build preloads the exact Ubuntu GRUB
 * image, requires physical confirmation, executes the target-pinned volatile
 * one-shot core, and hands off only after exact success and Falcon quiescence.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
#include <stdarg.h>

#include "option_rom.h"
#include "tu104_profile.h"

#if (defined(TU104_CAPTURE_ROM) + defined(TU104_MMIO_PROBE) + \
     defined(TU104_PROM_CAPTURE) + defined(TU104_REGISTER_PROBE) + \
     defined(TU104_DMA_MAP_PROBE) + \
     defined(TU104_HIGH_DMA_MAP_PROBE) + \
     defined(TU104_PLATFORM_DMA_PROBE) + \
     defined(TU104_FALCON_STATE_PROBE) + \
     defined(TU104_FWSEC_GUARD_PROBE) + \
     defined(TU104_FWSEC_FRTS_PROBE) + \
     defined(TU104_HANDOFF_PROBE) + \
     defined(TU104_UNLOCK_ACTIVE)) > 1
#error "Select only one diagnostic stage"
#endif

#ifdef TU104_UNLOCK_ACTIVE
#define APP_VERSION L"0.8.0-TU104-VOLATILE-UNLOCK-CANDIDATE"
#elif defined(TU104_HANDOFF_PROBE)
#define APP_VERSION L"0.7.2-UBUNTU-HANDOFF-READONLY"
#elif defined(TU104_FWSEC_FRTS_PROBE)
#define APP_VERSION L"0.7.1-FWSEC-FRTS-SCRUB-WAIT"
#elif defined(TU104_FWSEC_GUARD_PROBE)
#define APP_VERSION L"0.6.7-FWSEC-SCRUB-WAIT"
#elif defined(TU104_FALCON_STATE_PROBE)
#define APP_VERSION L"0.6.4-FALCON-STATE-READONLY"
#elif defined(TU104_PLATFORM_DMA_PROBE)
#define APP_VERSION L"0.6.3-PLATFORM-DMA-READONLY"
#elif defined(TU104_HIGH_DMA_MAP_PROBE)
#define APP_VERSION L"0.6.2-HIGH64-STREAM-MAP"
#elif defined(TU104_DMA_MAP_PROBE)
#define APP_VERSION L"0.6.1-LOW32-DMA-MAP"
#elif defined(TU104_REGISTER_PROBE)
#define APP_VERSION L"0.5.0-REGISTER-PROBE"
#elif defined(TU104_PROM_CAPTURE)
#define APP_VERSION L"0.4.0-PROM-CAPTURE"
#elif defined(TU104_MMIO_PROBE)
#define APP_VERSION L"0.3.0-MMIO-PROBE"
#elif defined(TU104_CAPTURE_ROM)
#define APP_VERSION L"0.2.0-ROMCAP"
#else
#define APP_VERSION L"0.1.0"
#endif
#define LOG_CAPACITY (32u * 1024u)
#define CONFIG_DWORDS 16u

static EFI_GUID pci_io_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID simple_fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID global_variable_guid = EFI_GLOBAL_VARIABLE;
static EFI_GUID iommu_protocol_guid = {
    0x4e939de9, 0xd948, 0x4b0f,
    {0x88, 0xed, 0xe6, 0xe1, 0xce, 0x51, 0x7c, 0x1e}
};

static CHAR8 log_buffer[LOG_CAPACITY];
static UINTN log_length;
static UINTN log_written;
static EFI_FILE_PROTOCOL *log_file;
static EFI_STATUS log_status = EFI_SUCCESS;
static EFI_HANDLE boot_volume_device;

static EFI_STATUS flush_log(void)
{
    UINTN remaining;
    UINTN written;

    if (!log_file || EFI_ERROR(log_status))
        return log_status;
    if (log_written > log_length) {
        log_status = EFI_COMPROMISED_DATA;
        return log_status;
    }

    remaining = log_length - log_written;
    if (remaining != 0) {
        written = remaining;
        log_status = uefi_call_wrapper(log_file->Write, 3, log_file,
                                       &written, log_buffer + log_written);
        if (!EFI_ERROR(log_status) && written != remaining)
            log_status = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(log_status))
            log_written += written;
    }

    if (!EFI_ERROR(log_status))
        log_status = uefi_call_wrapper(log_file->Flush, 1, log_file);
    return log_status;
}

static void note(const CHAR16 *format, ...)
{
    CHAR16 line[768];
    va_list arguments;
    UINTN index;

    va_start(arguments, format);
    VSPrint(line, sizeof(line), (CHAR16 *)format, arguments);
    va_end(arguments);

    if (ST && ST->ConOut)
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, line);

    for (index = 0; line[index] != 0; ++index) {
        if (log_length + 1 >= sizeof(log_buffer)) {
            log_status = EFI_VOLUME_FULL;
            break;
        }
        log_buffer[log_length++] =
            line[index] < 128 ? (CHAR8)line[index] : (CHAR8)'?';
    }
    log_buffer[log_length] = 0;
    flush_log();
}

static EFI_STATUS open_log_on_boot_volume(EFI_HANDLE image,
                                          const EFI_TIME *time)
{
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *candidate = NULL;
    EFI_FILE_SYSTEM_INFO *info = NULL;
    EFI_STATUS status;
    UINTN attempt;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, image,
                               &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status) || !loaded || !loaded->DeviceHandle)
        return EFI_NOT_FOUND;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded->DeviceHandle,
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

    note(L"Boot volume label=%s size=%ld free=%ld block=%d readonly=%d\r\n",
         info->VolumeLabel, info->VolumeSize, info->FreeSpace,
         info->BlockSize, info->ReadOnly);
    if (info->ReadOnly || StrCmp(info->VolumeLabel, L"RTX2070S") != 0) {
        status = EFI_ACCESS_DENIED;
        goto finished;
    }
    boot_volume_device = loaded->DeviceHandle;

    for (attempt = 0; attempt < 100; ++attempt) {
        CHAR16 path[192];

        SPrint(path, sizeof(path),
               L"\\TU104-UNLOCK\\LOGS\\PREFLIGHT-%04d%02d%02d-"
               L"%02d%02d%02d-%02d.LOG",
               time->Year, time->Month, time->Day,
               time->Hour, time->Minute, time->Second, attempt);

        status = uefi_call_wrapper(root->Open, 5, root, &candidate, path,
                                   EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(candidate->Close, 1, candidate);
            candidate = NULL;
            continue;
        }
        if (status != EFI_NOT_FOUND)
            goto finished;

        status = uefi_call_wrapper(root->Open, 5, root, &candidate, path,
                                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                                       EFI_FILE_MODE_CREATE,
                                   0);
        if (EFI_ERROR(status))
            goto finished;

        log_file = candidate;
        candidate = NULL;
        note(L"Log path=%s\r\n", path);
        status = flush_log();
        goto finished;
    }

    status = EFI_ACCESS_DENIED;

finished:
    if (info)
        FreePool(info);
    if (candidate)
        uefi_call_wrapper(candidate->Close, 1, candidate);
    if (root)
        uefi_call_wrapper(root->Close, 1, root);
    return status;
}

#ifdef TU104_CAPTURE_ROM
static EFI_STATUS capture_firmware_option_rom(EFI_PCI_IO_PROTOCOL *target,
                                               const EFI_TIME *time)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_SYSTEM_INFO *info = NULL;
    const UINT8 *rom;
    UINT64 rom_size_64;
    UINTN rom_size;
    UINTN offset = 0;
    UINTN attempt;
    UINT16 pcir_offset = 0;
    UINT32 crc;
    EFI_STATUS status;
    CHAR16 path[192];

    if (!target || !boot_volume_device || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;

    rom = (const UINT8 *)target->RomImage;
    rom_size_64 = target->RomSize;
    note(L"ROM capture guard: pointer=%lx size=%ld expected=%d\r\n",
         (UINT64)(UINTN)rom, rom_size_64,
         (UINTN)TU104_OBSERVED_EFI_ROM_SIZE);
    if (!rom || rom_size_64 != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE)
        return EFI_UNSUPPORTED;
    rom_size = (UINTN)rom_size_64;

    if (!tu104_option_rom_header_matches((const uint8_t *)rom,
                                         (size_t)rom_size,
                                         (uint16_t *)&pcir_offset)) {
        note(L"Firmware-provided ROM header/profile mismatch; capture refused.\r\n");
        return EFI_COMPROMISED_DATA;
    }
    crc = option_rom_crc32((const uint8_t *)rom, (size_t)rom_size);
    note(L"Firmware ROM header matched: PCIR offset=%04x CRC32=%08x\r\n",
         pcir_offset, crc);
    if (EFI_ERROR(log_status))
        return log_status;

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
               L"\\TU104-UNLOCK\\CAPTURES\\EFIROM-%04d%02d%02d-"
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

    note(L"Writing firmware-provided ROM copy to %s\r\n", path);
    while (offset < rom_size) {
        UINTN chunk = rom_size - offset;
        UINTN written;

        if (chunk > 4096u)
            chunk = 4096u;
        written = chunk;
        status = uefi_call_wrapper(file->Write, 3, file, &written,
                                   (VOID *)(rom + offset));
        if (EFI_ERROR(status) || written != chunk) {
            if (!EFI_ERROR(status))
                status = EFI_DEVICE_ERROR;
            goto finished;
        }
        offset += written;
    }
    status = uefi_call_wrapper(file->Flush, 1, file);
    if (!EFI_ERROR(status))
        note(L"ROM copy complete: bytes=%d CRC32=%08x\r\n", offset, crc);

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
#endif

static EFI_STATUS read_config_header(EFI_PCI_IO_PROTOCOL *pci,
                                     UINT32 config[CONFIG_DWORDS])
{
    SetMem(config, CONFIG_DWORDS * sizeof(config[0]), 0);
    return uefi_call_wrapper(pci->Pci.Read, 5, pci,
                             EfiPciIoWidthUint32, 0,
                             CONFIG_DWORDS, config);
}

static void log_config_header(const CHAR16 *name,
                              UINTN segment, UINTN bus,
                              UINTN device, UINTN function,
                              const UINT32 config[CONFIG_DWORDS])
{
    UINTN row;

    note(L"%s BDF=%04x:%02x:%02x.%x id=%08x sub=%08x classrev=%08x "
         L"command=%04x status=%04x\r\n",
         name, segment, bus, device, function,
         config[0], config[11], config[2],
         config[1] & 0xffffu, config[1] >> 16);
    for (row = 0; row < CONFIG_DWORDS; row += 4) {
        note(L"  CFG %02x: %08x %08x %08x %08x\r\n",
             row * 4, config[row], config[row + 1],
             config[row + 2], config[row + 3]);
    }
}

static EFI_STATUS
inspect_pci_configuration(EFI_PCI_IO_PROTOCOL **matched_target)
{
    EFI_HANDLE *handles = NULL;
    EFI_PCI_IO_PROTOCOL *target = NULL;
    UINT32 target_config[CONFIG_DWORDS];
    UINTN handle_count = 0;
    UINTN target_location_count = 0;
    UINTN identity_elsewhere_count = 0;
    UINTN nvidia_function_count = 0;
    UINTN parent_count = 0;
    UINTN index;
    EFI_STATUS status;

    if (matched_target)
        *matched_target = NULL;

    status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol,
                               &pci_io_guid, NULL,
                               &handle_count, &handles);
    note(L"PCI handle enumeration status=%r count=%d\r\n",
         status, handle_count);
    if (EFI_ERROR(status))
        return status;

    SetMem(target_config, sizeof(target_config), 0);
    for (index = 0; index < handle_count; ++index) {
        EFI_PCI_IO_PROTOCOL *pci = NULL;
        UINT32 id = 0xffffffffu;
        UINT32 class_revision = 0xffffffffu;
        UINT32 config[CONFIG_DWORDS];
        UINTN segment = 0;
        UINTN bus = 0;
        UINTN device = 0;
        UINTN function = 0;
        BOOLEAN interesting;

        status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[index],
                                   &pci_io_guid, (VOID **)&pci);
        if (EFI_ERROR(status) || !pci)
            continue;
        status = uefi_call_wrapper(pci->GetLocation, 5, pci,
                                   &segment, &bus, &device, &function);
        if (EFI_ERROR(status))
            continue;
        status = uefi_call_wrapper(pci->Pci.Read, 5, pci,
                                   EfiPciIoWidthUint32, 0, 1, &id);
        if (EFI_ERROR(status))
            continue;
        status = uefi_call_wrapper(pci->Pci.Read, 5, pci,
                                   EfiPciIoWidthUint32, 8, 1,
                                   &class_revision);
        if (EFI_ERROR(status))
            continue;

        interesting = ((id & 0xffffu) == TU104_PROFILE_VENDOR_ID) ||
            tu104_location_matches(segment, bus, device, function) ||
            tu104_parent_location_matches(segment, bus, device, function);
        if (!interesting)
            continue;

        status = read_config_header(pci, config);
        if (EFI_ERROR(status)) {
            note(L"Config read failed BDF=%04x:%02x:%02x.%x status=%r\r\n",
                 segment, bus, device, function, status);
            continue;
        }

        if ((id & 0xffffu) == TU104_PROFILE_VENDOR_ID) {
            ++nvidia_function_count;
            log_config_header(L"NVIDIA", segment, bus, device, function,
                              config);
        }

        if (tu104_parent_location_matches(segment, bus, device, function)) {
            ++parent_count;
            log_config_header(L"PARENT", segment, bus, device, function,
                              config);
            note(L"Parent buses primary=%02x secondary=%02x subordinate=%02x\r\n",
                 config[6] & 0xffu, (config[6] >> 8) & 0xffu,
                 (config[6] >> 16) & 0xffu);
        }

        if (tu104_location_matches(segment, bus, device, function)) {
            ++target_location_count;
            target = pci;
            CopyMem(target_config, config, sizeof(target_config));
        } else if (tu104_identity_matches(config[0], config[11], config[2])) {
            ++identity_elsewhere_count;
            note(L"Expected identity appeared at an unexpected BDF.\r\n");
        }
    }

    if (handles)
        FreePool(handles);

    note(L"Inventory summary: NVIDIA functions=%d target-BDF handles=%d "
         L"parent handles=%d identity-at-other-BDF=%d\r\n",
         nvidia_function_count, target_location_count,
         parent_count, identity_elsewhere_count);

    if (target_location_count != 1 || !target)
        return target_location_count ? EFI_COMPROMISED_DATA : EFI_NOT_FOUND;
    if (!tu104_identity_matches(target_config[0], target_config[11],
                                target_config[2])) {
        note(L"Exact BDF identity mismatch; profile rejected.\r\n");
        return EFI_UNSUPPORTED;
    }
    if (parent_count != 1) {
        note(L"Expected parent bridge was absent or ambiguous.\r\n");
        return EFI_UNSUPPORTED;
    }
    if (identity_elsewhere_count != 0) {
        note(L"Duplicate identity detected at another BDF.\r\n");
        return EFI_SECURITY_VIOLATION;
    }

    note(L"Target profile MATCHED. Raw BAR dwords: %08x %08x %08x "
         L"%08x %08x %08x ROM=%08x\r\n",
         target_config[4], target_config[5], target_config[6],
         target_config[7], target_config[8], target_config[9],
         target_config[12]);
    note(L"EFI PCI protocol ROM pointer=%lx size=%ld\r\n",
         (UINT64)(UINTN)target->RomImage, target->RomSize);
    if (matched_target)
        *matched_target = target;
    return EFI_SUCCESS;
}

#ifdef TU104_MMIO_PROBE
#include "mmio_probe.inc"
#endif
#ifdef TU104_PROM_CAPTURE
#include "prom_capture.inc"
#endif
#ifdef TU104_REGISTER_PROBE
#include "register_probe.inc"
#endif
#ifdef TU104_DMA_MAP_PROBE
#include "dma_map_probe.inc"
#endif
#ifdef TU104_HIGH_DMA_MAP_PROBE
#include "high_dma_map_probe.inc"
#endif
#if defined(TU104_PLATFORM_DMA_PROBE) || \
    defined(TU104_FWSEC_GUARD_PROBE) || \
    defined(TU104_FWSEC_FRTS_PROBE) || \
    defined(TU104_UNLOCK_ACTIVE)
#include "platform_dma_probe.inc"
#endif
#ifdef TU104_FALCON_STATE_PROBE
#include "falcon_state_probe.inc"
#endif
#if defined(TU104_FWSEC_FRTS_PROBE) || \
    defined(TU104_FWSEC_GUARD_PROBE) || \
    defined(TU104_UNLOCK_ACTIVE)
#include "fwsec_frts_probe.inc"
#endif
#ifdef TU104_FWSEC_GUARD_PROBE
#include "fwsec_guard_probe.inc"
#endif
#if defined(TU104_HANDOFF_PROBE) || defined(TU104_UNLOCK_ACTIVE)
#include "ubuntu_handoff_probe.inc"
#endif
#ifdef TU104_UNLOCK_ACTIVE
#include "tu104_unlock_active.inc"
#endif

static EFI_STATUS inspect_memory_map(void)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN required = 0;
    UINTN capacity = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    UINTN attempt;
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &required, NULL,
                               &map_key, &descriptor_size,
                               &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
    if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        descriptor_size > 4096 || required > 1024u * 1024u)
        return EFI_COMPROMISED_DATA;

    for (attempt = 0; attempt < 3; ++attempt) {
        UINTN requested = required + 16u * descriptor_size;

        map = AllocatePool(requested);
        if (!map)
            return EFI_OUT_OF_RESOURCES;
        capacity = requested;
        required = capacity;
        status = uefi_call_wrapper(BS->GetMemoryMap, 5, &required, map,
                                   &map_key, &descriptor_size,
                                   &descriptor_version);
        if (status != EFI_BUFFER_TOO_SMALL)
            break;
        FreePool(map);
        map = NULL;
        if (required > 1024u * 1024u)
            return EFI_COMPROMISED_DATA;
    }

    if (EFI_ERROR(status)) {
        if (map)
            FreePool(map);
        return status;
    }
    if (!map || required > capacity || required % descriptor_size != 0) {
        if (map)
            FreePool(map);
        return EFI_COMPROMISED_DATA;
    }

    {
        UINT64 high_pages = 0;
        UINT64 total_conventional_pages = 0;
        UINTN offset;

        for (offset = 0; offset < required; offset += descriptor_size) {
            EFI_MEMORY_DESCRIPTOR *descriptor =
                (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + offset);
            UINT64 start;
            UINT64 end;
            UINT64 high_start;

            if (descriptor->Type != EfiConventionalMemory ||
                descriptor->NumberOfPages == 0)
                continue;
            start = descriptor->PhysicalStart;
            if (descriptor->NumberOfPages >
                ((~(UINT64)0 - start) >> 12))
                continue;
            end = start + (descriptor->NumberOfPages << 12);
            total_conventional_pages += descriptor->NumberOfPages;
            if (end <= 0x100000000ULL)
                continue;
            high_start = start < 0x100000000ULL ?
                0x100000000ULL : start;
            high_pages += (end - high_start) >> 12;
        }

        note(L"Memory map entries=%d descriptor-size=%d version=%d "
             L"conventional=%ld MiB above4G=%ld MiB\r\n",
             required / descriptor_size, descriptor_size,
             descriptor_version, total_conventional_pages >> 8,
             high_pages >> 8);
    }

    FreePool(map);
    return EFI_SUCCESS;
}

static void inspect_platform_state(void)
{
    EFI_TIME time;
    UINT8 secure_boot = 0xff;
    UINT8 setup_mode = 0xff;
    UINTN size;
    UINT32 attributes = 0;
    EFI_STATUS status;
    VOID *iommu = NULL;

    note(L"Firmware vendor=%s revision=%08x UEFI=%d.%02d\r\n",
         ST->FirmwareVendor ? ST->FirmwareVendor : L"(unknown)",
         ST->FirmwareRevision,
         ST->Hdr.Revision >> 16, ST->Hdr.Revision & 0xffffu);

    SetMem(&time, sizeof(time), 0);
    status = uefi_call_wrapper(RT->GetTime, 2, &time, NULL);
    note(L"Firmware time=%04d-%02d-%02d %02d:%02d:%02d status=%r\r\n",
         time.Year, time.Month, time.Day,
         time.Hour, time.Minute, time.Second, status);

    size = sizeof(secure_boot);
    status = uefi_call_wrapper(RT->GetVariable, 5, L"SecureBoot",
                               &global_variable_guid, &attributes,
                               &size, &secure_boot);
    note(L"SecureBoot value=%d size=%d attrs=%08x status=%r\r\n",
         secure_boot, size, attributes, status);

    attributes = 0;
    size = sizeof(setup_mode);
    status = uefi_call_wrapper(RT->GetVariable, 5, L"SetupMode",
                               &global_variable_guid, &attributes,
                               &size, &setup_mode);
    note(L"SetupMode value=%d size=%d attrs=%08x status=%r\r\n",
         setup_mode, size, attributes, status);

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &iommu_protocol_guid, NULL, &iommu);
    note(L"EFI IOMMU protocol present=%d status=%r pointer=%lx\r\n",
         !EFI_ERROR(status) && iommu != NULL, status,
         (UINT64)(UINTN)iommu);
}

static EFI_STATUS run_preflight(EFI_HANDLE image)
{
    EFI_TIME time;
    EFI_STATUS status;
    EFI_STATUS pci_status;
    EFI_STATUS memory_status;
    EFI_PCI_IO_PROTOCOL *matched_target = NULL;
#ifdef TU104_CAPTURE_ROM
    EFI_STATUS capture_status = EFI_NOT_READY;
#endif
#ifdef TU104_MMIO_PROBE
    EFI_STATUS mmio_status = EFI_NOT_READY;
#endif
#ifdef TU104_PROM_CAPTURE
    EFI_STATUS prom_capture_status = EFI_NOT_READY;
#endif
#ifdef TU104_REGISTER_PROBE
    EFI_STATUS register_probe_status = EFI_NOT_READY;
#endif
#ifdef TU104_DMA_MAP_PROBE
    EFI_STATUS dma_map_status = EFI_NOT_READY;
#endif
#ifdef TU104_HIGH_DMA_MAP_PROBE
    EFI_STATUS high_dma_map_status = EFI_NOT_READY;
#endif
#ifdef TU104_PLATFORM_DMA_PROBE
    EFI_STATUS platform_dma_status = EFI_NOT_READY;
#endif
#ifdef TU104_FALCON_STATE_PROBE
    EFI_STATUS falcon_state_status = EFI_NOT_READY;
#endif
#ifdef TU104_FWSEC_FRTS_PROBE
    EFI_STATUS fwsec_frts_status = EFI_NOT_READY;
#endif
#ifdef TU104_FWSEC_GUARD_PROBE
    EFI_STATUS fwsec_guard_status = EFI_NOT_READY;
#endif
#ifdef TU104_HANDOFF_PROBE
    EFI_STATUS handoff_status = EFI_NOT_READY;
#endif
#ifdef TU104_UNLOCK_ACTIVE
    EFI_STATUS unlock_status = EFI_NOT_READY;
#endif

    note(L"RTX 2070 SUPER TU104 PREFLIGHT %s\r\n", APP_VERSION);
#ifdef TU104_UNLOCK_ACTIVE
    note(L"MODE: TARGET-PINNED ONE-SHOT VOLATILE TU104 UNLOCK CANDIDATE.\r\n");
    note(L"PRELOADS THE EXACT INSTALLED UBUNTU GRUB BEFORE PHYSICAL U-KEY "
         L"CONFIRMATION. THE ACTIVE CORE MAY WRITE ONLY ALLOWLISTED GSP/SEC2 "
         L"FALCON PORTS; IT DOES NOT WRITE PCI CONFIGURATION, FLASH, BOOT "
         L"VARIABLES, SYSTEM ESP, FEATURE SELECTORS, OR WPR REGISTERS DIRECTLY. "
         L"NO RETRY. UNCERTAIN POST-WRITE STATE RETAINS MEMORY AND HALTS FOR "
         L"PHYSICAL POWER-OFF. ONLY EXACT UNLOCK PLUS QUIESCENCE MAY START "
         L"UBUNTU IN THE SAME BOOT.\r\n");
#elif defined(TU104_HANDOFF_PROBE)
    note(L"MODE: READ-ONLY EXACT UBUNTU SAME-BOOT HANDOFF TEST.\r\n");
    note(L"VALIDATES THE INSTALLED GRUB EFI IMAGE AND grub.cfg, PRELOADS "
         L"GRUB BEFORE ANY ACTIVE GPU WORK, THEN STARTS IT DIRECTLY. NO GPU "
         L"MMIO, PCI WRITE, DMA, RESET, GPU FIRMWARE EXECUTION, FLASH, "
         L"SYSTEM-ESP WRITE, BOOT-VARIABLE CHANGE, OR REBOOT.\r\n");
#elif defined(TU104_FWSEC_FRTS_PROBE)
    note(L"MODE: FIRST ACTIVE, ONE-SHOT NATIVE FWSEC FRTS PROBE.\r\n");
    note(L"AFTER EXACT TARGET/PLATFORM/PROM/FALCON GUARDS, A PHYSICAL "
         L"U-KEY CONFIRMATION, AND A BOUNDED READ-ONLY SEC2 SCRUB WAIT: "
         L"CLAIM 10 PAGES AT THE PREVIOUSLY OBSERVED ABOVE-4GiB ADDRESS, "
         L"RESET GSP, LOAD THE OFFICIAL GENERIC LOADER, AND RUN ONLY THE "
         L"NATIVE SIGNED FRTS COMMAND. NO EXPLOIT BOOTER, FEATURE-SELECTOR "
         L"WRITE, SEC2 WRITE, DIRECT WPR WRITE, FLASH, AUTOMATIC BOOT, OR "
         L"RETRY. AFTER ANY GPU WRITE ATTEMPT THIS IMAGE RETAINS MEMORY AND "
         L"HALTS UNTIL PHYSICAL POWER-OFF.\r\n");
#elif defined(TU104_FWSEC_GUARD_PROBE)
    note(L"MODE: GPU-READ-ONLY FWSEC SEC2-SCRUB WAIT + CORE-GUARD REPLAY.\r\n");
    note(L"REPRODUCES THE ACTIVE IMAGE'S PLATFORM, PROM, HIGH-PAGE, "
         L"STAGING, PHYSICAL U CONFIRMATION, REVALIDATION, AND WATCHDOG "
         L"DISABLE, BOUNDED WAIT FOR DOCUMENTED SEC2 DMACTL SCRUB STATUS, "
         L"THEN CALLS ONLY THE REAL CORE GUARDS WHILE RECORDING ALL READS. "
         L"NO GPU MMIO/PCI WRITE, RESET, FIRMWARE EXECUTION, DMA MAP, FLASH, "
         L"OR CORE RUN STATE MACHINE.\r\n");
#elif defined(TU104_FALCON_STATE_PROBE)
    note(L"MODE: BOUNDED READ-ONLY TU104 GSP/SEC2 FALCON INVENTORY.\r\n");
    note(L"READS: BOOT0 TWICE AND 26 NAMED GSP/SEC2 CONTROL-STATE "
         L"REGISTERS TWICE. NO IMEM/DMEM PORT READ, PCI/MMIO WRITE, DMA, "
         L"RESET, FIRMWARE EXECUTION, OR FLASH COMMAND.\r\n");
#elif defined(TU104_PLATFORM_DMA_PROBE)
    note(L"MODE: READ-ONLY Z390 PLATFORM DMA/VT-D INVENTORY.\r\n");
    note(L"READS: MSI SETUP VARIABLE, CPU ID, HOST-BRIDGE CONFIGURATION, "
         L"MCHBAR VT-D2 BAR, BOUNDED VT-D STATUS, AND 256 CPU-MEMORY "
         L"BYTES OF ROOT MAP CODE. NO GPU BAR ACCESS, DMA MAP/TRANSFER, "
         L"PCI/MMIO WRITE, RESET, FIRMWARE EXECUTION, OR FLASH COMMAND.\r\n");
#elif defined(TU104_HIGH_DMA_MAP_PROBE)
    note(L"MODE: EFI ROOT-BRIDGE HIGH64 STREAMING MAP/UNMAP SERVICE PROBE.\r\n");
    note(L"ONE 32 MiB ABOVE-4GiB TEMPORARY PAGE ALLOCATION AND READ64 "
         L"MAP/UNMAP LIFECYCLE. NO GPU BAR ACCESS, PCI/MMIO WRITES, DEVICE "
         L"DMA TRANSFER, RESET, FIRMWARE EXECUTION, OR FLASH COMMANDS.\r\n");
#elif defined(TU104_DMA_MAP_PROBE)
    note(L"MODE: EFI PCI LOW32 COMMON-BUFFER MAP/UNMAP SERVICE PROBE.\r\n");
    note(L"ONE 32 MiB BELOW-4GiB TEMPORARY ALLOCATION AND MAP/UNMAP LIFECYCLE. "
         L"NO GPU BAR ACCESS, PCI/MMIO WRITES, DEVICE DMA TRANSFER, RESET, "
         L"FIRMWARE EXECUTION, OR FLASH COMMANDS.\r\n");
#elif defined(TU104_REGISTER_PROBE)
    note(L"MODE: BOUNDED READ-ONLY TU104 REGISTER INVENTORY.\r\n");
    note(L"READS: BOOT0 TWICE AND 12 NAMED STATUS REGISTERS TWICE. "
         L"NO WRITES, DMA, RESET, FIRMWARE EXECUTION, OR FLASH COMMANDS.\r\n");
#elif defined(TU104_PROM_CAPTURE)
    note(L"MODE: BOUNDED READ-ONLY 512 KiB GPU PROM CAPTURE.\r\n");
    note(L"READS: BOOT0 TWICE AND PROM[0..7FFFF] TWICE IN 64 KiB CHUNKS. "
         L"NO WRITES, DMA, RESET, FIRMWARE EXECUTION, OR FLASH COMMANDS.\r\n");
#elif defined(TU104_MMIO_PROBE)
    note(L"MODE: STRICTLY BOUNDED READ-ONLY GPU MMIO PROBE.\r\n");
    note(L"READS: BOOT0 TWICE AND PROM[0..4095] TWICE. "
         L"NO WRITES, DMA, RESET, FIRMWARE EXECUTION, OR FLASH COMMANDS.\r\n");
#elif defined(TU104_CAPTURE_ROM)
    note(L"MODE: PCI CONFIGURATION READS PLUS A BOUNDED COPY OF THE "
         L"FIRMWARE-PROVIDED OPTION ROM BUFFER.\r\n");
    note(L"NO GPU MMIO, DMA, RESET, FIRMWARE EXECUTION, OR FLASH ACCESS.\r\n");
#else
    note(L"MODE: PCI CONFIGURATION READS ONLY; NO GPU MMIO, DMA, RESET, "
         L"FIRMWARE EXECUTION, OR FLASH ACCESS.\r\n");
#endif

    SetMem(&time, sizeof(time), 0);
    status = uefi_call_wrapper(RT->GetTime, 2, &time, NULL);
    if (EFI_ERROR(status))
        SetMem(&time, sizeof(time), 0);

    status = open_log_on_boot_volume(image, &time);
    note(L"Open boot-volume log status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status)) {
        note(L"A durable USB log is required; PCI inspection skipped.\r\n");
        goto finished;
    }

    inspect_platform_state();
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto finished;
    }

    memory_status = inspect_memory_map();
    note(L"Memory-map inspection status=%r\r\n", memory_status);
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto finished;
    }

    pci_status = inspect_pci_configuration(&matched_target);
    note(L"PCI preflight status=%r\r\n", pci_status);
#if defined(TU104_UNLOCK_ACTIVE)
    note(L"SAFETY POLICY: one physical confirmation and one core invocation. "
         L"The 32-MiB pool is released only after exact selector/WPR results, "
         L"both Falcons stopped, cleanup verified, and Ubuntu GRUB already "
         L"preloaded. Any failed or uncertain write retains the pool and "
         L"halts without reboot or OS handoff.\r\n");
#elif defined(TU104_HANDOFF_PROBE)
    note(L"SAFETY RESULT: this image only reads and validates the exact "
         L"installed Ubuntu GRUB/config and invokes the UEFI image loader. "
         L"It has no GPU BAR, DMA, PCI/MMIO write, reset, firmware execution, "
         L"flash, boot-variable write, or system-ESP write path.\r\n");
#elif defined(TU104_FWSEC_FRTS_PROBE)
    note(L"SAFETY POLICY: this active image is target-locked and physically "
         L"armed once. It may write only 13 allowlisted GSP Falcon/FBIF "
         L"ports through the tested core. It cannot write SEC2, WPR2, PCI "
         L"configuration, feature selectors, flash, or boot variables.\r\n");
#elif defined(TU104_FWSEC_GUARD_PROBE)
    note(L"SAFETY RESULT: this diagnostic has no GPU/PCI write callback, "
         L"reset, firmware start, DMA Map/Unmap, or flash path. It disables "
         L"only the volatile firmware watchdog after U confirmation to match "
         L"0.7.0 timing. Its CPU-owned high pages are always freed and are "
         L"never exposed to the GPU.\r\n");
#elif defined(TU104_PLATFORM_DMA_PROBE)
    note(L"SAFETY RESULT: no PCI/MMIO writes, no GPU BAR access, no DMA "
         L"mapping or device transfer, no reset, and no GPU firmware "
         L"execution were compiled into this image. Only bounded CPU, "
         L"configuration, firmware-variable, and chipset status reads are "
         L"enabled.\r\n");
#elif defined(TU104_HIGH_DMA_MAP_PROBE)
    note(L"SAFETY RESULT: no PCI/MMIO writes, no GPU BAR reads, no device "
         L"DMA transfer, no reset, and no GPU firmware execution were "
         L"compiled into this image. Only EFI page allocation and one "
         L"temporary HIGH64 root Read64 Map/Unmap lifecycle are enabled.\r\n");
#elif defined(TU104_DMA_MAP_PROBE)
    note(L"SAFETY RESULT: no PCI/MMIO writes, no GPU BAR reads, no device "
         L"DMA transfer, no reset, and no GPU firmware execution were "
         L"compiled into this image. Only EFI allocation and one temporary "
         L"LOW32 common-buffer Map/Unmap service lifecycle are enabled.\r\n");
#elif defined(TU104_MMIO_PROBE) || defined(TU104_PROM_CAPTURE) || \
    defined(TU104_REGISTER_PROBE) || defined(TU104_FALCON_STATE_PROBE)
    note(L"SAFETY RESULT: no PCI/MMIO writes, no PCI DMA operations, no "
         L"reset, and no GPU firmware execution were compiled into this "
         L"image. Only the bounded reads stated above are enabled.\r\n");
#else
    note(L"SAFETY RESULT: no PCI writes, no GPU BAR access, no PCI DMA "
         L"operations, and no GPU firmware execution were compiled into "
         L"this image.\r\n");
#endif
#ifdef TU104_UNLOCK_ACTIVE
    if (!EFI_ERROR(pci_status) && !EFI_ERROR(memory_status))
        unlock_status = run_tu104_unlock_active(image, matched_target);
    status = EFI_ERROR(pci_status) ? pci_status :
        (EFI_ERROR(memory_status) ? memory_status : unlock_status);
#elif defined(TU104_HANDOFF_PROBE)
    if (!EFI_ERROR(pci_status) && !EFI_ERROR(memory_status))
        handoff_status = run_ubuntu_handoff_probe(image);
    status = EFI_ERROR(pci_status) ? pci_status :
        (EFI_ERROR(memory_status) ? memory_status : handoff_status);
#elif defined(TU104_FWSEC_FRTS_PROBE)
    if (!EFI_ERROR(pci_status))
        fwsec_frts_status = run_fwsec_frts_probe(matched_target);
    note(L"Native FWSEC FRTS active-stage status=%r\r\n",
         fwsec_frts_status);
    status = EFI_ERROR(pci_status) ? pci_status : fwsec_frts_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_FWSEC_GUARD_PROBE)
    if (!EFI_ERROR(pci_status))
        fwsec_guard_status = run_fwsec_guard_probe(matched_target);
    note(L"GPU-read-only FWSEC core-guard replay status=%r\r\n",
         fwsec_guard_status);
    status = EFI_ERROR(pci_status) ? pci_status : fwsec_guard_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_FALCON_STATE_PROBE)
    if (!EFI_ERROR(pci_status))
        falcon_state_status = run_falcon_state_probe(matched_target);
    note(L"Bounded Falcon-state probe status=%r\r\n",
         falcon_state_status);
    status = EFI_ERROR(pci_status) ? pci_status : falcon_state_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_PLATFORM_DMA_PROBE)
    if (!EFI_ERROR(pci_status))
        platform_dma_status = run_platform_dma_probe(matched_target);
    note(L"Read-only platform DMA/VT-d probe status=%r\r\n",
         platform_dma_status);
    status = EFI_ERROR(pci_status) ? pci_status : platform_dma_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_HIGH_DMA_MAP_PROBE)
    if (!EFI_ERROR(pci_status))
        high_dma_map_status = run_high_dma_map_probe(matched_target);
    note(L"Bounded HIGH64 map service probe status=%r\r\n",
         high_dma_map_status);
    status = EFI_ERROR(pci_status) ? pci_status : high_dma_map_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_DMA_MAP_PROBE)
    if (!EFI_ERROR(pci_status))
        dma_map_status = run_dma_map_probe(matched_target);
    note(L"Bounded DMA map service probe status=%r\r\n", dma_map_status);
    status = EFI_ERROR(pci_status) ? pci_status : dma_map_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_REGISTER_PROBE)
    if (!EFI_ERROR(pci_status))
        register_probe_status = run_register_probe(matched_target);
    note(L"Bounded register probe status=%r\r\n", register_probe_status);
    status = EFI_ERROR(pci_status) ? pci_status : register_probe_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_PROM_CAPTURE)
    if (!EFI_ERROR(pci_status))
        prom_capture_status = run_prom_capture(matched_target, &time);
    note(L"Bounded 512 KiB PROM capture status=%r\r\n",
         prom_capture_status);
    status = EFI_ERROR(pci_status) ? pci_status : prom_capture_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_MMIO_PROBE)
    if (!EFI_ERROR(pci_status))
        mmio_status = run_mmio_probe(matched_target, &time);
    note(L"Bounded MMIO probe status=%r\r\n", mmio_status);
    status = EFI_ERROR(pci_status) ? pci_status : mmio_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#elif defined(TU104_CAPTURE_ROM)
    if (!EFI_ERROR(pci_status))
        capture_status = capture_firmware_option_rom(matched_target, &time);
    note(L"Firmware-provided ROM capture status=%r\r\n", capture_status);
    status = EFI_ERROR(pci_status) ? pci_status : capture_status;
    if (!EFI_ERROR(status) && EFI_ERROR(memory_status))
        status = memory_status;
#else
    status = EFI_ERROR(pci_status) ? pci_status : memory_status;
#endif

finished:
    note(L"Diagnostic result=%r; returning to firmware in 5 seconds.\r\n",
         status);
    flush_log();
    if (log_file) {
        EFI_STATUS close_status =
            uefi_call_wrapper(log_file->Close, 1, log_file);
        log_file = NULL;
        if (!EFI_ERROR(log_status))
            log_status = close_status;
    }
    uefi_call_wrapper(BS->Stall, 1, 5000000);
    return EFI_SUCCESS;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    InitializeLib(image, system_table);
    return run_preflight(image);
}
