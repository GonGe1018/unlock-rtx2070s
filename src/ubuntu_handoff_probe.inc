/* Included only by TU104_HANDOFF_PROBE.
 *
 * This read-only integration stage proves that the USB application can find,
 * validate, preload, and start the exact installed Ubuntu GRUB image in the
 * same UEFI boot. It performs no GPU MMIO, PCI write, DMA, reset, firmware
 * execution, boot-variable update, or persistent system-ESP modification.
 */

#include "ubuntu_handoff_profile.h"

#define UBUNTU_HANDOFF_MAX_FS_HANDLES ((UINTN)256)

static const CHAR16 ubuntu_grub_path[] = L"\\EFI\\ubuntu\\grubx64.efi";
static const CHAR16 ubuntu_grub_cfg_path[] = L"\\EFI\\ubuntu\\grub.cfg";

struct ubuntu_handoff {
    EFI_HANDLE parent_image;
    EFI_HANDLE volume;
    EFI_HANDLE child_image;
    EFI_DEVICE_PATH *device_path;
    BOOLEAN loaded;
};

static EFI_STATUS
ubuntu_handoff_check_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path,
                          BOOLEAN config)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINT8 *data = NULL;
    UINTN expected = config ? UBUNTU_GRUB_CFG_BYTES : UBUNTU_GRUB_BYTES;
    UINTN bytes;
    EFI_STATUS status;
    EFI_STATUS close_status;
    BOOLEAN matched = FALSE;

    if (!root || !path)
        return EFI_INVALID_PARAMETER;
    status = uefi_call_wrapper(root->Open, 5, root, &file, (CHAR16 *)path,
                               EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file)
        return status;
    info = LibFileInfo(file);
    if (!info) {
        status = EFI_DEVICE_ERROR;
        goto finished;
    }
    if (info->FileSize != expected ||
        (info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        status = EFI_COMPROMISED_DATA;
        goto finished;
    }
    data = AllocatePool(expected);
    if (!data) {
        status = EFI_OUT_OF_RESOURCES;
        goto finished;
    }
    SetMem(data, expected, 0xa5);
    bytes = expected;
    status = uefi_call_wrapper(file->Read, 3, file, &bytes, data);
    if (EFI_ERROR(status) || bytes != expected) {
        if (!EFI_ERROR(status))
            status = EFI_DEVICE_ERROR;
        goto finished;
    }
    matched = config ?
        ubuntu_handoff_config_matches(data, bytes) :
        ubuntu_handoff_grub_matches(data, bytes);
    status = matched ? EFI_SUCCESS : EFI_CRC_ERROR;
    note(L"Ubuntu handoff file profile: path=%s bytes=%ld crc=%08x "
         L"matched=%d status=%r\r\n", path, (UINT64)bytes,
         option_rom_crc32((const uint8_t *)data, bytes), matched, status);

finished:
    if (data)
        FreePool(data);
    if (info)
        FreePool(info);
    close_status = uefi_call_wrapper(file->Close, 1, file);
    if (!EFI_ERROR(status) && EFI_ERROR(close_status))
        status = close_status;
    return status;
}

static void ubuntu_handoff_release(struct ubuntu_handoff *handoff)
{
    if (!handoff)
        return;
    if (handoff->loaded && handoff->child_image) {
        uefi_call_wrapper(BS->UnloadImage, 1, handoff->child_image);
        handoff->loaded = FALSE;
        handoff->child_image = NULL;
    }
    if (handoff->device_path) {
        FreePool(handoff->device_path);
        handoff->device_path = NULL;
    }
}

static EFI_STATUS
ubuntu_handoff_preload(EFI_HANDLE parent_image,
                       struct ubuntu_handoff *handoff)
{
    EFI_HANDLE *handles = NULL;
    EFI_HANDLE volume = NULL;
    EFI_LOADED_IMAGE *loaded = NULL;
    UINTN count = 0;
    UINTN matches = 0;
    UINTN index;
    EFI_STATUS status;

