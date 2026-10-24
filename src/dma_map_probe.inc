/* Included only by the explicitly selected TU104_DMA_MAP_PROBE build.
 *
 * This stage allocates one temporary 32 MiB EFI common buffer below 4 GiB
 * and asks the target's EFI_PCI_IO_PROTOCOL to Map/Unmap it.  Probe 0.6.0
 * established that this firmware does not advertise dual-address-cycle
 * allocation.  This follow-up requests no allocation attributes and changes
 * none.  The GPU is never given a command or descriptor containing the
 * returned address, so no device DMA transaction can occur.  There are no
 * PCI/MMIO writes or GPU BAR reads.
 */
#include "dma_map_policy.h"

#define DMA_MAP_EXPECTED_ROM_CRC UINT32_C(0xbf35aca7)
#define DMA_MAP_PAGES ((UINTN)(TU104_DMA_PROBE_BYTES >> 12))
#define DMA_MAP_EXPECTED_CURRENT_ATTRIBUTES UINT64_C(0x00000718)
#define DMA_MAP_EXPECTED_SUPPORTED_ATTRIBUTES UINT64_C(0x0001277b)

static EFI_STATUS verify_dma_map_target(EFI_PCI_IO_PROTOCOL *target,
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
    note(L"DMA map config guard: id=%08x sub=%08x class=%08x "
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
                         (size_t)target->RomSize) != DMA_MAP_EXPECTED_ROM_CRC)
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
}

static void fill_dma_map_pattern(VOID *buffer)
{
    UINT32 *words = (UINT32 *)buffer;
    UINTN count = (UINTN)(TU104_DMA_PROBE_BYTES / sizeof(*words));
    UINTN index;

    for (index = 0; index < count; ++index)
        words[index] = tu104_dma_pattern_word((size_t)index);
}

