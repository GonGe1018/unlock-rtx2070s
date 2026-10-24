/* Included only by the explicitly selected TU104_PLATFORM_DMA_PROBE build.
 *
 * This stage is a read-only platform observation.  It reads the exact MSI
 * Setup variable, CPU identity, host-bridge configuration, the Coffee Lake
 * MCHBAR VT-d BAR register, and (only when that BAR is enabled at the pinned
 * address) immutable VT-d status/capability registers.  It also copies the
 * first 256 CPU-memory bytes of the live root-bridge Map implementation after
 * proving that the pointer lies in EfiBootServicesCode.  It does not invoke
 * Map/Unmap, allocate DMA pages, access a GPU BAR, or write hardware state.
 */
#include "platform_dma_policy.h"

#define PLATFORM_EXPECTED_ROM_CRC UINT32_C(0xbf35aca7)
#define PLATFORM_MAP_CODE_BYTES ((UINTN)256)
#define PLATFORM_MEMORY_RO UINT64_C(0x00020000)

static EFI_GUID platform_root_guid = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;
static EFI_GUID platform_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID platform_acpi20_guid = ACPI_20_TABLE_GUID;
static EFI_GUID platform_setup_guid = {
    0xec87d643, 0xeba4, 0x4bb5,
    {0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9}
};
static UINT8 platform_setup_data[TU104_SETUP_BYTES];

static BOOLEAN platform_checksum_ok(const UINT8 *bytes, UINTN length)
{
    UINT8 sum = 0;
    UINTN index;

    if (!bytes || length == 0)
        return FALSE;
    for (index = 0; index < length; ++index)
        sum = (UINT8)(sum + bytes[index]);
    return sum == 0;
}

static UINT32 platform_u32(const UINT8 *bytes)
{
    UINT32 value = 0;

    value |= (UINT32)bytes[0];
    value |= (UINT32)bytes[1] << 8;
    value |= (UINT32)bytes[2] << 16;
    value |= (UINT32)bytes[3] << 24;
    return value;
}

static UINT64 platform_u64(const UINT8 *bytes)
{
    return (UINT64)platform_u32(bytes) |
        ((UINT64)platform_u32(bytes + 4) << 32);
}

