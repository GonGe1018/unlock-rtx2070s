/* Included only by TU104_UNLOCK_ACTIVE.
 *
 * Board- and platform-pinned wrapper around the offline-tested one-shot core.
 * The exact Ubuntu GRUB child is loaded before physical confirmation.  Once
 * any GPU write is attempted, the wrapper either proves complete unlock and
 * Falcon quiescence before releasing memory and starting GRUB, or retains all
 * backing storage and halts without returning to firmware.
 */

#include "tu104_unlock_lifecycle.h"

#define UNLOCK_CONFIRM_TICKS 300u
#define UNLOCK_MEMORY_RO UINT64_C(0x00020000)

extern const UINT8 _binary_bl_gsp_tu102_bin_start[];
extern const UINT8 _binary_bl_gsp_tu102_bin_end[];
extern const UINT8 _binary_booter_ucode_prod_bin_start[];
extern const UINT8 _binary_booter_ucode_prod_bin_end[];
extern const UINT8 _binary_booter_sig_prod_bin_start[];
extern const UINT8 _binary_booter_sig_prod_bin_end[];
extern const UINT8 _binary_v67_payload_bin_start[];
extern const UINT8 _binary_v67_payload_bin_end[];
extern const UINT8 _binary_gsp_rm_boot_dbg_bin_start[];
extern const UINT8 _binary_gsp_rm_boot_dbg_bin_end[];

struct unlock_uefi_io {
    EFI_PCI_IO_PROTOCOL *target;
    struct tu104_unlock_core *core;
    BOOLEAN write_attempted;
};

static UINTN unlock_asset_bytes(const UINT8 *start, const UINT8 *end)
{
    if (!start || !end || end < start)
        return 0;
    return (UINTN)(end - start);
}

static EFI_STATUS unlock_verify_assets(void)
{
    struct tu104_unlock_layout_input assets;
    UINTN generic_bytes = unlock_asset_bytes(
        _binary_bl_gsp_tu102_bin_start,
        _binary_bl_gsp_tu102_bin_end);

    SetMem(&assets, sizeof(assets), 0);
    assets.booter = _binary_booter_ucode_prod_bin_start;
    assets.booter_bytes = unlock_asset_bytes(
        _binary_booter_ucode_prod_bin_start,
        _binary_booter_ucode_prod_bin_end);
    assets.booter_signature = _binary_booter_sig_prod_bin_start;
    assets.booter_signature_bytes = unlock_asset_bytes(
        _binary_booter_sig_prod_bin_start,
        _binary_booter_sig_prod_bin_end);
    assets.v67 = _binary_v67_payload_bin_start;
    assets.v67_bytes = unlock_asset_bytes(
        _binary_v67_payload_bin_start,
        _binary_v67_payload_bin_end);
    assets.boot_container = _binary_gsp_rm_boot_dbg_bin_start;
    assets.boot_container_bytes = unlock_asset_bytes(
        _binary_gsp_rm_boot_dbg_bin_start,
        _binary_gsp_rm_boot_dbg_bin_end);

    note(L"Embedded assets: generic=%ld/%08x booter=%ld/%08x "
         L"signature=%ld/%08x V67=%ld/%08x boot-container=%ld/%08x\r\n",
         generic_bytes,
         tu104_unlock_layout_crc32(_binary_bl_gsp_tu102_bin_start,
                                    generic_bytes),
         (UINT64)assets.booter_bytes,
         tu104_unlock_layout_crc32(assets.booter, assets.booter_bytes),
         (UINT64)assets.booter_signature_bytes,
         tu104_unlock_layout_crc32(assets.booter_signature,
                                    assets.booter_signature_bytes),
         (UINT64)assets.v67_bytes,
         tu104_unlock_layout_crc32(assets.v67, assets.v67_bytes),
         (UINT64)assets.boot_container_bytes,
         tu104_unlock_layout_crc32(assets.boot_container,
                                    assets.boot_container_bytes));
    if (generic_bytes != TU104_GENERIC_BL_IMAGE_BYTES ||
        tu104_unlock_layout_crc32(_binary_bl_gsp_tu102_bin_start,
                                  generic_bytes) !=
            TU104_GENERIC_BL_CRC32 ||
        !tu104_unlock_assets_match(&assets))
        return EFI_CRC_ERROR;
    return EFI_ERROR(log_status) ? log_status : EFI_SUCCESS;
}

