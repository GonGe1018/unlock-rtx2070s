/* Included only by the explicitly selected TU104_FWSEC_FRTS_PROBE build.
 *
 * This is the first active stage.  It runs only the board-native, signed
 * production FWSEC FRTS command on GSP.  It does not run the exploit Booter,
 * alter feature selectors, touch SEC2, write WPR registers directly, flash
 * firmware, or start an operating system.  A physical U-key confirmation is
 * required immediately before the first possible GPU write.
 *
 * After the first write attempt, every outcome (including success) retains
 * all backing pages and halts in UEFI until a physical power-off.  A failed
 * write may have reached hardware even when the protocol reports an error.
 */

#include "tu104_fwsec_lifecycle.h"
#include "tu104_fwsec_probe_core.h"

#define FRTS_PROM_BYTES ((UINTN)0x00080000)
#define FRTS_PROM_CHUNK_BYTES ((UINTN)0x00010000)
#define FRTS_PROM_CRC UINT32_C(0x6fe8d04c)
#define FRTS_PROM_FIRST UINT32_C(0xeb77aa55)
#define FRTS_PROM_LEGACY_BYTES ((UINTN)0x0000ee00)
#define FRTS_PROM_LEGACY_CRC UINT32_C(0x2054ce3a)
#define FRTS_MEMORY_RO UINT64_C(0x00020000)
#define FRTS_PCI_CURRENT_ATTRIBUTES UINT64_C(0x00000718)
#define FRTS_PCI_SUPPORTED_ATTRIBUTES UINT64_C(0x0001277b)
#define FRTS_ROOT_CURRENT_ATTRIBUTES UINT64_C(0x00040018)
#define FRTS_ROOT_SUPPORTED_ATTRIBUTES UINT64_C(0x0007003f)
#define FRTS_ROOT_MAP_CRC32 UINT32_C(0x509162cd)
#define FRTS_ROOT_MAP_MEMORY_ATTRIBUTES UINT64_C(0x0000000f)
#define FRTS_CONFIRM_TICKS 300u

extern const UINT8 _binary_bl_gsp_tu102_bin_start[];
extern const UINT8 _binary_bl_gsp_tu102_bin_end[];

#ifdef TU104_FWSEC_FRTS_PROBE
struct frts_probe_io {
    EFI_PCI_IO_PROTOCOL *target;
};
#endif

