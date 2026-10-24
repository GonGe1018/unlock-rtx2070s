/* Included only by the explicitly selected TU104_HIGH_DMA_MAP_PROBE build.
 *
 * This stage claims one temporary 32 MiB conventional-memory range above
 * 4 GiB and asks the exact target's parent EFI PCI root bridge to create a
 * BusMasterRead64 mapping.  The returned address is never programmed into
 * the GPU, so no device transaction can occur.  The mapping is immediately
 * removed and the pages are released.  There are no PCI/MMIO writes, GPU BAR
 * accesses, resets, firmware launches, or flash operations in this image.
 */
#include "dma_map_policy.h"

#define HIGH_DMA_EXPECTED_ROM_CRC UINT32_C(0xbf35aca7)
#define HIGH_DMA_PAGES ((UINTN)(TU104_DMA_PROBE_BYTES >> 12))
#define HIGH_DMA_EXPECTED_CURRENT_ATTRIBUTES UINT64_C(0x00000718)
#define HIGH_DMA_EXPECTED_SUPPORTED_ATTRIBUTES UINT64_C(0x0001277b)
#define HIGH_DMA_MEMORY_RO UINT64_C(0x00020000)

static EFI_GUID high_dma_root_guid = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;
static EFI_GUID high_dma_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;

static EFI_STATUS verify_high_dma_target(EFI_PCI_IO_PROTOCOL *target,
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
    note(L"High DMA config guard: id=%08x sub=%08x class=%08x "
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
    if (!target->RomImage ||
        target->RomSize != (UINT64)TU104_OBSERVED_EFI_ROM_SIZE ||
        option_rom_crc32((const uint8_t *)target->RomImage,
                         (size_t)target->RomSize) != HIGH_DMA_EXPECTED_ROM_CRC)
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
}

static void fill_high_dma_pattern(VOID *buffer)
{
    UINT32 *words = (UINT32 *)buffer;
    UINTN count = (UINTN)(TU104_DMA_PROBE_BYTES / sizeof(*words));
    UINTN index;

    for (index = 0; index < count; ++index)
        words[index] = tu104_dma_pattern_word((size_t)index);
}

static EFI_STATUS resolve_high_dma_root(EFI_PCI_IO_PROTOCOL *target,
                                        const UINT32 expected[CONFIG_DWORDS],
                                        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL **out,
                                        UINT64 *supported,
                                        UINT64 *attributes)
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

    if (!target || !expected || !out || !supported || !attributes)
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
                               &high_dma_path_guid, (VOID **)&path);
    if (EFI_ERROR(status) || !path)
        return EFI_NOT_FOUND;
    remaining = path;
    status = uefi_call_wrapper(BS->LocateDevicePath, 3,
                               &high_dma_root_guid, &remaining,
                               &root_handle);
    if (EFI_ERROR(status) || !root_handle || !remaining || remaining == path)
        return EFI_NOT_FOUND;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, root_handle,
                               &high_dma_root_guid, (VOID **)&root);
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
    status = uefi_call_wrapper(root->GetAttributes, 3, root,
                               supported, attributes);
    note(L"Parent root resolved from exact GPU path: segment=%d "
         L"root_current=%lx root_supported=%lx status=%r\r\n",
         root->SegmentNumber, *attributes, *supported, status);
    if (EFI_ERROR(status))
        return status;
    *out = root;
    return EFI_SUCCESS;
}

static EFI_STATUS allocate_high_dma_pages(VOID **buffer,
                                          EFI_PHYSICAL_ADDRESS *physical,
                                          BOOLEAN *exact)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN required = 0;
    UINTN capacity = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    UINTN attempt;
    UINT64 best = 0;
    EFI_STATUS status;

    if (!buffer || !physical || !exact)
        return EFI_INVALID_PARAMETER;
    *buffer = NULL;
    *physical = 0;
    *exact = FALSE;
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &required, NULL,
                               &map_key, &descriptor_size,
                               &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
    if (descriptor_size != 48 || descriptor_version != 1 ||
        required > 1024u * 1024u)
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
    if (!map || required > capacity || required % descriptor_size != 0 ||
        descriptor_size != 48 || descriptor_version != 1) {
        if (map)
            FreePool(map);
        return EFI_COMPROMISED_DATA;
    }

    for (attempt = 0; attempt < required; attempt += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + attempt);
        UINT64 start = descriptor->PhysicalStart;
        UINT64 end;
        UINT64 candidate;

        if (descriptor->Type != EfiConventionalMemory ||
            descriptor->NumberOfPages == 0 ||
            !(descriptor->Attribute & EFI_MEMORY_WB) ||
            (descriptor->Attribute &
             (EFI_MEMORY_WP | EFI_MEMORY_RP | HIGH_DMA_MEMORY_RO)) ||
            (start & (TU104_DMA_PAGE_BYTES - 1)) ||
            descriptor->NumberOfPages > ((~(UINT64)0 - start) >> 12))
            continue;
        end = start + (descriptor->NumberOfPages << 12);
        candidate = tu104_high_dma_candidate(start, end);
        if (candidate > best)
            best = candidate;
    }
    FreePool(map);
    if (!best)
        return EFI_OUT_OF_RESOURCES;

    *physical = best;
    note(L"High AllocateAddress candidate=%lx bytes=%lx pages=%ld\r\n",
         best, TU104_DMA_PROBE_BYTES, HIGH_DMA_PAGES);
    if (EFI_ERROR(log_status))
        return log_status;
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress,
                               EfiBootServicesData, HIGH_DMA_PAGES, physical);
    note(L"High AllocatePages status=%r requested=%lx returned=%lx\r\n",
         status, best, *physical);
    if (EFI_ERROR(status)) {
        *physical = 0;
        return status;
    }
    *buffer = (VOID *)(UINTN)(*physical);
    *exact = *physical == best && tu104_dma64_geometry_suitable(
        (uint64_t)*physical, TU104_DMA_PROBE_BYTES);
    return EFI_SUCCESS;
}