static EFI_STATUS
unlock_verify_observed_high_range(UINT32 expected_type,
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
        UINT64 required_end = TU104_UNLOCK_OBSERVED_HIGH_BASE +
            TU104_UNLOCK_ALLOCATION_BYTES;

        if (descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages > ((~(UINT64)0 - start) >> 12))
            continue;
        end = start + (descriptor->NumberOfPages << 12);
        if (TU104_UNLOCK_OBSERVED_HIGH_BASE < start || required_end > end)
            continue;
        if (descriptor->Type != expected_type ||
            !(descriptor->Attribute & EFI_MEMORY_WB) ||
            (descriptor->Attribute &
             (EFI_MEMORY_WP | EFI_MEMORY_RP | UNLOCK_MEMORY_RO))) {
            FreePool(map);
            return EFI_ACCESS_DENIED;
        }
        ++matches;
    }
    FreePool(map);
    note(L"Unlock high-range %s guard: base=%lx bytes=%lx pages=%ld "
         L"expected-type=%d covering-descriptors=%d\r\n", phase,
         TU104_UNLOCK_OBSERVED_HIGH_BASE,
         TU104_UNLOCK_ALLOCATION_BYTES,
         TU104_UNLOCK_ALLOCATION_PAGES, expected_type, matches);
    if (EFI_ERROR(log_status))
        return log_status;
    return matches == 1 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS
unlock_allocate_observed_pages(VOID **pool, EFI_PHYSICAL_ADDRESS *physical,
                               BOOLEAN *allocated)
{
    EFI_STATUS status;

    if (!pool || !physical || !allocated)
        return EFI_INVALID_PARAMETER;
    *pool = NULL;
    *allocated = FALSE;
    *physical = TU104_UNLOCK_OBSERVED_HIGH_BASE;
    status = unlock_verify_observed_high_range(
        EfiConventionalMemory, L"pre-allocation");
    if (EFI_ERROR(status))
        return status;
    if (EFI_ERROR(flush_log()))
        return log_status;
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress,
                               EfiBootServicesData,
                               (UINTN)TU104_UNLOCK_ALLOCATION_PAGES,
                               physical);
    note(L"Unlock AllocateAddress status=%r requested=%lx returned=%lx\r\n",
         status, TU104_UNLOCK_OBSERVED_HIGH_BASE, *physical);
    if (EFI_ERROR(status))
        return status;
    *allocated = TRUE;
    *pool = (VOID *)(UINTN)*physical;
    if (*physical != TU104_UNLOCK_OBSERVED_HIGH_BASE ||
        !tu104_unlock_allocation_suitable(
            *physical, (size_t)TU104_UNLOCK_ALLOCATION_BYTES))
        return EFI_COMPROMISED_DATA;
    return unlock_verify_observed_high_range(
        EfiBootServicesData, L"owned-post-allocation");
}