static EFI_STATUS frts_verify_boot_mode(void)
{
    UINT8 secure_boot = 0xff;
    UINT8 setup_mode = 0xff;
    UINTN size = sizeof(secure_boot);
    UINT32 attributes = 0;
    EFI_STATUS status;

    status = uefi_call_wrapper(RT->GetVariable, 5, L"SecureBoot",
                               &global_variable_guid, &attributes,
                               &size, &secure_boot);
    if (EFI_ERROR(status) || size != 1 || attributes != 6 || secure_boot != 0)
        return EFI_UNSUPPORTED;
    size = sizeof(setup_mode);
    attributes = 0;
    status = uefi_call_wrapper(RT->GetVariable, 5, L"SetupMode",
                               &global_variable_guid, &attributes,
                               &size, &setup_mode);
    note(L"Active-stage boot guard: SecureBoot=%d SetupMode=%d "
         L"status=%r\r\n", secure_boot, setup_mode, status);
    if (EFI_ERROR(status) || size != 1 || attributes != 6 || setup_mode != 1)
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

static EFI_STATUS
frts_verify_pci_attributes(EFI_PCI_IO_PROTOCOL *target)
{
    UINT64 supported = 0;
    UINT64 current = 0;
    EFI_STATUS status;

    if (!target)
        return EFI_INVALID_PARAMETER;
    status = uefi_call_wrapper(target->Attributes, 4, target,
                               EfiPciIoAttributeOperationSupported,
                               0, &supported);
    if (!EFI_ERROR(status))
        status = uefi_call_wrapper(target->Attributes, 4, target,
                                   EfiPciIoAttributeOperationGet,
                                   0, &current);
    note(L"Active-stage PCI attributes: current=%lx supported=%lx "
         L"status=%r\r\n", current, supported, status);
    if (EFI_ERROR(status) || current != FRTS_PCI_CURRENT_ATTRIBUTES ||
        supported != FRTS_PCI_SUPPORTED_ATTRIBUTES)
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

static EFI_STATUS
frts_verify_root_profile(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root)
{
    static const UINT32 parent_config[CONFIG_DWORDS] = {
        UINT32_C(0x19018086), UINT32_C(0x00100007),
        UINT32_C(0x0604000d), UINT32_C(0x00810010),
        UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00010100), UINT32_C(0x20003030),
        UINT32_C(0xa400a300), UINT32_C(0xa2019001),
        UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x00000088),
        UINT32_C(0x00000000), UINT32_C(0x0018010b),
    };
    const UINT8 *map_code;
    UINT64 supported = 0;
    UINT64 current = 0;
    UINT64 code_attributes = 0;
    UINT32 value = 0;
    UINTN index;
    EFI_STATUS status;

    if (!root || !root->Map)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < CONFIG_DWORDS; ++index) {
        status = platform_pci_read32(root, 0, 1, 0,
                                     index * sizeof(UINT32), &value);
        if (EFI_ERROR(status) || value != parent_config[index])
            return EFI_SECURITY_VIOLATION;
    }
    status = uefi_call_wrapper(root->GetAttributes, 3, root,
                               &supported, &current);
    if (EFI_ERROR(status) || supported != FRTS_ROOT_SUPPORTED_ATTRIBUTES ||
        current != FRTS_ROOT_CURRENT_ATTRIBUTES)
        return EFI_UNSUPPORTED;
    map_code = (const UINT8 *)(UINTN)root->Map;
    status = platform_map_code_region(map_code, &code_attributes);
    note(L"Immediate root profile: parent-id=%08x root-current=%lx "
         L"root-supported=%lx map=%lx map-attrs=%lx map-crc=%08x "
         L"status=%r\r\n", parent_config[0], current, supported,
         (UINT64)(UINTN)map_code, code_attributes,
         EFI_ERROR(status) ? 0 : option_rom_crc32(
             (const uint8_t *)map_code, PLATFORM_MAP_CODE_BYTES), status);
    if (EFI_ERROR(status) ||
        code_attributes != FRTS_ROOT_MAP_MEMORY_ATTRIBUTES ||
        option_rom_crc32((const uint8_t *)map_code,
                         PLATFORM_MAP_CODE_BYTES) !=
            FRTS_ROOT_MAP_CRC32)
        return EFI_SECURITY_VIOLATION;
    return EFI_ERROR(log_status) ? log_status : EFI_SUCCESS;
}