static EFI_STATUS run_high_dma_map_probe(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 before_config[CONFIG_DWORDS];
    UINT32 after_config[CONFIG_DWORDS];
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *root = NULL;
    UINT64 pci_supported_before = 0;
    UINT64 pci_attributes_before = 0;
    UINT64 pci_supported_after = 0;
    UINT64 pci_attributes_after = 0;
    UINT64 root_supported_before = 0;
    UINT64 root_attributes_before = 0;
    UINT64 root_supported_after = 0;
    UINT64 root_attributes_after = 0;
    VOID *iommu = NULL;
    VOID *buffer = NULL;
    VOID *mapping = NULL;
    EFI_PHYSICAL_ADDRESS host_address = 0;
    EFI_PHYSICAL_ADDRESS device_address = 0;
    UINTN mapped_bytes = (UINTN)TU104_DMA_PROBE_BYTES;
    UINT32 crc_before = 0;
    UINT32 crc_after = 0;
    BOOLEAN allocated = FALSE;
    BOOLEAN allocation_exact = FALSE;
    BOOLEAN mapped = FALSE;
    BOOLEAN geometry_ok = FALSE;
    BOOLEAN config_unchanged = FALSE;
    BOOLEAN attributes_unchanged = FALSE;
    EFI_STATUS status;
    EFI_STATUS service_status;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    SetMem(before_config, sizeof(before_config), 0);
    SetMem(after_config, sizeof(after_config), 0);
    status = verify_high_dma_target(target, before_config);
    note(L"High DMA exact-target guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;

    status = uefi_call_wrapper(target->Attributes, 4, target,
                               EfiPciIoAttributeOperationSupported,
                               0, &pci_supported_before);
    if (!EFI_ERROR(status))
        status = uefi_call_wrapper(target->Attributes, 4, target,
                                   EfiPciIoAttributeOperationGet,
                                   0, &pci_attributes_before);
    note(L"High DMA PCI attributes status=%r current=%lx supported=%lx; "
         L"no attribute change requested.\r\n",
         status, pci_attributes_before, pci_supported_before);
    if (EFI_ERROR(status) ||
        pci_supported_before != HIGH_DMA_EXPECTED_SUPPORTED_ATTRIBUTES ||
        pci_attributes_before != HIGH_DMA_EXPECTED_CURRENT_ATTRIBUTES ||
        (pci_supported_before & EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE))
        return EFI_UNSUPPORTED;

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &iommu_protocol_guid, NULL, &iommu);
    note(L"Fresh EFI IOMMU guard status=%r present=%d pointer=%lx\r\n",
         status, iommu != NULL, (UINT64)(UINTN)iommu);
    if (status != EFI_NOT_FOUND || iommu != NULL)
        return EFI_UNSUPPORTED;

    status = resolve_high_dma_root(target, before_config, &root,
                                   &root_supported_before,
                                   &root_attributes_before);
    if (EFI_ERROR(status) || !root)
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;

    note(L"BEGIN temporary HIGH64 streaming-input map probe. The address "
         L"will not be programmed into the GPU; no device DMA can occur.\r\n");
    status = allocate_high_dma_pages(&buffer, &host_address,
                                     &allocation_exact);
    if (!EFI_ERROR(status))
        allocated = TRUE;
    if (EFI_ERROR(status))
        goto cleanup;
    if (!allocation_exact) {
        status = EFI_COMPROMISED_DATA;
        goto cleanup;
    }

    fill_high_dma_pattern(buffer);
    crc_before = option_rom_crc32((const uint8_t *)buffer,
                                  (size_t)TU104_DMA_PROBE_BYTES);
    note(L"Prepared high host-only canary CRC32=%08x at=%lx; "
         L"Read64 mapping begins next.\r\n", crc_before, host_address);
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto cleanup;
    }

    service_status = uefi_call_wrapper(root->Map, 6, root,
                                       EfiPciOperationBusMasterRead64,
                                       buffer, &mapped_bytes,
                                       &device_address, &mapping);
    if (!EFI_ERROR(service_status))
        mapped = TRUE;
    geometry_ok = !EFI_ERROR(service_status) &&
        tu104_dma64_geometry_suitable((uint64_t)host_address,
                                      TU104_DMA_PROBE_BYTES) &&
        tu104_dma64_geometry_suitable((uint64_t)device_address,
                                      (uint64_t)mapped_bytes) &&
        device_address == host_address;
    note(L"Root Read64 Map status=%r host=%lx device=%lx bytes=%lx "
         L"token=%lx identity=%d host_above4G=%d device_above4G=%d "
         L"geometry_suitable=%d\r\n",
         service_status, host_address, device_address,
         (UINT64)mapped_bytes, (UINT64)(UINTN)mapping,
         device_address == host_address,
         host_address >= TU104_DMA32_LIMIT,
         device_address >= TU104_DMA32_LIMIT,
         geometry_ok);
    note(L"The returned device address was never programmed into the GPU; "
         L"actual GPU DMA reachability was NOT tested.\r\n");
    status = EFI_ERROR(service_status) ? service_status :
        (geometry_ok ? EFI_SUCCESS : EFI_UNSUPPORTED);