static EFI_STATUS unlock_confirm_once(void)
{
    EFI_STATUS status;
    unsigned tick;

    if (!ST || !ST->ConIn || !ST->ConIn->Reset ||
        !ST->ConIn->ReadKeyStroke)
        return EFI_UNSUPPORTED;
    status = uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    if (EFI_ERROR(status))
        return status;
    note(L"READY: exact Ubuntu GRUB is preloaded and the volatile full-unlock "
         L"input pool passed all offline guards.\r\n");
    note(L"This one-shot attempt will reset/run GSP and SEC2 and execute the "
         L"production Booter with the V67 payload. It does NOT write flash.\r\n");
    note(L"Press U within 30 seconds to run once. Press Esc or wait to cancel "
         L"safely. After U, Ubuntu starts only on exact verified success.\r\n");
    note(L"If Ubuntu does not start automatically, FULLY POWER OFF. Do not "
         L"reboot: the frozen screen means fail-closed memory retention.\r\n");
    if (EFI_ERROR(flush_log()))
        return log_status;
    for (tick = 0; tick < UNLOCK_CONFIRM_TICKS; ++tick) {
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

static int unlock_core_read32(void *opaque, uint32_t offset,
                              uint32_t *value)
{
    struct unlock_uefi_io *io = opaque;
    EFI_STATUS status;

    if (!io || !io->target || !value)
        return 1;
    status = uefi_call_wrapper(io->target->Mem.Read, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, value);
    return EFI_ERROR(status);
}

static int unlock_core_write32(void *opaque, uint32_t offset,
                               uint32_t value)
{
    struct unlock_uefi_io *io = opaque;
    EFI_STATUS status;

    if (!io || !io->target || !tu104_unlock_write_allowed(offset))
        return 1;
    io->write_attempted = TRUE;
    status = uefi_call_wrapper(io->target->Mem.Write, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, &value);
    return EFI_ERROR(status);
}

static int unlock_core_delay(void *opaque, uint32_t microseconds)
{
    (void)opaque;
    return EFI_ERROR(uefi_call_wrapper(BS->Stall, 1,
                                       (UINTN)microseconds));
}

static int unlock_core_barrier(void *opaque)
{
    struct unlock_uefi_io *io = opaque;
    UINT32 offset;
    UINT32 expected;
    UINT32 value = 0;
    EFI_STATUS status;

    if (!io || !io->target || !io->core)
        return 1;
    if (io->core->stage == TU104_UNLOCK_STAGE_FRTS_START) {
        offset = TU104_GSP_BASE + TU104_FALCON_BOOTVEC_OFFSET;
        expected = TU104_GENERIC_BL_BOOT_VECTOR;
    } else if (io->core->stage == TU104_UNLOCK_STAGE_BOOTER_START) {
        offset = TU104_SEC2_BASE + TU104_FALCON_BOOTVEC_OFFSET;
        expected = 0;
    } else {
        return 1;
    }
    __asm__ __volatile__("mfence" ::: "memory");
    status = uefi_call_wrapper(io->target->Mem.Read, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, &value);
    __asm__ __volatile__("mfence" ::: "memory");
    return EFI_ERROR(status) || value != expected;
}

static int unlock_core_event(void *opaque, unsigned stage,
                             uint32_t first, uint32_t second)
{
    struct unlock_uefi_io *io = opaque;

    if (!io || !io->core || stage == TU104_UNLOCK_STAGE_NONE ||
        stage >= TU104_UNLOCK_STAGE_COUNT)
        return 1;
    if (io->write_attempted || io->core->mutated)
        return 0;
    note(L"Unlock pre-write core stage=%d first=%08x second=%08x\r\n",
         stage, first, second);
    return EFI_ERROR(flush_log());
}

static int unlock_core_transition(void *opaque)
{
    struct unlock_uefi_io *io = opaque;
    struct tu104_unlock_core *core;

    if (!io || !io->core)
        return 1;
    core = io->core;
    __asm__ __volatile__("mfence" ::: "memory");
    if (!io->write_attempted || !core->mutated ||
        !core->frts.result_verified || !core->frts.quiescent ||
        core->prepared_control_crc32 != tu104_unlock_layout_crc32(
            core->pool, TU104_UNLOCK_DATA_OFFSET))
        return 1;
    __asm__ __volatile__("mfence" ::: "memory");
    return 0;
}

static EFI_STATUS unlock_core_status_to_efi(int status)
{
    switch (status) {
    case TU104_UNLOCK_OK: return EFI_SUCCESS;
    case TU104_UNLOCK_GUARD: return EFI_SECURITY_VIOLATION;
    case TU104_UNLOCK_IO: return EFI_DEVICE_ERROR;
    case TU104_UNLOCK_TIMEOUT: return EFI_TIMEOUT;
    case TU104_UNLOCK_VERIFY: return EFI_COMPROMISED_DATA;
    case TU104_UNLOCK_LOG:
        return EFI_ERROR(log_status) ? log_status : EFI_DEVICE_ERROR;
    default: return EFI_COMPROMISED_DATA;
    }
}

static __attribute__((noreturn)) void unlock_retain_and_halt(void)
{
    __asm__ __volatile__("cli" ::: "memory");
    for (;;)
        __asm__ __volatile__("hlt");
}

static EFI_STATUS
run_tu104_unlock_active(EFI_HANDLE image, EFI_PCI_IO_PROTOCOL *target)
{
    struct ubuntu_handoff handoff;
    struct tu104_unlock_core core;
    struct tu104_unlock_lifecycle_state lifecycle;
    struct unlock_uefi_io io;
    enum tu104_unlock_lifecycle_action action;
    UINT8 *prom = NULL;
    VOID *pool = NULL;
    EFI_PHYSICAL_ADDRESS physical = 0;
    BOOLEAN pages_allocated = FALSE;
    BOOLEAN mutated = FALSE;
    int core_status = TU104_UNLOCK_GUARD;
    EFI_STATUS status = EFI_NOT_READY;

    if (!image || !target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    SetMem(&handoff, sizeof(handoff), 0);
    SetMem(&core, sizeof(core), 0);
    SetMem(&lifecycle, sizeof(lifecycle), 0);
    SetMem(&io, sizeof(io), 0);

    status = run_platform_dma_probe(target);
    note(L"Unlock complete platform prerequisite status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_revalidate_platform(target);
    if (EFI_ERROR(status))
        goto cleanup;
    status = unlock_verify_assets();
    if (EFI_ERROR(status))
        goto cleanup;
    status = ubuntu_handoff_preload(image, &handoff);
    note(L"Pre-mutation exact Ubuntu GRUB preload status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_capture_and_verify_prom(target, &prom);
    if (EFI_ERROR(status))
        goto cleanup;
    status = unlock_allocate_observed_pages(&pool, &physical,
                                            &pages_allocated);
    if (EFI_ERROR(status))
        goto cleanup;

    io.target = target;
    io.core = &core;
    core.opaque = &io;
    core.read32 = unlock_core_read32;
    core.write32 = unlock_core_write32;
    core.delay_us = unlock_core_delay;
    core.barrier = unlock_core_barrier;
    core.event = unlock_core_event;
    core.next_input = unlock_core_transition;
    core.pool = (uint8_t *)pool;
    core.pool_bytes = (size_t)TU104_UNLOCK_ALLOCATION_BYTES;
    core.pool_physical = physical;
    core.native_fwsec = prom + TU104_FWSEC_IMAGE_PROM_OFFSET;
    core.native_fwsec_bytes = TU104_FWSEC_STORED_BYTES;
    core.generic_bl = _binary_bl_gsp_tu102_bin_start;
    core.generic_bl_bytes = unlock_asset_bytes(
        _binary_bl_gsp_tu102_bin_start,
        _binary_bl_gsp_tu102_bin_end);
    core.booter = _binary_booter_ucode_prod_bin_start;
    core.booter_bytes = unlock_asset_bytes(
        _binary_booter_ucode_prod_bin_start,
        _binary_booter_ucode_prod_bin_end);
    core.booter_signature = _binary_booter_sig_prod_bin_start;
    core.booter_signature_bytes = unlock_asset_bytes(
        _binary_booter_sig_prod_bin_start,
        _binary_booter_sig_prod_bin_end);
    core.v67 = _binary_v67_payload_bin_start;
    core.v67_bytes = unlock_asset_bytes(
        _binary_v67_payload_bin_start,
        _binary_v67_payload_bin_end);
    core.boot_container = _binary_gsp_rm_boot_dbg_bin_start;
    core.boot_container_bytes = unlock_asset_bytes(
        _binary_gsp_rm_boot_dbg_bin_start,
        _binary_gsp_rm_boot_dbg_bin_end);
    core_status = tu104_unlock_prepare(&core);
    note(L"Full unlock preparation core-status=%d physical=%lx bytes=%lx "
         L"control-crc=%08x\r\n", core_status, physical,
         TU104_UNLOCK_ALLOCATION_BYTES, core.prepared_control_crc32);
    if (core_status != TU104_UNLOCK_OK) {
        status = unlock_core_status_to_efi(core_status);
        goto cleanup;
    }

    status = unlock_confirm_once();
    note(L"Physical full-unlock one-shot confirmation status=%r\r\n",
         status);
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
    note(L"ARMED: exactly one full-unlock core invocation begins next. "
         L"Progress output is suppressed after the first write. On failure "
         L"this screen remains frozen and physical power-off is required.\r\n");
    if (EFI_ERROR(flush_log())) {
        status = log_status;
        goto cleanup;
    }

    core_status = tu104_unlock_run(&core);
    mutated = core.mutated || io.write_attempted;
    lifecycle.allocation_physical = physical;
    lifecycle.allocation_bytes = (size_t)TU104_UNLOCK_ALLOCATION_BYTES;
    lifecycle.allocation_owned = pages_allocated;
    lifecycle.handoff_preloaded = handoff.loaded;
    lifecycle.mutated = mutated;
    lifecycle.run_status = core_status;
    lifecycle.stage = core.stage;
    lifecycle.unlocked = core.unlocked;
    lifecycle.quiescent = core.quiescent;
    action = tu104_unlock_lifecycle_decide(&lifecycle);
    if (action == TU104_UNLOCK_RETAIN_AND_HALT)
        unlock_retain_and_halt();
    if (action != TU104_UNLOCK_RELEASE_AND_HANDOFF) {
        status = unlock_core_status_to_efi(core_status);
        goto cleanup;
    }

    note(L"SUCCESS: volatile TU104 unlock verified SS0=%08x SS1=%08x "
         L"mailbox0=%08x stage=%d quiescent=%d.\r\n",
         core.ss0_result, core.ss1_result, core.booter_mailbox0,
         core.stage, core.quiescent);
    status = uefi_call_wrapper(BS->FreePages, 2, physical,
                               (UINTN)TU104_UNLOCK_ALLOCATION_PAGES);
    note(L"Verified-success 32-MiB pool release status=%r\r\n", status);
    if (EFI_ERROR(status))
        unlock_retain_and_halt();
    pages_allocated = FALSE;
    if (prom) {
        FreePool(prom);
        prom = NULL;
    }
    note(L"Unlock backing memory released only after quiescence; starting "
         L"the already-preloaded exact Ubuntu GRUB now.\r\n");
    status = ubuntu_handoff_start(&handoff);
    (void)status;
    unlock_retain_and_halt();

cleanup:
    mutated = core.mutated || io.write_attempted;
    if (mutated)
        unlock_retain_and_halt();
    if (pages_allocated) {
        EFI_STATUS free_status = uefi_call_wrapper(
            BS->FreePages, 2, physical,
            (UINTN)TU104_UNLOCK_ALLOCATION_PAGES);

        note(L"Pre-mutation unlock-pool release status=%r\r\n", free_status);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status))
            status = free_status;
    }
    if (prom)
        FreePool(prom);
    ubuntu_handoff_release(&handoff);
    note(L"Full unlock candidate ended before any GPU write status=%r; "
         L"safe to return.\r\n", status);
    return status;
}
