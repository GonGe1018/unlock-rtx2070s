#ifndef OPTION_ROM_H
#define OPTION_ROM_H

#include <stddef.h>
#include <stdint.h>

#include "tu104_profile.h"

#define TU104_OBSERVED_EFI_ROM_SIZE ((size_t)130560u)

static inline uint16_t option_rom_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static inline int
tu104_option_rom_header_matches(const uint8_t *rom, size_t size,
                                uint16_t *pcir_offset_out)
{
    uint16_t pcir_offset;
    uint16_t vendor;
    uint16_t device;

    if (!rom || size < 0x1cu || rom[0] != 0x55u || rom[1] != 0xaau)
        return 0;
    pcir_offset = option_rom_le16(rom + 0x18u);
    if ((size_t)pcir_offset > size - 0x18u)
        return 0;
    if (rom[pcir_offset] != 'P' || rom[pcir_offset + 1] != 'C' ||
        rom[pcir_offset + 2] != 'I' || rom[pcir_offset + 3] != 'R')
        return 0;

    vendor = option_rom_le16(rom + pcir_offset + 4u);
    device = option_rom_le16(rom + pcir_offset + 6u);
    if (vendor != (uint16_t)TU104_PROFILE_VENDOR_ID ||
        device != (uint16_t)TU104_PROFILE_DEVICE_ID)
        return 0;

    if (pcir_offset_out)
        *pcir_offset_out = pcir_offset;
    return 1;
}

static inline uint32_t option_rom_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;

    for (index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                (UINT32_C(0xedb88320) &
                 (uint32_t)-(int32_t)(crc & UINT32_C(1)));
    }
    return ~crc;
}

#endif
