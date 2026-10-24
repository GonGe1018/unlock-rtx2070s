#ifndef PCI_BAR_RESOURCE_H
#define PCI_BAR_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

#define ACPI_QWORD_ADDRESS_DESCRIPTOR UINT8_C(0x8a)
#define ACPI_END_TAG_DESCRIPTOR       UINT8_C(0x79)
#define ACPI_QWORD_DESCRIPTOR_LENGTH  UINT16_C(0x002b)

struct pci_bar_resource {
    uint8_t resource_type;
    uint64_t granularity;
    uint64_t minimum;
    uint64_t maximum;
    uint64_t translation;
    uint64_t length;
};

static inline uint16_t bar_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static inline uint64_t bar_le64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned index;

    for (index = 0; index < 8; ++index)
        value |= (uint64_t)bytes[index] << (index * 8);
    return value;
}

static inline int
parse_pci_bar_resource(const uint8_t *bytes, size_t available,
                       struct pci_bar_resource *out)
{
    uint16_t body_length;
    size_t descriptor_bytes;
    uint8_t sum = 0;
    size_t index;

    if (!bytes || !out || available < 48 ||
        bytes[0] != ACPI_QWORD_ADDRESS_DESCRIPTOR)
        return 0;
    body_length = bar_le16(bytes + 1);
    if (body_length != ACPI_QWORD_DESCRIPTOR_LENGTH)
        return 0;
    descriptor_bytes = (size_t)body_length + 3;
    if (descriptor_bytes + 2 > available ||
        bytes[descriptor_bytes] != ACPI_END_TAG_DESCRIPTOR)
        return 0;

    /* A zero end-tag checksum means "not generated". Otherwise verify it. */
    if (bytes[descriptor_bytes + 1] != 0) {
        for (index = 0; index < descriptor_bytes + 2; ++index)
            sum = (uint8_t)(sum + bytes[index]);
        if (sum != 0)
            return 0;
    }

    out->resource_type = bytes[3];
    out->granularity = bar_le64(bytes + 6);
    out->minimum = bar_le64(bytes + 14);
    out->maximum = bar_le64(bytes + 22);
    out->translation = bar_le64(bytes + 30);
    out->length = bar_le64(bytes + 38);
    return 1;
}

#endif