static EFI_STATUS
frts_capture_and_verify_prom(EFI_PCI_IO_PROTOCOL *target, UINT8 **capture)
{
    UINT8 *prom = NULL;
    UINTN offset;
    EFI_STATUS status = EFI_SUCCESS;

    if (!target || !capture)
        return EFI_INVALID_PARAMETER;
    *capture = NULL;
    prom = AllocatePool(FRTS_PROM_BYTES);
    if (!prom)
        return EFI_OUT_OF_RESOURCES;
    SetMem(prom, FRTS_PROM_BYTES, 0xa5);
    for (offset = 0; offset < FRTS_PROM_BYTES;
         offset += FRTS_PROM_CHUNK_BYTES) {
        status = uefi_call_wrapper(target->Mem.Read, 6, target,
                                   EfiPciIoWidthUint32, 0,
                                   TU104_PROM_WINDOW + offset,
                                   FRTS_PROM_CHUNK_BYTES / sizeof(UINT32),
                                   prom + offset);
        if (EFI_ERROR(status))
            break;
    }
    note(L"Active-stage live PROM: first=%08x bytes=%lx crc=%08x "
         L"legacy-crc=%08x status=%r\r\n",
         tu104_fwsec_probe_le32(prom), (UINT64)FRTS_PROM_BYTES,
         EFI_ERROR(status) ? 0 :
             option_rom_crc32((const uint8_t *)prom, FRTS_PROM_BYTES),
         EFI_ERROR(status) ? 0 :
             option_rom_crc32((const uint8_t *)prom,
                              FRTS_PROM_LEGACY_BYTES), status);
    if (EFI_ERROR(status) ||
        tu104_fwsec_probe_le32(prom) != FRTS_PROM_FIRST ||
        option_rom_crc32((const uint8_t *)prom, FRTS_PROM_BYTES) !=
            FRTS_PROM_CRC ||
        option_rom_crc32((const uint8_t *)prom,
                         FRTS_PROM_LEGACY_BYTES) != FRTS_PROM_LEGACY_CRC ||
        !target->RomImage || target->RomSize < FRTS_PROM_LEGACY_BYTES ||
        CompareMem(prom, target->RomImage, FRTS_PROM_LEGACY_BYTES) != 0 ||
        tu104_fwsec_probe_crc32(
            prom + TU104_FWSEC_IMAGE_PROM_OFFSET,
            TU104_FWSEC_STORED_BYTES) != TU104_FWSEC_NATIVE_CRC32 ||
        !tu104_fwsec_probe_native_layout_matches(
            prom + TU104_FWSEC_IMAGE_PROM_OFFSET,
            TU104_FWSEC_STORED_BYTES)) {
        FreePool(prom);
        return EFI_CRC_ERROR;
    }
    *capture = prom;
    return EFI_SUCCESS;
}

#if defined(TU104_FWSEC_FRTS_PROBE) || defined(TU104_FWSEC_GUARD_PROBE)
static EFI_STATUS
frts_verify_observed_high_range(UINT32 expected_type,
                                const CHAR16 *phase)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN required = 0;
    UINTN capacity = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    UINTN attempt;
    UINTN matches = 0;
    EFI_STATUS status;

    if (!phase || (expected_type != EfiConventionalMemory &&
                   expected_type != EfiBootServicesData))
        return EFI_INVALID_PARAMETER;
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &required, NULL,
                               &map_key, &descriptor_size,
                               &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size != 48 ||
        descriptor_version != 1 || required > 1024u * 1024u)
        return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
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
    if (EFI_ERROR(status) || !map || required > capacity ||
        required % descriptor_size != 0 || descriptor_size != 48 ||
        descriptor_version != 1) {
        if (map)
            FreePool(map);
        return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
    }

    for (attempt = 0; attempt < required; attempt += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + attempt);
        UINT64 start = descriptor->PhysicalStart;
        UINT64 end;
        UINT64 required_end = TU104_FWSEC_OBSERVED_HIGH_BASE +
            TU104_FWSEC_ALLOCATION_BYTES;

        if (descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages > ((~(UINT64)0 - start) >> 12))
            continue;
        end = start + (descriptor->NumberOfPages << 12);
        if (TU104_FWSEC_OBSERVED_HIGH_BASE < start || required_end > end)
            continue;
        if (descriptor->Type != expected_type ||
            !(descriptor->Attribute & EFI_MEMORY_WB) ||
            (descriptor->Attribute &
             (EFI_MEMORY_WP | EFI_MEMORY_RP | FRTS_MEMORY_RO))) {
            FreePool(map);
            return EFI_ACCESS_DENIED;
        }
        ++matches;
    }
    FreePool(map);
    note(L"Observed high-range %s guard: base=%lx bytes=%lx pages=%ld "
         L"expected-type=%d covering-descriptors=%d\r\n", phase,
         TU104_FWSEC_OBSERVED_HIGH_BASE,
         TU104_FWSEC_ALLOCATION_BYTES,
         TU104_FWSEC_ALLOCATION_PAGES, expected_type, matches);
    if (EFI_ERROR(log_status))
        return log_status;
    return matches == 1 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS
