/* Included only by TU104_FWSEC_GUARD_PROBE after fwsec_frts_probe.inc.
 *
 * This image reproduces the 0.7.0 pre-mutation sequence through physical
 * confirmation, platform revalidation, and watchdog disable.  It then calls
 * the real core guard function directly with a trace-only MMIO-read callback.
 * The active core state machine is never called, the write callback always
 * fails closed without hardware access, and the fixed high allocation is
 * always freed before returning.
 */

#define FWSEC_GUARD_CONTROL_COUNT 11u
#define FWSEC_GUARD_DESCRIPTOR_DWORDS 15u
#define FWSEC_GUARD_FALCON_COUNT TU104_FALCON_FIELD_COUNT
#define FWSEC_GUARD_SNAPSHOT_READS \
    (FWSEC_GUARD_CONTROL_COUNT + 2u * FWSEC_GUARD_FALCON_COUNT)
#define FWSEC_GUARD_IMAGE_DWORDS \
    (TU104_FWSEC_STORED_BYTES / sizeof(UINT32))
#define FWSEC_GUARD_SECOND_SNAPSHOT_INDEX \
    (FWSEC_GUARD_SNAPSHOT_READS + FWSEC_GUARD_DESCRIPTOR_DWORDS + \
     FWSEC_GUARD_IMAGE_DWORDS)
#define FWSEC_GUARD_EXPECTED_READS \
    (FWSEC_GUARD_SECOND_SNAPSHOT_INDEX + FWSEC_GUARD_SNAPSHOT_READS)

struct fwsec_guard_definition {
    const CHAR16 *name;
    UINT32 offset;
    UINT32 expected;
};

struct fwsec_guard_trace_entry {
    UINT32 offset;
    UINT32 value;
    EFI_STATUS status;
};

struct fwsec_guard_trace_io {
    EFI_PCI_IO_PROTOCOL *target;
    struct fwsec_guard_trace_entry *entries;
    UINTN count;
    UINTN event_count;
    UINTN write_attempts;
    unsigned event_stage;
    BOOLEAN overflow;
};

struct fwsec_guard_trace_summary {
    UINTN mismatches;
    BOOLEAN first_set;
    const CHAR16 *first_group;
    UINTN first_index;
    UINT32 first_expected_offset;
    UINT32 first_observed_offset;
    UINT32 first_expected;
    UINT32 first_observed;
    EFI_STATUS first_status;
};

static const struct fwsec_guard_definition fwsec_guard_controls[] = {
    { L"BOOT0",                UINT32_C(0x00000000), TU104_BOOT0 },
    { L"FECS_FEATURE_PLM",     UINT32_C(0x00409650), UINT32_C(0xffffff8f) },
    { L"FECS_FEATURE_READOUT", UINT32_C(0x00409660), UINT32_C(0x000002f3) },
    { L"FECS_SS0",             UINT32_C(0x00409664), UINT32_C(0x00000000) },
    { L"FECS_SPEED_READOUT",   UINT32_C(0x00409668), UINT32_C(0x00400080) },
    { L"FECS_SS1",             UINT32_C(0x0040966c), UINT32_C(0x00000006) },
    { L"WPR2_LO",              TU104_WPR2_LO_OFFSET, UINT32_C(0x1ffffe00) },
    { L"WPR2_HI",              TU104_WPR2_HI_OFFSET, UINT32_C(0x00000000) },
    { L"GFW_BOOT_OK",          UINT32_C(0x00118234), UINT32_C(0x000003ff) },
    { L"VGA_WORKSPACE_BASE",   UINT32_C(0x00625f04), UINT32_C(0x00020e09) },
    { L"VBIOS_SCRATCH14",      TU104_FRTS_SCRATCH_OFFSET, UINT32_C(0x00000000) },
};

static const UINT32 fwsec_guard_descriptor[FWSEC_GUARD_DESCRIPTOR_DWORDS] = {
    UINT32_C(0x003c0201), UINT32_C(0x00009df0),
    UINT32_C(0x00009df0), UINT32_C(0x00000000),
    UINT32_C(0x000000e0), UINT32_C(0x00000000),
    UINT32_C(0x00009a00), UINT32_C(0x00000000),
    UINT32_C(0x00000400), UINT32_C(0x00009600),
    UINT32_C(0x00009a00), UINT32_C(0x00000000),
    UINT32_C(0x000003f0), UINT32_C(0x00009a00),
    UINT32_C(0x00006638),
};

