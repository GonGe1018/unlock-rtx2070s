/* Included only by the explicitly selected TU104_FALCON_STATE_PROBE build.
 *
 * This stage reads a fixed list of GSP/SEC2 Falcon control and FBIF registers
 * twice.  It does not read IMEM/DMEM data ports and contains no write, DMA,
 * reset, or firmware-execution path.
 */
#include "pci_bar_resource.h"

#define FALCON_STATE_EXPECTED_BOOT0 UINT32_C(0x164000a1)
#define FALCON_STATE_EXPECTED_BAR0 UINT32_C(0xa3000000)
#define FALCON_STATE_EXPECTED_BAR0_BYTES UINT64_C(0x01000000)
#define FALCON_STATE_PASSES 2u
#define FALCON_STATE_COUNT 26u

struct falcon_state_definition {
    const char *name;
    UINT32 offset;
};

static const struct falcon_state_definition falcon_state_definitions[] = {
    { "GSP_MAILBOX0",   UINT32_C(0x110040) },
    { "GSP_MAILBOX1",   UINT32_C(0x110044) },
    { "GSP_RM",         UINT32_C(0x110084) },
    { "GSP_DEBUGINFO",  UINT32_C(0x110094) },
    { "GSP_HWCFG2",     UINT32_C(0x1100f4) },
    { "GSP_CPUCTL",     UINT32_C(0x110100) },
    { "GSP_BOOTVEC",    UINT32_C(0x110104) },
    { "GSP_HWCFG",      UINT32_C(0x110108) },
    { "GSP_DMACTL",     UINT32_C(0x11010c) },
    { "GSP_ENGINE",     UINT32_C(0x1103c0) },
    { "GSP_TRANSCFG0",  UINT32_C(0x110600) },
    { "GSP_TRANSCFG4",  UINT32_C(0x110610) },
    { "GSP_FBIF_CTL",   UINT32_C(0x110624) },
    { "SEC2_MAILBOX0",  UINT32_C(0x840040) },
    { "SEC2_MAILBOX1",  UINT32_C(0x840044) },
    { "SEC2_RM",        UINT32_C(0x840084) },
    { "SEC2_DEBUGINFO", UINT32_C(0x840094) },
    { "SEC2_HWCFG2",    UINT32_C(0x8400f4) },
    { "SEC2_CPUCTL",    UINT32_C(0x840100) },
    { "SEC2_BOOTVEC",   UINT32_C(0x840104) },
    { "SEC2_HWCFG",     UINT32_C(0x840108) },
    { "SEC2_DMACTL",    UINT32_C(0x84010c) },
    { "SEC2_ENGINE",    UINT32_C(0x8403c0) },
    { "SEC2_TRANSCFG0", UINT32_C(0x840600) },
    { "SEC2_TRANSCFG4", UINT32_C(0x840610) },
    { "SEC2_FBIF_CTL",  UINT32_C(0x840624) },
};

typedef char falcon_state_count_must_match[
    sizeof(falcon_state_definitions) /
        sizeof(falcon_state_definitions[0]) == FALCON_STATE_COUNT ? 1 : -1
];

static EFI_STATUS verify_falcon_state_bar(EFI_PCI_IO_PROTOCOL *target)
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
    note(L"Falcon-state config guard: id=%08x sub=%08x class=%08x "
         L"command=%04x BAR0=%08x ROM=%08x\r\n",
         config[0], config[11], config[2], config[1] & 0xffffu,
         config[4], config[12]);
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
        resource.minimum != FALCON_STATE_EXPECTED_BAR0 || !maximum_ok ||
        resource.translation != 0 ||
        resource.length != FALCON_STATE_EXPECTED_BAR0_BYTES)
        status = EFI_UNSUPPORTED;
    else
        status = EFI_SUCCESS;
    FreePool(descriptor);
    return status;
}

static EFI_STATUS run_falcon_state_probe(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 values[FALCON_STATE_PASSES][FALCON_STATE_COUNT];
    UINT32 boot0_first = 0;
    UINT32 boot0_second = 0;
    UINTN pass;
    UINTN index;
    UINTN stable = 0;
    EFI_STATUS status;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    status = verify_falcon_state_bar(target);
    note(L"Falcon-state BAR/config guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;
    if (!target->RomImage ||
        target->RomSize != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE ||
        option_rom_crc32((const uint8_t *)target->RomImage,
                         (size_t)target->RomSize) != UINT32_C(0xbf35aca7)) {
        note(L"Firmware option-ROM guard failed; Falcon-state probe refused.\r\n");
        return EFI_COMPROMISED_DATA;
    }

    note(L"BEGIN bounded Falcon-state probe: BOOT0 twice, "
         L"26 named registers twice.\r\n");
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
         boot0_first, boot0_second, FALCON_STATE_EXPECTED_BOOT0, status);
    if (EFI_ERROR(status) || boot0_first != FALCON_STATE_EXPECTED_BOOT0 ||
        boot0_second != FALCON_STATE_EXPECTED_BOOT0)
        return EFI_COMPROMISED_DATA;

    SetMem(values, sizeof(values), 0);
    for (pass = 0; pass < FALCON_STATE_PASSES; ++pass) {
        note(L"Falcon-state pass %d begin.\r\n", pass + 1);
        for (index = 0; index < FALCON_STATE_COUNT; ++index) {
            status = uefi_call_wrapper(target->Mem.Read, 6, target,
                                       EfiPciIoWidthUint32, 0,
                                       falcon_state_definitions[index].offset,
                                       1, &values[pass][index]);
            note(L"FALCON pass=%d name=%a offset=%08x value=%08x "
                 L"status=%r\r\n",
                 pass + 1, falcon_state_definitions[index].name,
                 falcon_state_definitions[index].offset,
                 values[pass][index], status);
            if (EFI_ERROR(status) || EFI_ERROR(log_status))
                return EFI_ERROR(status) ? status : log_status;
        }
    }

    for (index = 0; index < FALCON_STATE_COUNT; ++index) {
        BOOLEAN equal = values[0][index] == values[1][index];
        BOOLEAN readable = values[0][index] != UINT32_C(0xffffffff) &&
            (values[0][index] & UINT32_C(0xffff0000)) !=
                UINT32_C(0xbadf0000);

        if (equal && readable)
            ++stable;
        note(L"FALCON compare name=%a offset=%08x first=%08x second=%08x "
             L"stable=%d readable=%d\r\n",
             falcon_state_definitions[index].name,
             falcon_state_definitions[index].offset,
             values[0][index], values[1][index], equal, readable);
    }
    note(L"Falcon-state summary: stable-readable=%d total=%d "
         L"GSP-IMEM-blocks=%d SEC2-IMEM-blocks=%d.\r\n",
         stable, FALCON_STATE_COUNT,
         values[0][7] & UINT32_C(0x1ff),
         values[0][20] & UINT32_C(0x1ff));
    if (stable != FALCON_STATE_COUNT)
        return EFI_COMPROMISED_DATA;
    note(L"END bounded Falcon-state probe status=Success\r\n");
    return EFI_SUCCESS;
}