static EFI_STATUS platform_dmar_presence(BOOLEAN *present)
{
    const UINT8 *rsdp = NULL;
    const UINT8 *xsdt;
    UINT32 length;
    UINTN config_matches = 0;
    UINTN dmar_matches = 0;
    UINTN index;

    if (!present)
        return EFI_INVALID_PARAMETER;
    *present = FALSE;
    for (index = 0; index < ST->NumberOfTableEntries; ++index) {
        if (CompareMem(&ST->ConfigurationTable[index].VendorGuid,
                       &platform_acpi20_guid,
                       sizeof(platform_acpi20_guid)) == 0) {
            rsdp = (const UINT8 *)ST->ConfigurationTable[index].VendorTable;
            ++config_matches;
        }
    }
    if (config_matches != 1 || !rsdp)
        return config_matches ? EFI_COMPROMISED_DATA : EFI_NOT_FOUND;
    if (CompareMem((VOID *)rsdp, (VOID *)"RSD PTR ", 8) != 0 ||
        !platform_checksum_ok(rsdp, 20) || rsdp[15] < 2)
        return EFI_COMPROMISED_DATA;
    length = platform_u32(rsdp + 20);
    if (length < 36 || length > 4096 ||
        !platform_checksum_ok(rsdp, length))
        return EFI_COMPROMISED_DATA;
    xsdt = (const UINT8 *)(UINTN)platform_u64(rsdp + 24);
    if (!xsdt || CompareMem((VOID *)xsdt, (VOID *)"XSDT", 4) != 0)
        return EFI_COMPROMISED_DATA;
    length = platform_u32(xsdt + 4);
    if (length < 36 || length > 65536 || (length - 36) % 8 != 0 ||
        !platform_checksum_ok(xsdt, length))
        return EFI_COMPROMISED_DATA;
    for (index = 36; index < length; index += 8) {
        const UINT8 *table =
            (const UINT8 *)(UINTN)platform_u64(xsdt + index);

        if (!table)
            return EFI_COMPROMISED_DATA;
        if (CompareMem((VOID *)table, (VOID *)"DMAR", 4) == 0) {
            UINT32 table_length = platform_u32(table + 4);

            if (table_length < 48 || table_length > 65536 ||
                !platform_checksum_ok(table, table_length))
                return EFI_COMPROMISED_DATA;
            note(L"ACPI DMAR found: address=%lx bytes=%d revision=%d "
                 L"host_width=%d flags=%02x (READ ONLY)\r\n",
                 (UINT64)(UINTN)table, table_length, table[8],
                 table[36], table[37]);
            ++dmar_matches;
        }
    }
    if (dmar_matches > 1)
        return EFI_COMPROMISED_DATA;
    *present = dmar_matches == 1;
    return *present ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS platform_read_setup(void)
{
    UINTN size = sizeof(platform_setup_data);
    UINT32 attributes = 0;
    EFI_STATUS status;

    SetMem(platform_setup_data, sizeof(platform_setup_data), 0);
    status = uefi_call_wrapper(RT->GetVariable, 5, L"Setup",
                               &platform_setup_guid, &attributes,
                               &size, platform_setup_data);
    note(L"MSI Setup GET-only status=%r size=%ld attrs=%08x CRC32=%08x\r\n",
         status, size, attributes,
         EFI_ERROR(status) ? 0 :
         option_rom_crc32((const uint8_t *)platform_setup_data,
                          sizeof(platform_setup_data)));
    if (EFI_ERROR(status))
        return status;
    if (size != sizeof(platform_setup_data) || attributes != 7)
        return EFI_UNSUPPORTED;
    note(L"Setup profile: VT-d[A1D]=%d Above4G-MMIO[A1E]=%d "
         L"MemoryRemap[AEF]=%d Above4G-mining[1432]=%d CSM[145E]=%d; "
         L"no variable write.\r\n",
         platform_setup_data[TU104_SETUP_VTD_OFFSET],
         platform_setup_data[TU104_SETUP_ABOVE4G_MMIO_OFFSET],
         platform_setup_data[TU104_SETUP_MEMORY_REMAP_OFFSET],
         platform_setup_data[TU104_SETUP_ABOVE4G_MINING_OFFSET],
         platform_setup_data[TU104_SETUP_CSM_OFFSET]);
    return tu104_setup_dma_profile_matches(
        (const uint8_t *)platform_setup_data,
        sizeof(platform_setup_data)) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static void platform_cpuid(UINT32 leaf, UINT32 subleaf,
                           UINT32 *eax, UINT32 *ebx,
                           UINT32 *ecx, UINT32 *edx)
{
    __asm__ __volatile__("cpuid"
                         : "=a"(*eax), "=b"(*ebx),
                           "=c"(*ecx), "=d"(*edx)
                         : "a"(leaf), "c"(subleaf));
}

static EFI_STATUS platform_read_cpu(void)
{
    UINT32 max_leaf;
    UINT32 max_extended;
    UINT32 eax;
    UINT32 ebx;
    UINT32 ecx;
    UINT32 edx;
    UINT32 vendor_b;
    UINT32 vendor_c;
    UINT32 vendor_d;
    UINT32 signature;
    UINT32 hypervisor;
    UINT32 physical_bits;

    platform_cpuid(0, 0, &max_leaf, &vendor_b, &vendor_c, &vendor_d);
    if (max_leaf < 1)
        return EFI_UNSUPPORTED;
    platform_cpuid(1, 0, &signature, &ebx, &ecx, &edx);
    hypervisor = ecx >> 31;
    platform_cpuid(UINT32_C(0x80000000), 0,
                   &max_extended, &ebx, &ecx, &edx);
    if (max_extended < UINT32_C(0x80000008))
        return EFI_UNSUPPORTED;
    platform_cpuid(UINT32_C(0x80000008), 0,
                   &eax, &ebx, &ecx, &edx);
    physical_bits = eax & 0xffu;
    note(L"CPU DMA profile: vendor=%08x/%08x/%08x signature=%08x "
         L"physbits=%d hypervisor=%d\r\n",
         vendor_b, vendor_d, vendor_c, signature,
         physical_bits, hypervisor);
    if (vendor_b != UINT32_C(0x756e6547) ||
        vendor_d != UINT32_C(0x49656e69) ||
        vendor_c != UINT32_C(0x6c65746e) ||
        signature != UINT32_C(0x000906ed) ||
        physical_bits != 39 || hypervisor)
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

static EFI_STATUS platform_verify_target(EFI_PCI_IO_PROTOCOL *target,
                                         UINT32 config[CONFIG_DWORDS])
{
    static const UINT32 expected_bars[7] = {
        UINT32_C(0xa3000000), UINT32_C(0x9000000c),
        UINT32_C(0x00000000), UINT32_C(0xa000000c),
        UINT32_C(0x00000000), UINT32_C(0x00003001),
        UINT32_C(0xa4000000)
    };
    EFI_STATUS status;
    UINTN index;

    status = read_config_header(target, config);
    if (EFI_ERROR(status))
        return status;
    if (!tu104_identity_matches(config[0], config[11], config[2]) ||
        config[2] != UINT32_C(0x030000a1) ||
        (config[1] & 0xffffu) != 0x0007u)
        return EFI_UNSUPPORTED;
    for (index = 0; index < 6; ++index) {
        if (config[4 + index] != expected_bars[index])
            return EFI_UNSUPPORTED;
    }
    if (config[12] != expected_bars[6] || !target->RomImage ||
        target->RomSize != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE ||
        option_rom_crc32((const uint8_t *)target->RomImage,
                         (size_t)target->RomSize) !=
            PLATFORM_EXPECTED_ROM_CRC)
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
}

static EFI_STATUS
platform_resolve_root(EFI_PCI_IO_PROTOCOL *target,
                      const UINT32 expected[CONFIG_DWORDS],
                      EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL **out)
{
    EFI_HANDLE *handles = NULL;
    EFI_HANDLE device_handle = NULL;
    EFI_HANDLE root_handle = NULL;
    EFI_DEVICE_PATH *path = NULL;
    EFI_DEVICE_PATH *remaining = NULL;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    UINTN count = 0;
    UINTN matches = 0;
    UINTN index;
    EFI_STATUS status;

    if (!target || !expected || !out)
        return EFI_INVALID_PARAMETER;
    *out = NULL;
    status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol,
                               &pci_io_guid, NULL, &count, &handles);
    if (EFI_ERROR(status))
        return status;
    for (index = 0; index < count; ++index) {
        EFI_PCI_IO_PROTOCOL *candidate = NULL;
        UINTN segment = 0;
        UINTN bus = 0;
        UINTN device = 0;
        UINTN function = 0;

        status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[index],
                                   &pci_io_guid, (VOID **)&candidate);
        if (EFI_ERROR(status) || candidate != target)
            continue;
        status = uefi_call_wrapper(candidate->GetLocation, 5, candidate,
                                   &segment, &bus, &device, &function);
        if (!EFI_ERROR(status) &&
            tu104_location_matches(segment, bus, device, function)) {
            device_handle = handles[index];
            ++matches;
        }
    }
    FreePool(handles);
    if (matches != 1 || !device_handle)
        return EFI_SECURITY_VIOLATION;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, device_handle,
                               &platform_path_guid, (VOID **)&path);
    if (EFI_ERROR(status) || !path)
        return EFI_NOT_FOUND;
    remaining = path;
    status = uefi_call_wrapper(BS->LocateDevicePath, 3,
                               &platform_root_guid, &remaining,
                               &root_handle);
    if (EFI_ERROR(status) || !root_handle || !remaining || remaining == path)
        return EFI_NOT_FOUND;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, root_handle,
                               &platform_root_guid, (VOID **)&root);
    if (EFI_ERROR(status) || !root || root->SegmentNumber != 0)
        return EFI_UNSUPPORTED;
    for (index = 0; index < CONFIG_DWORDS; ++index) {
        UINT32 value = 0;
        UINT64 address = (UINT64)0x01000000u + index * sizeof(UINT32);

        status = uefi_call_wrapper(root->Pci.Read, 5, root,
                                   EfiPciIoWidthUint32, address, 1, &value);
        if (EFI_ERROR(status) || value != expected[index])
            return EFI_SECURITY_VIOLATION;
    }
    *out = root;
    return EFI_SUCCESS;
}

