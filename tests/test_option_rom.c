#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "option_rom.h"

static void make_valid_header(uint8_t rom[512])
{
    memset(rom, 0, 512);
    rom[0] = 0x55;
    rom[1] = 0xaa;
    rom[0x18] = 0x20;
    rom[0x20] = 'P';
    rom[0x21] = 'C';
    rom[0x22] = 'I';
    rom[0x23] = 'R';
    rom[0x24] = 0xde;
    rom[0x25] = 0x10;
    rom[0x26] = 0x84;
    rom[0x27] = 0x1e;
}

int main(void)
{
    static const uint8_t known_vector[] = "123456789";
    uint8_t rom[512];
    uint16_t pcir_offset = 0;

    assert(option_rom_crc32(known_vector, sizeof(known_vector) - 1) ==
           UINT32_C(0xcbf43926));

    make_valid_header(rom);
    assert(tu104_option_rom_header_matches(rom, sizeof(rom), &pcir_offset));
    assert(pcir_offset == 0x20);

    rom[0] = 0;
    assert(!tu104_option_rom_header_matches(rom, sizeof(rom), NULL));
    make_valid_header(rom);
    rom[0x20] = 'X';
    assert(!tu104_option_rom_header_matches(rom, sizeof(rom), NULL));
    make_valid_header(rom);
    rom[0x26] = 0x07;
    rom[0x27] = 0x1e;
    assert(!tu104_option_rom_header_matches(rom, sizeof(rom), NULL));
    make_valid_header(rom);
    rom[0x18] = 0xff;
    rom[0x19] = 0xff;
    assert(!tu104_option_rom_header_matches(rom, sizeof(rom), NULL));

    puts("option ROM tests: 8 checks passed");
    return 0;
}