    if (!parent_image || !handoff)
        return EFI_INVALID_PARAMETER;
    SetMem(handoff, sizeof(*handoff), 0);
    handoff->parent_image = parent_image;
    status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol,
                               &simple_fs_guid, NULL, &count, &handles);
    note(L"Ubuntu handoff SimpleFS enumeration status=%r count=%d\r\n",
         status, count);
    if (EFI_ERROR(status))
        return status;
    if (!handles || count == 0 || count > UBUNTU_HANDOFF_MAX_FS_HANDLES) {
        status = EFI_COMPROMISED_DATA;
        goto finished;
    }

    for (index = 0; index < count; ++index) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
        EFI_FILE_PROTOCOL *root = NULL;
        EFI_STATUS candidate;
        EFI_STATUS close_status;

        candidate = uefi_call_wrapper(BS->HandleProtocol, 3,
                                      handles[index], &simple_fs_guid,
                                      (VOID **)&file_system);
        if (EFI_ERROR(candidate) || !file_system) {
            status = EFI_DEVICE_ERROR;
            goto finished;
        }
        candidate = uefi_call_wrapper(file_system->OpenVolume, 2,
                                      file_system, &root);
        if (EFI_ERROR(candidate) || !root) {
            status = EFI_DEVICE_ERROR;
            goto finished;
        }
        candidate = ubuntu_handoff_check_file(
            root, ubuntu_grub_path, FALSE);
        if (candidate == EFI_NOT_FOUND) {
            close_status = uefi_call_wrapper(root->Close, 1, root);
            if (EFI_ERROR(close_status)) {
                status = close_status;
                goto finished;
            }
            continue;
        }
        if (!EFI_ERROR(candidate))
            candidate = ubuntu_handoff_check_file(
                root, ubuntu_grub_cfg_path, TRUE);
        close_status = uefi_call_wrapper(root->Close, 1, root);
        if (!EFI_ERROR(candidate) && EFI_ERROR(close_status))
            candidate = close_status;
        if (EFI_ERROR(candidate)) {
            status = candidate;
            goto finished;
        }
        ++matches;
        volume = handles[index];
    }
    if (matches != 1 || !volume) {
        status = matches ? EFI_NO_MAPPING : EFI_NOT_FOUND;
        goto finished;
    }

    handoff->volume = volume;
    handoff->device_path = FileDevicePath(
        volume, (CHAR16 *)ubuntu_grub_path);
    if (!handoff->device_path) {
        status = EFI_OUT_OF_RESOURCES;
        goto finished;
    }
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, parent_image,
                               handoff->device_path, NULL, 0,
                               &handoff->child_image);
    note(L"Ubuntu direct-GRUB LoadImage status=%r candidates=%d "
         L"volume=%lx child=%lx\r\n", status, matches,
         (UINT64)(UINTN)volume,
         (UINT64)(UINTN)handoff->child_image);
    if (EFI_ERROR(status) || !handoff->child_image)
        goto finished;
    handoff->loaded = TRUE;
    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               handoff->child_image,
                               &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status) || !loaded ||
        loaded->ParentHandle != parent_image ||
        loaded->DeviceHandle != volume ||
        loaded->ImageCodeType != EfiLoaderCode ||
        loaded->ImageDataType != EfiLoaderData ||
        loaded->ImageSize != UBUNTU_GRUB_IMAGE_BYTES) {
        status = EFI_COMPROMISED_DATA;
        goto finished;
    }
    note(L"Ubuntu loaded-image guard: parent=%lx device=%lx "
         L"base=%lx image-bytes=%lx code-type=%d data-type=%d MATCH\r\n",
         (UINT64)(UINTN)loaded->ParentHandle,
         (UINT64)(UINTN)loaded->DeviceHandle,
         (UINT64)(UINTN)loaded->ImageBase, loaded->ImageSize,
         loaded->ImageCodeType, loaded->ImageDataType);
    status = EFI_ERROR(log_status) ? log_status : EFI_SUCCESS;

finished:
    if (handles)
        FreePool(handles);
    if (EFI_ERROR(status))
        ubuntu_handoff_release(handoff);
    return status;
}

static EFI_STATUS ubuntu_handoff_start(struct ubuntu_handoff *handoff)
{
    CHAR16 *exit_data = NULL;
    UINTN exit_data_bytes = 0;
    EFI_STATUS status;

    if (!handoff || !handoff->loaded || !handoff->child_image)
        return EFI_NOT_READY;
    note(L"HANDOFF READY: exact installed Ubuntu GRUB is preloaded. "
         L"Closing the USB log, then StartImage; no boot variable write.\r\n");
    status = flush_log();
    if (EFI_ERROR(status))
        return status;
    if (log_file) {
        status = uefi_call_wrapper(log_file->Close, 1, log_file);
        log_file = NULL;
        if (EFI_ERROR(status))
            return status;
    }
    if (ST && ST->ConOut)
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut,
                          L"Starting exact Ubuntu GRUB now...\r\n");
    status = uefi_call_wrapper(BS->StartImage, 3,
                               handoff->child_image,
                               &exit_data_bytes, &exit_data);
    if (exit_data)
        FreePool(exit_data);
    return status;
}

#ifdef TU104_HANDOFF_PROBE
static EFI_STATUS run_ubuntu_handoff_probe(EFI_HANDLE image)
{
    struct ubuntu_handoff handoff;
    EFI_STATUS status;

    SetMem(&handoff, sizeof(handoff), 0);
    status = ubuntu_handoff_preload(image, &handoff);
    note(L"Read-only same-boot Ubuntu preload status=%r\r\n", status);
    if (!EFI_ERROR(status))
        status = ubuntu_handoff_start(&handoff);
    /* Reached only when preload failed or the child unexpectedly returned. */
    ubuntu_handoff_release(&handoff);
    return status;
}
#endif