static EFI_STATUS platform_pci_read32(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root,
    UINTN bus, UINTN device, UINTN function,
    UINTN offset, UINT32 *value)
{
    UINT64 address;

    if (!root || !value || bus > 255 || device > 31 || function > 7 ||
        offset > 4092 || (offset & 3))
        return EFI_INVALID_PARAMETER;
    address = ((UINT64)bus << 24) | ((UINT64)device << 16) |
        ((UINT64)function << 8) | offset;
    return uefi_call_wrapper(root->Pci.Read, 5, root,
                             EfiPciIoWidthUint32, address, 1, value);
}

static EFI_STATUS platform_mem_read32_stable(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root,
    UINT64 address, UINT32 *value)
{
    UINT32 first = 0;
    UINT32 second = 0;
    EFI_STATUS status;

    if (!root || !value || (address & 3))
        return EFI_INVALID_PARAMETER;
    status = uefi_call_wrapper(root->Mem.Read, 5, root,
                               EfiPciIoWidthUint32, address, 1, &first);
    if (!EFI_ERROR(status))
        status = uefi_call_wrapper(root->Mem.Read, 5, root,
                                   EfiPciIoWidthUint32, address, 1, &second);
    if (EFI_ERROR(status))
        return status;
    if (first != second)
        return EFI_COMPROMISED_DATA;
    *value = first;
    return EFI_SUCCESS;
}