frts_allocate_observed_pages(VOID **buffer, EFI_PHYSICAL_ADDRESS *physical,
                             BOOLEAN *allocated)
{
    EFI_STATUS status;

    if (!buffer || !physical || !allocated)
        return EFI_INVALID_PARAMETER;
    *buffer = NULL;
    *allocated = FALSE;
    *physical = TU104_FWSEC_OBSERVED_HIGH_BASE;
    status = frts_verify_observed_high_range(
        EfiConventionalMemory, L"pre-allocation");
    if (EFI_ERROR(status))
        return status;
    if (EFI_ERROR(flush_log()))
        return log_status;
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress,
                               EfiBootServicesData,
                               (UINTN)TU104_FWSEC_ALLOCATION_PAGES,
                               physical);
    note(L"Active-stage AllocateAddress status=%r requested=%lx "
         L"returned=%lx\r\n", status,
         TU104_FWSEC_OBSERVED_HIGH_BASE, *physical);
    if (EFI_ERROR(status))
        return status;
    *allocated = TRUE;
    *buffer = (VOID *)(UINTN)*physical;
    if (*physical != TU104_FWSEC_OBSERVED_HIGH_BASE ||
        !tu104_fwsec_allocation_suitable(
            *physical, TU104_FWSEC_ALLOCATION_BYTES))
        return EFI_COMPROMISED_DATA;
    return frts_verify_observed_high_range(
        EfiBootServicesData, L"owned-post-allocation");
}
#endif

static EFI_STATUS
frts_revalidate_platform(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 config[CONFIG_DWORDS];
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    VOID *iommu = NULL;
    BOOLEAN dmar_present = FALSE;
    EFI_STATUS status;

    SetMem(config, sizeof(config), 0);
    status = platform_verify_target(target, config);
    if (!EFI_ERROR(status))
        status = frts_verify_boot_mode();
    if (!EFI_ERROR(status))
        status = platform_read_setup();
    if (!EFI_ERROR(status))
        status = platform_read_cpu();
    if (!EFI_ERROR(status))
        status = frts_verify_pci_attributes(target);
    if (EFI_ERROR(status))
        return status;
    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &iommu_protocol_guid, NULL, &iommu);
    if (status != EFI_NOT_FOUND || iommu != NULL)
        return EFI_UNSUPPORTED;
    status = platform_dmar_presence(&dmar_present);
    if (status != EFI_NOT_FOUND || dmar_present)
        return EFI_UNSUPPORTED;
    status = platform_resolve_root(target, config, &root);
    if (EFI_ERROR(status) || !root)
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    status = frts_verify_root_profile(root);
    if (!EFI_ERROR(status))
        status = platform_observe_vtd(root);
    note(L"Immediate pre-write platform revalidation status=%r\r\n",
         status);
    return EFI_ERROR(log_status) ? log_status : status;
}

/* TU102 DMACTL[2:1] are read-only IMEM/DMEM scrub status.  The 0.6.7
 * field diagnostic observed 7 -> 1 after one 1 ms delay in two independent
 * runs, then the unchanged 10,197-read core guard passed.  Keep the full
 * stable portion pinned and fail closed before any GPU write on timeout,
 * unreadable data, or an unexpected non-scrub bit.
 */