typedef char fwsec_guard_control_count_must_match[
    sizeof(fwsec_guard_controls) / sizeof(fwsec_guard_controls[0]) ==
        FWSEC_GUARD_CONTROL_COUNT ? 1 : -1
];
typedef char fwsec_guard_read_count_must_match[
    FWSEC_GUARD_EXPECTED_READS == 10197u ? 1 : -1
];

static EFI_STATUS fwsec_guard_confirm_once(void)
{
    EFI_STATUS status;
    unsigned tick;

    if (!ST || !ST->ConIn || !ST->ConIn->Reset ||
        !ST->ConIn->ReadKeyStroke)
        return EFI_UNSUPPORTED;
    status = uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    if (EFI_ERROR(status))
        return status;
    note(L"READY FOR READ-ONLY CORE-GUARD REPLAY.\r\n");
    note(L"Press U within 30 seconds to reproduce the 0.7.0 pre-guard "
         L"timing. This diagnostic performs no GPU write/reset/firmware "
         L"execution; Esc or timeout cancels safely.\r\n");
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

static int fwsec_guard_trace_read(void *opaque, uint32_t offset,
                                  uint32_t *value)
{
    struct fwsec_guard_trace_io *io = opaque;
    struct fwsec_guard_trace_entry *entry;
    EFI_STATUS status;

    if (!io || !io->target || !io->entries || !value)
        return 1;
    if (io->count >= FWSEC_GUARD_EXPECTED_READS) {
        io->overflow = TRUE;
        return 1;
    }
    entry = &io->entries[io->count++];
    entry->offset = offset;
    entry->value = 0;
    status = uefi_call_wrapper(io->target->Mem.Read, 6, io->target,
                               EfiPciIoWidthUint32, 0,
                               (UINT64)offset, 1, &entry->value);
    entry->status = status;
    *value = entry->value;
    return EFI_ERROR(status);
}

static int fwsec_guard_trace_write(void *opaque, uint32_t offset,
                                   uint32_t value)
{
    struct fwsec_guard_trace_io *io = opaque;

    (void)offset;
    (void)value;
    if (io)
        ++io->write_attempts;
    return 1;
}

static int fwsec_guard_trace_delay(void *opaque, uint32_t usec)
{
    (void)opaque;
    (void)usec;
    return 1;
}

static int fwsec_guard_trace_barrier(void *opaque)
{
    (void)opaque;
    return 1;
}

static int fwsec_guard_trace_event(void *opaque, unsigned stage,
                                   uint32_t first, uint32_t second)
{
    struct fwsec_guard_trace_io *io = opaque;

    if (!io || stage != TU104_FWSEC_STAGE_GUARDS || io->event_count != 0)
        return 1;
    ++io->event_count;
    io->event_stage = stage;
    note(L"FRTS core stage=GUARDS id=%d first=%08x second=%08x\r\n",
         stage, first, second);
    return EFI_ERROR(flush_log());
}

static BOOLEAN
fwsec_guard_snapshot_expected(UINTN index, UINT32 *offset, UINT32 *value,
                              const CHAR16 **group)
{
    UINTN engine;
    UINTN field;
    const UINT32 bases[2] = { TU104_GSP_BASE, TU104_SEC2_BASE };
    const UINT32 *expected[2] = {
        tu104_gsp_initial_values, tu104_sec2_initial_values
    };
    const CHAR16 *groups[2] = { L"GSP", L"SEC2" };

    if (!offset || !value || !group || index >= FWSEC_GUARD_SNAPSHOT_READS)
        return FALSE;
    if (index < FWSEC_GUARD_CONTROL_COUNT) {
        *offset = fwsec_guard_controls[index].offset;
        *value = fwsec_guard_controls[index].expected;
        *group = L"CONTROL";
        return TRUE;
    }
    index -= FWSEC_GUARD_CONTROL_COUNT;
    engine = index / FWSEC_GUARD_FALCON_COUNT;
    field = index % FWSEC_GUARD_FALCON_COUNT;
    if (engine >= 2)
        return FALSE;
    *offset = bases[engine] + tu104_falcon_relative_offsets[field];
    *value = expected[engine][field];
    *group = groups[engine];
    return TRUE;
}

static BOOLEAN
fwsec_guard_expected_at(UINTN index, const UINT8 *native_fwsec,
                        UINT32 *offset, UINT32 *value,
                        const CHAR16 **group)
{
    UINTN relative;

    if (!native_fwsec || !offset || !value || !group ||
        index >= FWSEC_GUARD_EXPECTED_READS)
        return FALSE;
    if (index < FWSEC_GUARD_SNAPSHOT_READS)
        return fwsec_guard_snapshot_expected(index, offset, value, group);
    if (index < FWSEC_GUARD_SNAPSHOT_READS +
                    FWSEC_GUARD_DESCRIPTOR_DWORDS) {
        relative = index - FWSEC_GUARD_SNAPSHOT_READS;
        *offset = TU104_FWSEC_DESCRIPTOR_MMIO +
            (UINT32)(relative * sizeof(UINT32));
        *value = fwsec_guard_descriptor[relative];
        *group = L"DESCRIPTOR";
        return TRUE;
    }
    if (index < FWSEC_GUARD_SECOND_SNAPSHOT_INDEX) {
        relative = index - FWSEC_GUARD_SNAPSHOT_READS -
            FWSEC_GUARD_DESCRIPTOR_DWORDS;
        *offset = TU104_FWSEC_IMAGE_MMIO +
            (UINT32)(relative * sizeof(UINT32));
        *value = tu104_fwsec_probe_le32(
            native_fwsec + relative * sizeof(UINT32));
        *group = L"FWSEC_IMAGE";
        return TRUE;
    }
    return fwsec_guard_snapshot_expected(
        index - FWSEC_GUARD_SECOND_SNAPSHOT_INDEX,
        offset, value, group);
}

static EFI_STATUS fwsec_guard_core_status_to_efi(int status)
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

static EFI_STATUS
fwsec_guard_summarize_trace(const struct fwsec_guard_trace_io *io,
                            const UINT8 *native_fwsec, int core_status)
{
    struct fwsec_guard_trace_summary summary;
    UINTN index;
    EFI_STATUS status;

    if (!io || !io->entries || !native_fwsec)
        return EFI_INVALID_PARAMETER;
    SetMem(&summary, sizeof(summary), 0);
    summary.first_group = L"NONE";
    summary.first_status = EFI_SUCCESS;
    for (index = 0; index < io->count; ++index) {
        const struct fwsec_guard_trace_entry *entry = &io->entries[index];
        const CHAR16 *group = L"SEQUENCE";
        UINT32 expected_offset = 0;
        UINT32 expected_value = 0;
        BOOLEAN mapped = fwsec_guard_expected_at(
            index, native_fwsec, &expected_offset, &expected_value, &group);

        if (mapped && !EFI_ERROR(entry->status) &&
            entry->offset == expected_offset &&
            entry->value == expected_value)
            continue;
        ++summary.mismatches;
        if (!summary.first_set) {
            summary.first_set = TRUE;
            summary.first_group = group;
            summary.first_index = index;
            summary.first_expected_offset = expected_offset;
            summary.first_observed_offset = entry->offset;
            summary.first_expected = expected_value;
            summary.first_observed = entry->value;
            summary.first_status = entry->status;
        }
    }

    note(L"CORE-GUARD REPLAY SUMMARY core-status=%d reads=%d "
         L"expected-reads=%d events=%d event-stage=%d write-attempts=%d "
         L"overflow=%d mismatches=%d\r\n",
         core_status, io->count, FWSEC_GUARD_EXPECTED_READS,
         io->event_count, io->event_stage, io->write_attempts,
         io->overflow, summary.mismatches);
    note(L"CORE-GUARD FIRST FAILURE group=%s index=%d "
         L"expected-offset=%08x observed-offset=%08x expected=%08x "
         L"observed=%08x read-status=%r\r\n",
         summary.first_group, summary.first_index,
         summary.first_expected_offset, summary.first_observed_offset,
         summary.first_expected, summary.first_observed,
         summary.first_status);
    if (EFI_ERROR(log_status))
        return log_status;

    status = fwsec_guard_core_status_to_efi(core_status);
    if (core_status == TU104_FWSEC_PROBE_OK &&
        io->count == FWSEC_GUARD_EXPECTED_READS &&
        io->event_count == 1 &&
        io->event_stage == TU104_FWSEC_STAGE_GUARDS &&
        io->write_attempts == 0 && !io->overflow &&
        summary.mismatches == 0)
        return EFI_SUCCESS;
    if (!EFI_ERROR(status))
        return EFI_COMPROMISED_DATA;
    return status;
}

static EFI_STATUS
run_fwsec_guard_probe(EFI_PCI_IO_PROTOCOL *target)
{
    struct tu104_fwsec_probe probe;
    struct fwsec_guard_trace_io io;
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
    note(L"Guard-replay complete platform prerequisite status=%r\r\n",
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
    io.entries = AllocatePool(
        (UINTN)FWSEC_GUARD_EXPECTED_READS * sizeof(*io.entries));
    if (!io.entries) {
        status = EFI_OUT_OF_RESOURCES;
        goto cleanup;
    }
    SetMem(io.entries,
           (UINTN)FWSEC_GUARD_EXPECTED_READS * sizeof(*io.entries), 0);

    probe.opaque = &io;
    probe.read32 = fwsec_guard_trace_read;
    probe.write32 = fwsec_guard_trace_write;
    probe.delay_us = fwsec_guard_trace_delay;
    probe.barrier = fwsec_guard_trace_barrier;
    probe.event = fwsec_guard_trace_event;
    probe.staging = (uint8_t *)staging;
    probe.staging_bytes = TU104_FWSEC_STAGING_BYTES;
    probe.staging_physical = physical;
    probe.native_fwsec = prom + TU104_FWSEC_IMAGE_PROM_OFFSET;
    probe.native_fwsec_bytes = TU104_FWSEC_STORED_BYTES;
    probe.generic_bl = _binary_bl_gsp_tu102_bin_start;
    probe.generic_bl_bytes = generic_bytes;
    core_status = tu104_fwsec_probe_prepare(&probe);
    note(L"Guard-replay staging preparation core-status=%d physical=%lx "
         L"logical-bytes=%lx allocated-bytes=%lx crc=%08x\r\n",
         core_status, physical, TU104_FWSEC_STAGING_BYTES,
         TU104_FWSEC_ALLOCATION_BYTES,
         option_rom_crc32((const uint8_t *)staging,
                          TU104_FWSEC_STAGING_BYTES));
    if (core_status != TU104_FWSEC_PROBE_OK) {
        status = fwsec_guard_core_status_to_efi(core_status);
        goto cleanup;
    }

    status = fwsec_guard_confirm_once();
    note(L"Read-only replay physical confirmation status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_revalidate_platform(target);
    if (EFI_ERROR(status))
        goto cleanup;
    status = uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    note(L"Read-only replay watchdog disable status=%r\r\n", status);
    if (EFI_ERROR(status))
        goto cleanup;
    status = frts_wait_sec2_scrub(target);
    if (EFI_ERROR(status))
        goto cleanup;
    note(L"ARMED READ-ONLY REPLAY: invoking only the real core GUARDS "
         L"function. No reset/write/start function is reachable.\r\n");
    if (EFI_ERROR(flush_log())) {
        status = log_status;
        goto cleanup;
    }

    core_status = tu104_fwsec_probe_guards(&probe);
    status = fwsec_guard_summarize_trace(&io, probe.native_fwsec,
                                         core_status);
    if (!EFI_ERROR(status))
        note(L"SUCCESS: the real 0.7.0 core GUARDS function accepted all "
             L"10,197 traced reads under the active timing sequence.\r\n");

cleanup:
    if (pages_allocated) {
        EFI_STATUS free_status = uefi_call_wrapper(
            BS->FreePages, 2, physical,
            (UINTN)TU104_FWSEC_ALLOCATION_PAGES);

        note(L"Guard-replay high-page release status=%r\r\n", free_status);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status))
            status = free_status;
    }
    if (prom)
        FreePool(prom);
    if (io.entries)
        FreePool(io.entries);
    note(L"GPU-read-only core-guard replay complete status=%r; no GPU "
         L"write/reset/firmware execution occurred.\r\n", status);
    return EFI_ERROR(log_status) ? log_status : status;
}