static EFI_STATUS platform_map_code_region(const UINT8 *code,
                                           UINT64 *attributes)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN required = 0;
    UINTN capacity = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    UINTN attempt;
    EFI_STATUS status;
    BOOLEAN matched = FALSE;

    if (!code || !attributes)
        return EFI_INVALID_PARAMETER;
    *attributes = 0;
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
    for (attempt = 0; attempt < required; attempt += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR descriptor;
        UINT64 start;
        UINT64 end;
        UINT64 pointer = (UINT64)(UINTN)code;

        SetMem(&descriptor, sizeof(descriptor), 0);
        CopyMem(&descriptor, (UINT8 *)map + attempt, sizeof(descriptor));
        if (descriptor.NumberOfPages == 0 ||
            descriptor.NumberOfPages >
                ((~(UINT64)0 - descriptor.PhysicalStart) >> 12))
            continue;
        start = descriptor.PhysicalStart;
        end = start + (descriptor.NumberOfPages << 12);
        if (descriptor.Type == EfiBootServicesCode && pointer >= start &&
            pointer <= end - PLATFORM_MAP_CODE_BYTES &&
            !(descriptor.Attribute & EFI_MEMORY_RP)) {
            if (matched) {
                FreePool(map);
                return EFI_COMPROMISED_DATA;
            }
            matched = TRUE;
            *attributes = descriptor.Attribute;
        }
    }
    FreePool(map);
    return matched ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static EFI_STATUS platform_log_map_code(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root)
{
    const UINT8 *code;
    static const CHAR16 digits[] = L"0123456789ABCDEF";
    UINT64 attributes = 0;
    UINTN row;
    EFI_STATUS status;

    if (!root || !root->Map)
        return EFI_INVALID_PARAMETER;
    code = (const UINT8 *)(UINTN)root->Map;
    status = platform_map_code_region(code, &attributes);
    note(L"Root Map CPU-code guard status=%r pointer=%lx bytes=%d "
         L"memory_attrs=%lx\r\n",
         status, (UINT64)(UINTN)code,
         PLATFORM_MAP_CODE_BYTES, attributes);
    if (EFI_ERROR(status))
        return status;
    note(L"Root Map first-256 CRC32=%08x; function is NOT invoked or patched.\r\n",
         option_rom_crc32((const uint8_t *)code,
                          PLATFORM_MAP_CODE_BYTES));
    for (row = 0; row < PLATFORM_MAP_CODE_BYTES; row += 32) {
        CHAR16 hex[65];
        UINTN index;

        for (index = 0; index < 32; ++index) {
            UINT8 byte = code[row + index];

            hex[index * 2] = digits[byte >> 4];
            hex[index * 2 + 1] = digits[byte & 0x0f];
        }
        hex[64] = 0;
        note(L"Root Map code +%03x: %s\r\n", row, hex);
    }
    return EFI_ERROR(log_status) ? log_status : EFI_SUCCESS;
}