static EFI_STATUS
frts_wait_sec2_scrub(EFI_PCI_IO_PROTOCOL *target)
{
    const UINT32 offset = TU104_SEC2_BASE + TU104_FALCON_DMACTL_OFFSET;
    const UINT32 stable_expected = UINT32_C(0x00000001);
    UINT32 first = 0;
    UINT32 last = 0;
    UINTN polls = 0;
    UINTN scrubbing_samples = 0;
    unsigned index;
    EFI_STATUS status = EFI_TIMEOUT;

    if (!target)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < TU104_SCRUB_POLL_LIMIT; ++index) {
        UINT32 value = 0;

        status = uefi_call_wrapper(target->Mem.Read, 6, target,
                                   EfiPciIoWidthUint32, 0,
                                   (UINT64)offset, 1, &value);
        if (EFI_ERROR(status))
            break;
        ++polls;
        if (polls == 1)
            first = value;
        last = value;
        if (value == UINT32_C(0xffffffff) ||
            (value & UINT32_C(0xffff0000)) == UINT32_C(0xbadf0000)) {
            status = EFI_COMPROMISED_DATA;
            break;
        }
        if ((value & ~TU104_FALCON_DMACTL_SCRUBBING) != stable_expected) {
            status = EFI_SECURITY_VIOLATION;
            break;
        }
        if ((value & TU104_FALCON_DMACTL_SCRUBBING) == 0) {
            status = EFI_SUCCESS;
            break;
        }
        ++scrubbing_samples;
        if (index + 1 == TU104_SCRUB_POLL_LIMIT) {
            status = EFI_TIMEOUT;
            break;
        }
        status = uefi_call_wrapper(BS->Stall, 1,
                                   (UINTN)TU104_POLL_DELAY_US);
        if (EFI_ERROR(status))
            break;
    }
    note(L"SEC2 scrub wait: offset=%08x first=%08x last=%08x polls=%d "
         L"scrubbing-samples=%d mask=%08x stable-expected=%08x "
         L"status=%r\r\n", offset, first, last, polls,
         scrubbing_samples, TU104_FALCON_DMACTL_SCRUBBING,
         stable_expected, status);
    return EFI_ERROR(log_status) ? log_status : status;
}

#ifdef TU104_FWSEC_FRTS_PROBE
static EFI_STATUS frts_confirm_once(void)
{
    EFI_STATUS status;
    unsigned tick;

    if (!ST || !ST->ConIn || !ST->ConIn->Reset ||
        !ST->ConIn->ReadKeyStroke)
        return EFI_UNSUPPORTED;
    status = uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    if (EFI_ERROR(status))
        return status;
    note(L"READY FOR FIRST ACTIVE TEST: native signed FWSEC FRTS only.\r\n");
    note(L"This will reset/run GSP and change volatile WPR2 state. It does "
         L"NOT run the unlock exploit or write flash.\r\n");
    note(L"Press U within 30 seconds to attempt exactly once. Press Esc or "
         L"wait to cancel safely. After any write attempt you MUST fully "
         L"power off; the program will not return.\r\n");
    if (EFI_ERROR(flush_log()))
        return log_status;
    for (tick = 0; tick < FRTS_CONFIRM_TICKS; ++tick) {
        EFI_INPUT_KEY key;

        SetMem(&key, sizeof(key), 0);
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2,
                                   ST->ConIn, &key);
        if (!EFI_ERROR(status)) {
            if (key.ScanCode == 0x17 || key.UnicodeChar == 27)
                return EFI_ABORTED;
            if (key.UnicodeChar == L'u' || key.UnicodeChar == L'U')
                return EFI_SUCCESS;
        } else if (status != EFI_NOT_READY) {
            return status;
        }
        status = uefi_call_wrapper(BS->Stall, 1, 100000);
        if (EFI_ERROR(status))
            return status;
    }
    return EFI_TIMEOUT;
}

static int frts_core_read32(void *opaque, uint32_t offset, uint32_t *value)
{
    struct frts_probe_io *io = opaque;
    EFI_STATUS status;

    if (!io || !io->target || !value)
        return 1;
    status = uefi_call_wrapper(io->target->Mem.Read, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, value);
    return EFI_ERROR(status);
}

static int frts_core_write32(void *opaque, uint32_t offset, uint32_t value)
{
    struct frts_probe_io *io = opaque;
    EFI_STATUS status;

    if (!io || !io->target ||
        !tu104_fwsec_probe_write_allowed(offset))
        return 1;
    status = uefi_call_wrapper(io->target->Mem.Write, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, &value);
    return EFI_ERROR(status);
}

static int frts_core_delay(void *opaque, uint32_t microseconds)
{
    (void)opaque;
    return EFI_ERROR(uefi_call_wrapper(BS->Stall, 1,
                                       (UINTN)microseconds));
}

