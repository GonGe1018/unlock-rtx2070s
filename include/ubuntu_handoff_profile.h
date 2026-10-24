#ifndef UBUNTU_HANDOFF_PROFILE_H
#define UBUNTU_HANDOFF_PROFILE_H

/* Exact same-boot handoff profile observed on this machine on 2026-10-25. */

#include <stddef.h>
#include <stdint.h>

#include "option_rom.h"

#define UBUNTU_GRUB_BYTES ((size_t)2660232u)
#define UBUNTU_GRUB_CRC32 UINT32_C(0x9b31e4cd)
#define UBUNTU_GRUB_PE_OFFSET UINT32_C(0x00000080)
#define UBUNTU_GRUB_IMAGE_BYTES UINT32_C(0x00289000)
#define UBUNTU_GRUB_PE_CHECKSUM UINT32_C(0x0029202d)
#define UBUNTU_GRUB_MACHINE_AMD64 UINT16_C(0x8664)
#define UBUNTU_GRUB_OPTIONAL_BYTES UINT16_C(0x00f0)
#define UBUNTU_GRUB_PE32_PLUS UINT16_C(0x020b)
#define UBUNTU_GRUB_EFI_APPLICATION UINT16_C(0x000a)

#define UBUNTU_GRUB_CFG_BYTES ((size_t)126u)
#define UBUNTU_GRUB_CFG_CRC32 UINT32_C(0x51c36111)

static inline uint32_t
ubuntu_handoff_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline int
ubuntu_handoff_grub_matches(const uint8_t *data, size_t bytes)
{
    uint32_t pe;
    const uint8_t *coff;
    const uint8_t *optional;

    if (data == NULL || bytes != UBUNTU_GRUB_BYTES ||
        data[0] != 'M' || data[1] != 'Z' ||
        option_rom_crc32(data, bytes) != UBUNTU_GRUB_CRC32)
        return 0;
    pe = ubuntu_handoff_le32(data + 0x3c);
    if (pe != UBUNTU_GRUB_PE_OFFSET || pe > bytes - 24 - 0x48)
        return 0;
    coff = data + pe;
    optional = coff + 24;
    if (coff[0] != 'P' || coff[1] != 'E' ||
        coff[2] != 0 || coff[3] != 0 ||
        option_rom_le16(coff + 4) != UBUNTU_GRUB_MACHINE_AMD64 ||
        option_rom_le16(coff + 20) != UBUNTU_GRUB_OPTIONAL_BYTES ||
        option_rom_le16(optional) != UBUNTU_GRUB_PE32_PLUS ||
        ubuntu_handoff_le32(optional + 0x38) !=
            UBUNTU_GRUB_IMAGE_BYTES ||
        ubuntu_handoff_le32(optional + 0x40) !=
            UBUNTU_GRUB_PE_CHECKSUM ||
        option_rom_le16(optional + 0x44) !=
            UBUNTU_GRUB_EFI_APPLICATION)
        return 0;
    return 1;
}

static inline int
ubuntu_handoff_config_matches(const uint8_t *data, size_t bytes)
{
    static const uint8_t expected[UBUNTU_GRUB_CFG_BYTES] =
        "search.fs_uuid fc61af23-4f55-4ce9-9df1-dc547db5442d root "
        "hd1,gpt2 \n"
        "set prefix=($root)'/boot/grub'\n"
        "configfile $prefix/grub.cfg\n";
    size_t index;

    if (data == NULL || bytes != sizeof(expected) ||
        option_rom_crc32(data, bytes) != UBUNTU_GRUB_CFG_CRC32)
        return 0;
    for (index = 0; index < sizeof(expected); ++index) {
        if (data[index] != expected[index])
            return 0;
    }
    return 1;
}

#endif
