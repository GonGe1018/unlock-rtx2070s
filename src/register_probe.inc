/* Included only by the explicitly selected TU104_REGISTER_PROBE build. */
#include "pci_bar_resource.h"

#define REGISTER_PROBE_EXPECTED_BOOT0 UINT32_C(0x164000a1)
#define REGISTER_PROBE_EXPECTED_BAR0 UINT32_C(0xa3000000)
#define REGISTER_PROBE_EXPECTED_BAR0_BYTES UINT64_C(0x01000000)
#define REGISTER_PROBE_PASSES 2u
#define REGISTER_PROBE_COUNT 12u

struct register_probe_definition {
    const char *name;
    UINT32 offset;
};

static const struct register_probe_definition register_probe_definitions[] = {
    { "FECS_FEATURE_PLM",     UINT32_C(0x409650) },
    { "FECS_FEATURE_READOUT", UINT32_C(0x409660) },
    { "FECS_SS0",             UINT32_C(0x409664) },
    { "FECS_SPEED_READOUT",   UINT32_C(0x409668) },
    { "FECS_SS1",             UINT32_C(0x40966c) },
    { "WPR2_LO",              UINT32_C(0x1fa824) },
    { "WPR2_HI",              UINT32_C(0x1fa828) },
    { "SEC2_CPUCTL",          UINT32_C(0x840100) },
    { "GSP_CPUCTL",           UINT32_C(0x110100) },
    { "GFW_BOOT_OK",          UINT32_C(0x118234) },
    { "VGA_WORKSPACE_BASE",   UINT32_C(0x625f04) },
    { "VBIOS_SCRATCH14",      UINT32_C(0x001438) },
};

typedef char register_probe_count_must_match[
    sizeof(register_probe_definitions) /
        sizeof(register_probe_definitions[0]) == REGISTER_PROBE_COUNT ? 1 : -1
];

static EFI_STATUS verify_register_probe_bar(EFI_PCI_IO_PROTOCOL *target)
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
    note(L"Register probe config guard: id=%08x sub=%08x class=%08x "
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
        resource.minimum != REGISTER_PROBE_EXPECTED_BAR0 || !maximum_ok ||
        resource.translation != 0 ||
        resource.length != REGISTER_PROBE_EXPECTED_BAR0_BYTES)
        status = EFI_UNSUPPORTED;
    else
        status = EFI_SUCCESS;
    FreePool(descriptor);
    return status;
}

static EFI_STATUS run_register_probe(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 values[REGISTER_PROBE_PASSES][REGISTER_PROBE_COUNT];
    UINT32 boot0_first = 0;
    UINT32 boot0_second = 0;
    UINTN pass;
    UINTN index;
    UINTN stable = 0;
    EFI_STATUS status;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    status = verify_register_probe_bar(target);
    note(L"Register probe BAR/config guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;

    if (!target->RomImage ||
        target->RomSize != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE ||
        option_rom_crc32((const uint8_t *)target->RomImage,
                         (size_t)target->RomSize) != UINT32_C(0xbf35aca7)) {
        note(L"Firmware option-ROM guard failed; register probe refused.\r\n");
        return EFI_COMPROMISED_DATA;
    }

    note(L"BEGIN bounded register probe: BOOT0 twice, 12 registers twice.\r\n");
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
         boot0_first, boot0_second, REGISTER_PROBE_EXPECTED_BOOT0, status);
    if (EFI_ERROR(status) || boot0_first != REGISTER_PROBE_EXPECTED_BOOT0 ||
        boot0_second != REGISTER_PROBE_EXPECTED_BOOT0)
        return EFI_COMPROMISED_DATA;

    SetMem(values, sizeof(values), 0);
    for (pass = 0; pass < REGISTER_PROBE_PASSES; ++pass) {
        note(L"Register pass %d begin.\r\n", pass + 1);
        for (index = 0; index < REGISTER_PROBE_COUNT; ++index) {
            status = uefi_call_wrapper(target->Mem.Read, 6, target,
                                       EfiPciIoWidthUint32, 0,
                                       register_probe_definitions[index].offset,
                                       1, &values[pass][index]);
            note(L"REG pass=%d name=%a offset=%08x value=%08x status=%r\r\n",
                 pass + 1, register_probe_definitions[index].name,
                 register_probe_definitions[index].offset,
                 values[pass][index], status);
            if (EFI_ERROR(status) || EFI_ERROR(log_status))
                return EFI_ERROR(status) ? status : log_status;
        }
    }

    for (index = 0; index < REGISTER_PROBE_COUNT; ++index) {
        BOOLEAN equal = values[0][index] == values[1][index];

        if (equal)
            ++stable;
        note(L"REG compare name=%a offset=%08x first=%08x second=%08x "
             L"stable=%d\r\n",
             register_probe_definitions[index].name,
             register_probe_definitions[index].offset,
             values[0][index], values[1][index], equal);
    }
    note(L"Register stability summary: stable=%d total=%d. "
         L"Inventory values are observations, not unlock authorization.\r\n",
         stable, REGISTER_PROBE_COUNT);
    note(L"END bounded register probe status=Success\r\n");
    return EFI_SUCCESS;
}