static EFI_STATUS platform_observe_vtd(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root)
{
    UINT32 host_id = 0;
    UINT32 host_command = 0;
    UINT32 host_class = 0;
    UINT32 host_subsystem = 0;
    UINT32 mchbar_low = 0;
    UINT32 mchbar_high = 0;
    UINT32 igd_id = 0;
    UINT64 mchbar;
    UINT32 vtd2_raw = 0;
    UINT64 vtd2_base;
    UINT32 version = 0;
    UINT32 cap_low = 0;
    UINT32 cap_high = 0;
    UINT32 ecap_low = 0;
    UINT32 ecap_high = 0;
    UINT32 global_status = 0;
    UINT32 protected_memory_enable = 0;
    UINT64 capability;
    UINT64 extended_capability;
    BOOLEAN protected_memory_read = FALSE;
    EFI_STATUS status;

    status = platform_pci_read32(root, 0, 0, 0, 0, &host_id);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 0, 0, 4, &host_command);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 0, 0, 8, &host_class);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 0, 0, 0x2c,
                                     &host_subsystem);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 0, 0, 0x48, &mchbar_low);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 0, 0, 0x4c, &mchbar_high);
    if (!EFI_ERROR(status))
        status = platform_pci_read32(root, 0, 2, 0, 0, &igd_id);
    note(L"Host bridge READ ONLY: id=%08x command=%04x classrev=%08x "
         L"sub=%08x MCHBAR=%08x:%08x IGD-id=%08x status=%r\r\n",
         host_id, host_command & 0xffffu, host_class,
         host_subsystem, mchbar_high, mchbar_low, igd_id, status);
    if (EFI_ERROR(status))
        return status;
    if (host_id != UINT32_C(0x3e308086) ||
        host_class != UINT32_C(0x0600000d) ||
        host_subsystem != UINT32_C(0x7b171462) ||
        igd_id != UINT32_C(0xffffffff) ||
        !tu104_mchbar_profile_matches(mchbar_low, mchbar_high))
        return EFI_UNSUPPORTED;
    mchbar = tu104_mchbar_address(mchbar_low, mchbar_high);
    status = platform_mem_read32_stable(
        root, mchbar + TU104_MCHBAR_VTD2_OFFSET, &vtd2_raw);
    vtd2_base = tu104_vtd2_address(vtd2_raw);
    note(L"MCHBAR VT-d2 READ ONLY: register=%lx raw=%08x enabled=%d "
         L"base=%lx stable_status=%r\r\n",
         mchbar + TU104_MCHBAR_VTD2_OFFSET, vtd2_raw,
         tu104_vtd2_enabled(vtd2_raw), vtd2_base, status);
    if (EFI_ERROR(status))
        return status;
    if (!tu104_vtd2_enabled(vtd2_raw)) {
        note(L"VT-d2 BAR is disabled; no VT-d engine MMIO was read. "
             L"This is an observation, not a GPU DMA reachability test.\r\n");
        return (vtd2_raw == 0 || tu104_vtd2_profile_matches(vtd2_raw)) ?
            EFI_SUCCESS : EFI_UNSUPPORTED;
    }
    if (!tu104_vtd2_profile_matches(vtd2_raw))
        return EFI_UNSUPPORTED;

    status = platform_mem_read32_stable(root, vtd2_base, &version);
    if (!EFI_ERROR(status))
        status = platform_mem_read32_stable(root, vtd2_base + 0x08,
                                             &cap_low);
    if (!EFI_ERROR(status))
        status = platform_mem_read32_stable(root, vtd2_base + 0x0c,
                                             &cap_high);
    if (!EFI_ERROR(status))
        status = platform_mem_read32_stable(root, vtd2_base + 0x10,
                                             &ecap_low);
    if (!EFI_ERROR(status))
        status = platform_mem_read32_stable(root, vtd2_base + 0x14,
                                             &ecap_high);
    if (!EFI_ERROR(status))
        status = platform_mem_read32_stable(root, vtd2_base + 0x1c,
                                             &global_status);
    capability = (UINT64)cap_low | ((UINT64)cap_high << 32);
    extended_capability = (UINT64)ecap_low | ((UINT64)ecap_high << 32);
    if (!EFI_ERROR(status) && tu104_vtd_has_protected_memory(capability)) {
        status = platform_mem_read32_stable(root, vtd2_base + 0x64,
                                             &protected_memory_enable);
        if (!EFI_ERROR(status))
            protected_memory_read = TRUE;
    }
    note(L"VT-d2 status READ ONLY: VER=%08x CAP=%lx ECAP=%lx "
         L"GSTS=%08x TES=%d PMEN=%08x PMR-read=%d "
         L"status=%r\r\n",
         version, capability, extended_capability,
         global_status, tu104_vtd_translation_enabled(global_status),
         protected_memory_enable, protected_memory_read, status);
    if (EFI_ERROR(status))
        return status;
    return tu104_vtd_observation_clear(
        version, capability, global_status, protected_memory_enable,
        protected_memory_read) ? EFI_SUCCESS : EFI_ACCESS_DENIED;
}