static EFI_STATUS run_dma_map_probe(EFI_PCI_IO_PROTOCOL *target)
{
    UINT32 before_config[CONFIG_DWORDS];
    UINT32 after_config[CONFIG_DWORDS];
    UINT64 supported_before = 0;
    UINT64 attributes_before = 0;
    UINT64 supported_after = 0;
    UINT64 attributes_after = 0;
    UINT64 allocate_attributes;
    VOID *buffer = NULL;
    VOID *mapping = NULL;
    EFI_PHYSICAL_ADDRESS device_address = 0;
    UINTN mapped_bytes = (UINTN)TU104_DMA_PROBE_BYTES;
    UINT32 crc_before = 0;
    UINT32 crc_after = 0;
    BOOLEAN allocated = FALSE;
    BOOLEAN mapped = FALSE;
    BOOLEAN geometry_ok = FALSE;
    BOOLEAN config_unchanged;
    BOOLEAN attributes_unchanged;
    EFI_STATUS status;
    EFI_STATUS map_status;
    EFI_STATUS unmap_status = EFI_NOT_STARTED;
    EFI_STATUS free_status = EFI_NOT_STARTED;

    if (!target || EFI_ERROR(log_status))
        return EFI_INVALID_PARAMETER;
    SetMem(before_config, sizeof(before_config), 0);
    SetMem(after_config, sizeof(after_config), 0);
    status = verify_dma_map_target(target, before_config);
    note(L"DMA map exact-target guard status=%r\r\n", status);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;

    status = uefi_call_wrapper(target->Attributes, 4, target,
                               EfiPciIoAttributeOperationSupported,
                               0, &supported_before);
    if (!EFI_ERROR(status))
        status = uefi_call_wrapper(target->Attributes, 4, target,
                                   EfiPciIoAttributeOperationGet,
                                   0, &attributes_before);
    note(L"DMA map PCI attributes query status=%r current=%lx supported=%lx; "
         L"no attribute change requested.\r\n",
         status, attributes_before, supported_before);
    if (EFI_ERROR(status) || EFI_ERROR(log_status))
        return EFI_ERROR(status) ? status : log_status;
    if (supported_before != DMA_MAP_EXPECTED_SUPPORTED_ATTRIBUTES ||
        attributes_before != DMA_MAP_EXPECTED_CURRENT_ATTRIBUTES ||
        (supported_before & EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE)) {
        note(L"PCI attribute snapshot differs from probe 0.6.0; low32 map "
             L"skipped.\r\n");
        return EFI_UNSUPPORTED;
    }
    allocate_attributes = 0;

    note(L"BEGIN temporary LOW32 common-buffer service probe: bytes=%lx "
         L"pages=%ld allocation_attributes=0. No device DMA command exists "
         L"in this image.\r\n",
         TU104_DMA_PROBE_BYTES, DMA_MAP_PAGES);
    if (EFI_ERROR(log_status))
        return log_status;
    status = uefi_call_wrapper(target->AllocateBuffer, 6, target,
                               AllocateAnyPages, EfiBootServicesData,
                               DMA_MAP_PAGES, &buffer, allocate_attributes);
    note(L"AllocateBuffer status=%r host=%lx attributes=%lx\r\n",
         status, (UINT64)(UINTN)buffer, allocate_attributes);
    if (EFI_ERROR(status)) {
        buffer = NULL;
        goto cleanup;
    }
    allocated = buffer != NULL;
    if (!buffer || ((UINTN)buffer & 0xfffu)) {
        status = EFI_COMPROMISED_DATA;
        goto cleanup;
    }

    fill_dma_map_pattern(buffer);
    crc_before = option_rom_crc32((const uint8_t *)buffer,
                                  (size_t)TU104_DMA_PROBE_BYTES);
    note(L"Prepared host-only canary CRC32=%08x; mapping begins next.\r\n",
         crc_before);
    if (EFI_ERROR(log_status)) {
        status = log_status;
        goto cleanup;
    }

    map_status = uefi_call_wrapper(target->Map, 6, target,
                                   EfiPciIoOperationBusMasterCommonBuffer,
                                   buffer, &mapped_bytes, &device_address,
                                   &mapping);
    if (!EFI_ERROR(map_status))
        mapped = TRUE;
    geometry_ok = !EFI_ERROR(map_status) &&
        tu104_dma32_geometry_suitable((uint64_t)(UINTN)buffer,
                                      TU104_DMA_PROBE_BYTES) &&
        tu104_dma32_geometry_suitable((uint64_t)device_address,
                                      (uint64_t)mapped_bytes);
    note(L"CommonBuffer Map status=%r host=%lx device=%lx bytes=%lx "
         L"token=%lx identity=%d host_below4G=%d device_below4G=%d "
         L"geometry_suitable=%d\r\n",
         map_status, (UINT64)(UINTN)buffer, device_address,
         (UINT64)mapped_bytes, (UINT64)(UINTN)mapping,
         device_address == (EFI_PHYSICAL_ADDRESS)(UINTN)buffer,
         (UINT64)(UINTN)buffer <=
             TU104_DMA32_LIMIT - TU104_DMA_PROBE_BYTES,
         device_address <= TU104_DMA32_LIMIT - TU104_DMA_PROBE_BYTES,
         geometry_ok);
    note(L"The returned device address was never programmed into the GPU; "
         L"actual GPU DMA reachability was NOT tested.\r\n");
    status = EFI_ERROR(map_status) ? map_status :
             (geometry_ok ? EFI_SUCCESS : EFI_UNSUPPORTED);

cleanup:
    if (mapped) {
        unmap_status = uefi_call_wrapper(target->Unmap, 2,
                                         target, mapping);
        note(L"CommonBuffer Unmap status=%r\r\n", unmap_status);
        if (!EFI_ERROR(unmap_status))
            mapped = FALSE;
        else
            status = unmap_status;
    }

    if (allocated) {
        crc_after = option_rom_crc32((const uint8_t *)buffer,
                                     (size_t)TU104_DMA_PROBE_BYTES);
        note(L"Host canary after map lifecycle CRC32=%08x unchanged=%d\r\n",
             crc_after, crc_before == crc_after);
        if (crc_before != crc_after && !EFI_ERROR(status))
            status = EFI_CRC_ERROR;
    }

    if (allocated && !mapped) {
        free_status = uefi_call_wrapper(target->FreeBuffer, 3, target,
                                        DMA_MAP_PAGES, buffer);
        note(L"FreeBuffer status=%r\r\n", free_status);
        if (!EFI_ERROR(free_status))
            buffer = NULL, allocated = FALSE;
        else
            status = free_status;
    } else if (allocated) {
        note(L"Unmap failed: backing retained; no GPU address was programmed. "
             L"A full power cycle is recommended before retrying.\r\n");
    }

    if (!EFI_ERROR(read_config_header(target, after_config))) {
        config_unchanged = CompareMem(before_config, after_config,
                                      sizeof(before_config)) == 0;
    } else {
        config_unchanged = FALSE;
    }
    map_status = uefi_call_wrapper(target->Attributes, 4, target,
                                   EfiPciIoAttributeOperationSupported,
                                   0, &supported_after);
    if (!EFI_ERROR(map_status))
        map_status = uefi_call_wrapper(target->Attributes, 4, target,
                                       EfiPciIoAttributeOperationGet,
                                       0, &attributes_after);
    attributes_unchanged = !EFI_ERROR(map_status) &&
        supported_after == supported_before &&
        attributes_after == attributes_before;
    note(L"Post-probe config_unchanged=%d attributes_unchanged=%d "
         L"current=%lx supported=%lx query=%r\r\n",
         config_unchanged, attributes_unchanged,
         attributes_after, supported_after, map_status);
    if ((!config_unchanged || !attributes_unchanged) && !EFI_ERROR(status))
        status = EFI_DEVICE_ERROR;
    if (EFI_ERROR(log_status))
        status = log_status;
    note(L"END common-buffer service probe status=%r; mapped=%d "
         L"buffer_retained=%d.\r\n", status, mapped, allocated);
    return status;
}