cleanup:
    if (mapped) {
        service_status = uefi_call_wrapper(root->Unmap, 2, root, mapping);
        note(L"Root Read64 Unmap status=%r\r\n", service_status);
        if (!EFI_ERROR(service_status))
            mapped = FALSE;
        else
            status = service_status;
    }
    if (allocated) {
        crc_after = option_rom_crc32((const uint8_t *)buffer,
                                     (size_t)TU104_DMA_PROBE_BYTES);
        note(L"High host canary after map lifecycle CRC32=%08x unchanged=%d\r\n",
             crc_after, crc_before == crc_after);
        if (crc_before != crc_after && !EFI_ERROR(status))
            status = EFI_CRC_ERROR;
    }
    if (allocated && !mapped) {
        service_status = uefi_call_wrapper(BS->FreePages, 2,
                                           host_address, HIGH_DMA_PAGES);
        note(L"High FreePages status=%r\r\n", service_status);
        if (!EFI_ERROR(service_status))
            allocated = FALSE, buffer = NULL;
        else
            status = service_status;
    } else if (allocated) {
        note(L"Unmap failed: high backing retained. No GPU address was "
             L"programmed; fully power-cycle before any retry.\r\n");
    }

    if (!EFI_ERROR(read_config_header(target, after_config)))
        config_unchanged = CompareMem(before_config, after_config,
                                      sizeof(before_config)) == 0;
    service_status = uefi_call_wrapper(target->Attributes, 4, target,
                                       EfiPciIoAttributeOperationSupported,
                                       0, &pci_supported_after);
    if (!EFI_ERROR(service_status))
        service_status = uefi_call_wrapper(
            target->Attributes, 4, target, EfiPciIoAttributeOperationGet,
            0, &pci_attributes_after);
    if (!EFI_ERROR(service_status))
        service_status = uefi_call_wrapper(root->GetAttributes, 3, root,
                                           &root_supported_after,
                                           &root_attributes_after);
    attributes_unchanged = !EFI_ERROR(service_status) &&
        pci_supported_after == pci_supported_before &&
        pci_attributes_after == pci_attributes_before &&
        root_supported_after == root_supported_before &&
        root_attributes_after == root_attributes_before;
    note(L"Post-high-map config_unchanged=%d attributes_unchanged=%d "
         L"pci_current=%lx pci_supported=%lx root_current=%lx "
         L"root_supported=%lx query=%r\r\n",
         config_unchanged, attributes_unchanged,
         pci_attributes_after, pci_supported_after,
         root_attributes_after, root_supported_after, service_status);
    if ((!config_unchanged || !attributes_unchanged) && !EFI_ERROR(status))
        status = EFI_DEVICE_ERROR;
    if (EFI_ERROR(log_status))
        status = log_status;
    note(L"END HIGH64 streaming map service probe status=%r; mapped=%d "
         L"buffer_retained=%d.\r\n", status, mapped, allocated);
    return status;
}