static EFI_STATUS run_platform_dma_probe(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 target_config[CONFIG_DWORDS];
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    VOID *iommu = NULL;
    BOOLEAN dmar_present = FALSE;
    EFI_STATUS status;
    EFI_STATUS observed;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    SetMem(target_config, sizeof(target_config), 0);
    status = platform_verify_target(target, target_config);
    note(L"Platform DMA exact-target guard status=%r\r\n", status);
    if (EFI_ERROR(status))
        return status;
    status = platform_read_setup();
    note(L"Platform Setup profile status=%r\r\n", status);
    if (EFI_ERROR(status))
        return status;
    status = platform_read_cpu();
    note(L"Platform CPU profile status=%r\r\n", status);
    if (EFI_ERROR(status))
        return status;

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &iommu_protocol_guid, NULL, &iommu);
    note(L"Fresh EFI IOMMU guard status=%r present=%d pointer=%lx\r\n",
         status, iommu != NULL, (UINT64)(UINTN)iommu);
    if (status != EFI_NOT_FOUND || iommu != NULL)
        return EFI_UNSUPPORTED;
    observed = platform_dmar_presence(&dmar_present);
    note(L"ACPI DMAR presence status=%r present=%d; no ACPI or IOMMU "
         L"write.\r\n", observed, dmar_present);
    if (observed != EFI_NOT_FOUND || dmar_present)
        return EFI_UNSUPPORTED;

    status = platform_resolve_root(target, target_config, &root);
    note(L"Exact GPU parent root resolution status=%r segment=%d\r\n",
         status, root ? root->SegmentNumber : 0xffffu);
    if (EFI_ERROR(status) || !root)
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    status = platform_log_map_code(root);
    note(L"Root Map CPU-code capture status=%r\r\n", status);
    if (EFI_ERROR(status))
        return status;
    status = platform_observe_vtd(root);
    note(L"Bounded chipset VT-d observation status=%r\r\n", status);
    if (EFI_ERROR(log_status))
        return log_status;
    return status;
}