static int frts_core_barrier(void *opaque)
{
    struct frts_probe_io *io = opaque;
    UINT32 boot_vector = 0;
    EFI_STATUS status;

    if (!io || !io->target)
        return 1;
    __asm__ __volatile__("mfence" ::: "memory");
    status = uefi_call_wrapper(io->target->Mem.Read, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               TU104_GSP_BASE +
                                   TU104_FALCON_BOOTVEC_OFFSET,
                               1, &boot_vector);
    __asm__ __volatile__("mfence" ::: "memory");
    return EFI_ERROR(status) ||
        boot_vector != TU104_GENERIC_BL_BOOT_VECTOR;
}

static int frts_core_event(void *opaque, unsigned stage,
                           uint32_t first, uint32_t second)
{
    const CHAR16 *name = L"UNKNOWN";

    (void)opaque;
    switch (stage) {
    case TU104_FWSEC_STAGE_GUARDS: name = L"GUARDS"; break;
    case TU104_FWSEC_STAGE_RESET: name = L"RESET"; break;
    case TU104_FWSEC_STAGE_LOADER: name = L"LOADER"; break;
    case TU104_FWSEC_STAGE_START: name = L"START"; break;
    case TU104_FWSEC_STAGE_RESULT: name = L"RESULT"; break;
    case TU104_FWSEC_STAGE_CLEANUP: name = L"CLEANUP"; break;
    case TU104_FWSEC_STAGE_DONE: name = L"DONE"; break;
    default: return 1;
    }
    note(L"FRTS core stage=%s id=%d first=%08x second=%08x\r\n",
         name, stage, first, second);
    return EFI_ERROR(flush_log());
}

static EFI_STATUS frts_core_status_to_efi(int status)
{
    switch (status) {
    case TU104_FWSEC_PROBE_OK: return EFI_SUCCESS;
    case TU104_FWSEC_PROBE_GUARD: return EFI_SECURITY_VIOLATION;
    case TU104_FWSEC_PROBE_IO: return EFI_DEVICE_ERROR;
    case TU104_FWSEC_PROBE_TIMEOUT: return EFI_TIMEOUT;
    case TU104_FWSEC_PROBE_VERIFY: return EFI_COMPROMISED_DATA;
    case TU104_FWSEC_PROBE_LOG:
        return EFI_ERROR(log_status) ? log_status : EFI_DEVICE_ERROR;
    default: return EFI_COMPROMISED_DATA;
    }
}

static __attribute__((noreturn)) void
frts_retain_and_halt(struct tu104_fwsec_probe *probe, int core_status)
{
    if (core_status == TU104_FWSEC_PROBE_OK && probe->result_verified &&
        probe->quiescent) {
        note(L"SUCCESS: native FWSEC reached high physical staging and "
             L"created exact WPR2 LO=%08x HI=%08x.\r\n",
             probe->wpr2_lo_result, probe->wpr2_hi_result);
    } else {
        note(L"STOPPED: active FRTS attempt status=%d stage=%d started=%d "
             L"verified=%d quiescent=%d scratch=%08x WPR2=%08x/%08x.\r\n",
             core_status, probe->stage, probe->firmware_started,
             probe->result_verified, probe->quiescent,
             probe->scratch_result, probe->wpr2_lo_result,
             probe->wpr2_hi_result);
    }
    note(L"Backing memory is intentionally retained. FULLY POWER OFF with "
         L"the physical power button. Do not reboot or continue to an OS.\r\n");
    flush_log();
    if (log_file) {
        uefi_call_wrapper(log_file->Close, 1, log_file);
        log_file = NULL;
    }
    for (;;) {
        uefi_call_wrapper(BS->Stall, 1, 1000000);
        __asm__ __volatile__("pause");
    }
}

