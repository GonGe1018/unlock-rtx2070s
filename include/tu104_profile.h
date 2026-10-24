#ifndef TU104_PROFILE_H
#define TU104_PROFILE_H

#include <stdint.h>

#define TU104_PROFILE_SEGMENT          0u
#define TU104_PROFILE_BUS              1u
#define TU104_PROFILE_DEVICE           0u
#define TU104_PROFILE_FUNCTION         0u

#define TU104_PROFILE_VENDOR_ID        UINT32_C(0x10de)
#define TU104_PROFILE_DEVICE_ID        UINT32_C(0x1e84)
#define TU104_PROFILE_SUBVENDOR_ID     UINT32_C(0x1458)
#define TU104_PROFILE_SUBDEVICE_ID     UINT32_C(0x3ff5)
#define TU104_PROFILE_CLASS_CODE       UINT32_C(0x030000)

/* PCI configuration dwords are little-endian: device/subdevice in high 16. */
#define TU104_PROFILE_PCI_ID \
    ((TU104_PROFILE_DEVICE_ID << 16) | TU104_PROFILE_VENDOR_ID)
#define TU104_PROFILE_SUBSYSTEM \
    ((TU104_PROFILE_SUBDEVICE_ID << 16) | TU104_PROFILE_SUBVENDOR_ID)

#define TU104_PARENT_SEGMENT           0u
#define TU104_PARENT_BUS               0u
#define TU104_PARENT_DEVICE            1u
#define TU104_PARENT_FUNCTION          0u

static inline int
tu104_location_matches(uint64_t segment, uint64_t bus,
                       uint64_t device, uint64_t function)
{
    return segment == TU104_PROFILE_SEGMENT &&
           bus == TU104_PROFILE_BUS &&
           device == TU104_PROFILE_DEVICE &&
           function == TU104_PROFILE_FUNCTION;
}

static inline int
tu104_parent_location_matches(uint64_t segment, uint64_t bus,
                              uint64_t device, uint64_t function)
{
    return segment == TU104_PARENT_SEGMENT &&
           bus == TU104_PARENT_BUS &&
           device == TU104_PARENT_DEVICE &&
           function == TU104_PARENT_FUNCTION;
}

static inline int
tu104_identity_matches(uint32_t pci_id, uint32_t subsystem,
                       uint32_t class_revision)
{
    uint32_t class_code = (class_revision >> 8) & UINT32_C(0x00ffffff);

    return pci_id == TU104_PROFILE_PCI_ID &&
           subsystem == TU104_PROFILE_SUBSYSTEM &&
           class_code == TU104_PROFILE_CLASS_CODE;
}

#endif
