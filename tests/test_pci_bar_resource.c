#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pci_bar_resource.h"

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static void put64(uint8_t *at, uint64_t value)
{
    unsigned index;

    for (index = 0; index < 8; ++index)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void make_descriptor(uint8_t bytes[48])
{
    memset(bytes, 0, 48);
    bytes[0] = ACPI_QWORD_ADDRESS_DESCRIPTOR;
    put16(bytes + 1, ACPI_QWORD_DESCRIPTOR_LENGTH);
    bytes[3] = 0;
    put64(bytes + 6, 32);
    put64(bytes + 14, UINT64_C(0xa3000000));
    put64(bytes + 22, UINT64_C(0xa3ffffff));
    put64(bytes + 30, 0);
    put64(bytes + 38, UINT64_C(0x01000000));
    bytes[46] = ACPI_END_TAG_DESCRIPTOR;
}

int main(void)
{
    uint8_t bytes[48];
    struct pci_bar_resource resource;

    make_descriptor(bytes);
    assert(parse_pci_bar_resource(bytes, sizeof(bytes), &resource));
    assert(resource.resource_type == 0);
    assert(resource.granularity == 32);
    assert(resource.minimum == UINT64_C(0xa3000000));
    assert(resource.maximum == UINT64_C(0xa3ffffff));
    assert(resource.translation == 0);
    assert(resource.length == UINT64_C(0x01000000));

    bytes[0] = 0;
    assert(!parse_pci_bar_resource(bytes, sizeof(bytes), &resource));
    make_descriptor(bytes);
    bytes[46] = 0;
    assert(!parse_pci_bar_resource(bytes, sizeof(bytes), &resource));
    make_descriptor(bytes);
    assert(!parse_pci_bar_resource(bytes, 47, &resource));

    puts("PCI BAR resource tests: 9 checks passed");
    return 0;
}