static EFI_STATUS
run_fwsec_frts_probe(EFI_PCI_IO_PROTOCOL *target)
{
    struct tu104_fwsec_probe probe;
    struct frts_probe_io io;
    UINT8 *prom = NULL;
    VOID *staging = NULL;
    EFI_PHYSICAL_ADDRESS physical = 0;
    UINTN generic_bytes =
        (UINTN)(_binary_bl_gsp_tu102_bin_end -
                _binary_bl_gsp_tu102_bin_start);
    BOOLEAN pages_allocated = FALSE;
    int core_status = TU104_FWSEC_PROBE_GUARD;
    EFI_STATUS status;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    SetMem(&probe, sizeof(probe), 0);
    SetMem(&io, sizeof(io), 0);
    io.target = target;

    status = run_platform_dma_probe(target);
    note(L"Active-stage complete platform prerequisite status=%r\r\n",
         status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_revalidate_platform(target);
    if (EFI_ERROR(status))
        goto cleanup;
    if (generic_bytes != TU104_GENERIC_BL_IMAGE_BYTES ||
        tu104_fwsec_probe_crc32(_binary_bl_gsp_tu102_bin_start,
                                generic_bytes) !=
            TU104_GENERIC_BL_CRC32) {
        status = EFI_CRC_ERROR;
        goto cleanup;
    }
    status = frts_capture_and_verify_prom(target, &prom);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_allocate_observed_pages(&staging, &physical,
                                          &pages_allocated);
    if (EFI_ERROR(status))
        goto cleanup;
    SetMem(staging, (UINTN)TU104_FWSEC_ALLOCATION_BYTES, 0);

    probe.opaque = &io;
    probe.read32 = frts_core_read32;
    probe.write32 = frts_core_write32;
    probe.delay_us = frts_core_delay;
    probe.barrier = frts_core_barrier;
    probe.event = frts_core_event;
    probe.staging = (uint8_t *)staging;
    probe.staging_bytes = TU104_FWSEC_STAGING_BYTES;
    probe.staging_physical = physical;
    probe.native_fwsec = prom + TU104_FWSEC_IMAGE_PROM_OFFSET;
    probe.native_fwsec_bytes = TU104_FWSEC_STORED_BYTES;
    probe.generic_bl = _binary_bl_gsp_tu102_bin_start;
    probe.generic_bl_bytes = generic_bytes;
    core_status = tu104_fwsec_probe_prepare(&probe);
    note(L"FRTS staging preparation core-status=%d physical=%lx "
         L"logical-bytes=%lx allocated-bytes=%lx crc=%08x\r\n",
         core_status, physical, TU104_FWSEC_STAGING_BYTES,
         TU104_FWSEC_ALLOCATION_BYTES,
         option_rom_crc32((const uint8_t *)staging,
                          TU104_FWSEC_STAGING_BYTES));
    if (core_status != TU104_FWSEC_PROBE_OK) {
        status = frts_core_status_to_efi(core_status);
        goto cleanup;
    }

    status = frts_confirm_once();
    note(L"Physical one-shot confirmation status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_revalidate_platform(target);
    if (EFI_ERROR(status))
        goto cleanup;
    status = uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    note(L"Pre-write watchdog disable status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_wait_sec2_scrub(target);
    if (EFI_ERROR(status))
        goto cleanup;
    note(L"ARMED: beginning target-locked FRTS core. The next successful "
         L"stage transition may perform the first GPU write.\r\n");
    if (EFI_ERROR(flush_log())) {
        status = log_status;
        goto cleanup;
    }

    core_status = tu104_fwsec_probe_run(&probe);
    status = frts_core_status_to_efi(core_status);
    if (tu104_fwsec_page_disposition(probe.mutated) ==
        TU104_FWSEC_RETAIN_AND_HALT)
        frts_retain_and_halt(&probe, core_status);

cleanup:
    if (pages_allocated && !probe.mutated) {
        EFI_STATUS free_status = uefi_call_wrapper(
            BS->FreePages, 2, physical,
            (UINTN)TU104_FWSEC_ALLOCATION_PAGES);

        note(L"Pre-mutation high-page release status=%r\r\n", free_status);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status))
            status = free_status;
    }
    if (prom && !probe.mutated)
        FreePool(prom);
    note(L"Active FRTS stage ended before mutation status=%r; safe to "
         L"return.\r\n", status);
    return status;
}
#endif /* TU104_FWSEC_FRTS_PROBE */
