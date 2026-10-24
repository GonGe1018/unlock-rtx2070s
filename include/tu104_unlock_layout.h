#ifndef TU104_UNLOCK_LAYOUT_H
#define TU104_UNLOCK_LAYOUT_H

/*
 * Pure host-memory preparation for the target-pinned TU104 volatile unlock.
 *
 * This file performs no allocation, MMIO, DMA mapping, or firmware start.  It
 * builds the 32 MiB immutable input pool consumed later by native FWSEC and
 * the production TU10x Booter.  The framebuffer layout follows NVIDIA's
 * public TU102 GspFwWprMeta definition and kgspPopulateWprMeta_TU102 formulas.
 */

#include <stddef.h>
#include <stdint.h>

#define TU104_UNLOCK_POOL_BYTES UINT64_C(0x02000000)
#define TU104_UNLOCK_PAGE_BYTES UINT64_C(0x00001000)
#define TU104_UNLOCK_PHYS_MIN UINT64_C(0x100000000)
#define TU104_UNLOCK_PHYS_LIMIT UINT64_C(0x8000000000)

#define TU104_UNLOCK_FWSEC_OFFSET UINT32_C(0x00000000)
#define TU104_UNLOCK_BOOTER_OFFSET UINT32_C(0x0000a000)
#define TU104_UNLOCK_V67_OFFSET UINT32_C(0x00019000)
#define TU104_UNLOCK_BOOT_OFFSET UINT32_C(0x00029000)
#define TU104_UNLOCK_META_OFFSET UINT32_C(0x0002f000)
#define TU104_UNLOCK_RADIX_OFFSET UINT32_C(0x00030000)
#define TU104_UNLOCK_DATA_OFFSET UINT32_C(0x00041000)

#define TU104_UNLOCK_BOOTER_BYTES UINT32_C(0x0000e700)
#define TU104_UNLOCK_BOOTER_PATCH_OFFSET UINT32_C(0x00008700)
#define TU104_UNLOCK_BOOTER_SIGNATURE_BYTES UINT32_C(0x00000010)
#define TU104_UNLOCK_V67_BYTES UINT32_C(0x0000fa00)
#define TU104_UNLOCK_BOOT_IMAGE_BYTES UINT32_C(0x00006000)
#define TU104_UNLOCK_BOOT_CONTAINER_BYTES UINT32_C(0x00020000)
#define TU104_UNLOCK_META_BYTES UINT32_C(0x00000100)
#define TU104_UNLOCK_DUMMY_ELF_BYTES UINT32_C(0x01bfadc8)

#define TU104_UNLOCK_BOOTER_CRC32 UINT32_C(0x00568187)
#define TU104_UNLOCK_BOOTER_SIGNATURE_CRC32 UINT32_C(0x48e70666)
#define TU104_UNLOCK_PATCHED_BOOTER_CRC32 UINT32_C(0x555f0915)
#define TU104_UNLOCK_V67_CRC32 UINT32_C(0xf4a4754c)
#define TU104_UNLOCK_BOOT_PREFIX_CRC32 UINT32_C(0x6ebed2ee)
#define TU104_UNLOCK_BOOT_CONTAINER_CRC32 UINT32_C(0x7ee8cdcd)

#define TU104_UNLOCK_FB_BYTES UINT64_C(0x0000000200000000)
#define TU104_UNLOCK_VGA_WORKSPACE_OFFSET UINT64_C(0x00000001fffe0000)
#define TU104_UNLOCK_VGA_WORKSPACE_BYTES UINT64_C(0x0000000000020000)
#define TU104_UNLOCK_FRTS_OFFSET UINT64_C(0x00000001ffee0000)
#define TU104_UNLOCK_FRTS_BYTES UINT64_C(0x0000000000100000)
#define TU104_UNLOCK_HEAP_BYTES UINT64_C(0x0000000006900000)
#define TU104_UNLOCK_PRESCRUB_BYTES UINT64_C(0x0000000010000000)

#define TU104_UNLOCK_META_MAGIC UINT64_C(0xdc3aae21371a60b3)
#define TU104_UNLOCK_META_REVISION UINT64_C(1)
#define TU104_UNLOCK_CLOCK_BOOST_FLAG UINT8_C(1)
#define TU104_UNLOCK_META_FLAGS_OFFSET UINT32_C(241)

#define TU104_UNLOCK_RADIX_L2_PAGES UINT32_C(14)
#define TU104_UNLOCK_DUMMY_ELF_PAGES UINT32_C(7163)

enum tu104_unlock_layout_status {
    TU104_UNLOCK_LAYOUT_OK = 0,
    TU104_UNLOCK_LAYOUT_GUARD = 1,
    TU104_UNLOCK_LAYOUT_VERIFY = 2
};

struct tu104_unlock_layout_input {
    uint8_t *pool;
    size_t pool_bytes;
    uint64_t pool_physical;

    const uint8_t *booter;
    size_t booter_bytes;
    const uint8_t *booter_signature;
    size_t booter_signature_bytes;
    const uint8_t *v67;
    size_t v67_bytes;
    const uint8_t *boot_container;
    size_t boot_container_bytes;
};

static inline uint32_t
tu104_unlock_layout_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline uint64_t
tu104_unlock_layout_le64(const uint8_t *bytes)
{
    return (uint64_t)tu104_unlock_layout_le32(bytes) |
           ((uint64_t)tu104_unlock_layout_le32(bytes + 4) << 32);
}

static inline void
tu104_unlock_layout_put32(uint8_t *bytes, uint32_t value)
{
    unsigned index;

    for (index = 0; index < 4; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static inline void
tu104_unlock_layout_put64(uint8_t *bytes, uint64_t value)
{
    tu104_unlock_layout_put32(bytes, (uint32_t)value);
    tu104_unlock_layout_put32(bytes + 4, (uint32_t)(value >> 32));
}

static inline void
tu104_unlock_layout_zero(uint8_t *destination, size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index)
        destination[index] = 0;
}

static inline void
tu104_unlock_layout_copy(uint8_t *destination, const uint8_t *source,
                         size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index)
        destination[index] = source[index];
}

static inline int
tu104_unlock_layout_equal(const uint8_t *first, const uint8_t *second,
                          size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index) {
        if (first[index] != second[index])
            return 0;
    }
    return 1;
}

static inline uint32_t
tu104_unlock_layout_crc32(const uint8_t *data, size_t bytes)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;

    if (data == NULL)
        return 0;
    for (index = 0; index < bytes; ++index) {
        unsigned bit;

        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                (UINT32_C(0xedb88320) &
                 (uint32_t)-(int32_t)(crc & UINT32_C(1)));
    }
    return ~crc;
}

static inline int
tu104_unlock_pool_geometry_suitable(uint64_t physical, size_t bytes)
{
    return bytes == (size_t)TU104_UNLOCK_POOL_BYTES &&
           physical >= TU104_UNLOCK_PHYS_MIN &&
           (physical & (TU104_UNLOCK_PAGE_BYTES - 1)) == 0 &&
           physical <= TU104_UNLOCK_PHYS_LIMIT - TU104_UNLOCK_POOL_BYTES;
}

static inline int
tu104_unlock_assets_match(const struct tu104_unlock_layout_input *input)
{
    static const uint8_t production_signature[16] = {
        UINT8_C(0x32), UINT8_C(0x60), UINT8_C(0x43), UINT8_C(0x67),
        UINT8_C(0x98), UINT8_C(0x89), UINT8_C(0x7b), UINT8_C(0x06),
        UINT8_C(0xe8), UINT8_C(0x5f), UINT8_C(0x1a), UINT8_C(0xa8),
        UINT8_C(0x71), UINT8_C(0x43), UINT8_C(0x5d), UINT8_C(0xcb),
    };
    size_t index;

    if (input == NULL || input->booter == NULL ||
        input->booter_signature == NULL || input->v67 == NULL ||
        input->boot_container == NULL ||
        input->booter_bytes != TU104_UNLOCK_BOOTER_BYTES ||
        input->booter_signature_bytes !=
            TU104_UNLOCK_BOOTER_SIGNATURE_BYTES ||
        input->v67_bytes != TU104_UNLOCK_V67_BYTES ||
        input->boot_container_bytes != TU104_UNLOCK_BOOT_CONTAINER_BYTES)
        return 0;
    if (tu104_unlock_layout_crc32(input->booter, input->booter_bytes) !=
            TU104_UNLOCK_BOOTER_CRC32 ||
        tu104_unlock_layout_crc32(input->booter_signature,
                                  input->booter_signature_bytes) !=
            TU104_UNLOCK_BOOTER_SIGNATURE_CRC32 ||
        tu104_unlock_layout_crc32(input->v67, input->v67_bytes) !=
            TU104_UNLOCK_V67_CRC32 ||
        tu104_unlock_layout_crc32(input->boot_container,
                                  TU104_UNLOCK_BOOT_IMAGE_BYTES) !=
            TU104_UNLOCK_BOOT_PREFIX_CRC32 ||
        tu104_unlock_layout_crc32(input->boot_container,
                                  input->boot_container_bytes) !=
            TU104_UNLOCK_BOOT_CONTAINER_CRC32)
        return 0;
    if (!tu104_unlock_layout_equal(input->booter_signature,
                                    production_signature,
                                    sizeof(production_signature)))
        return 0;
    for (index = 0; index < TU104_UNLOCK_BOOTER_SIGNATURE_BYTES; ++index) {
        if (input->booter[TU104_UNLOCK_BOOTER_PATCH_OFFSET + index] != 0)
            return 0;
    }

    /* Target-specific canaries supplement the full-blob CRC pin. */
    if (tu104_unlock_layout_le32(input->v67 + UINT32_C(0xbb20)) != 8 ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xbb38)) !=
            UINT32_C(0x0040966c) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xbb68)) !=
            UINT32_C(0x00409650) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xbb98)) !=
            UINT32_C(0x001fa828) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xbbf8)) !=
            UINT32_C(0x001fa824) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xf974)) !=
            UINT32_C(0x00409650) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xf988)) !=
            UINT32_C(0x88888888) ||
        tu104_unlock_layout_le32(input->v67 + UINT32_C(0xf9a4)) !=
            UINT32_C(0x00409664))
        return 0;
    return 1;
}

static inline int
tu104_unlock_layout_prepare(struct tu104_unlock_layout_input *input)
{
    uint8_t *meta;
    uint8_t *radix;
    uint64_t values[32] = {0};
    uint64_t pages;
    uint64_t l2_pages;
    uint64_t megabyte = UINT64_C(0x100000);
    uint64_t index;

    if (input == NULL || input->pool == NULL ||
        !tu104_unlock_pool_geometry_suitable(input->pool_physical,
                                              input->pool_bytes) ||
        !tu104_unlock_assets_match(input))
        return TU104_UNLOCK_LAYOUT_GUARD;

    pages = (TU104_UNLOCK_DUMMY_ELF_BYTES +
             TU104_UNLOCK_PAGE_BYTES - 1) / TU104_UNLOCK_PAGE_BYTES;
    l2_pages = (pages + UINT64_C(511)) / UINT64_C(512);
    if (pages != TU104_UNLOCK_DUMMY_ELF_PAGES ||
        l2_pages != TU104_UNLOCK_RADIX_L2_PAGES ||
        TU104_UNLOCK_RADIX_OFFSET +
                (UINT64_C(2) + l2_pages) * TU104_UNLOCK_PAGE_BYTES >
            TU104_UNLOCK_DATA_OFFSET ||
        TU104_UNLOCK_DATA_OFFSET + pages * TU104_UNLOCK_PAGE_BYTES >
            TU104_UNLOCK_POOL_BYTES)
        return TU104_UNLOCK_LAYOUT_GUARD;

    tu104_unlock_layout_zero(input->pool, input->pool_bytes);
    tu104_unlock_layout_copy(input->pool + TU104_UNLOCK_BOOTER_OFFSET,
                             input->booter, input->booter_bytes);
    tu104_unlock_layout_copy(
        input->pool + TU104_UNLOCK_BOOTER_OFFSET +
            TU104_UNLOCK_BOOTER_PATCH_OFFSET,
        input->booter_signature, input->booter_signature_bytes);
    tu104_unlock_layout_copy(input->pool + TU104_UNLOCK_V67_OFFSET,
                             input->v67, input->v67_bytes);
    tu104_unlock_layout_copy(input->pool + TU104_UNLOCK_BOOT_OFFSET,
                             input->boot_container,
                             TU104_UNLOCK_BOOT_IMAGE_BYTES);

    meta = input->pool + TU104_UNLOCK_META_OFFSET;
    values[0] = TU104_UNLOCK_META_MAGIC;
    values[1] = TU104_UNLOCK_META_REVISION;
    values[2] = input->pool_physical + TU104_UNLOCK_RADIX_OFFSET;
    values[3] = TU104_UNLOCK_DUMMY_ELF_BYTES;
    values[4] = input->pool_physical + TU104_UNLOCK_BOOT_OFFSET;
    values[5] = TU104_UNLOCK_BOOT_IMAGE_BYTES;
    values[6] = UINT64_C(0x1800);
    values[7] = UINT64_C(0x0800);
    values[8] = 0;
    values[9] = input->pool_physical + TU104_UNLOCK_V67_OFFSET;
    values[10] = TU104_UNLOCK_V67_BYTES;
    values[22] = TU104_UNLOCK_FB_BYTES;
    values[23] = TU104_UNLOCK_VGA_WORKSPACE_OFFSET;
    values[24] = TU104_UNLOCK_VGA_WORKSPACE_BYTES;
    values[21] = values[23];
    values[20] = TU104_UNLOCK_FRTS_BYTES;
    values[19] = values[21] - values[20];
    values[18] = (values[19] - values[5]) & ~UINT64_C(0xfff);
    values[17] = (values[18] - values[3]) & ~UINT64_C(0xffff);
    values[16] = TU104_UNLOCK_HEAP_BYTES;
    values[15] = (values[17] - values[16]) & ~(megabyte - 1);
    values[14] = values[15] - megabyte;
    values[13] = megabyte;
    values[12] = values[14] - values[13];
    values[11] = values[12];

    if (values[19] != TU104_UNLOCK_FRTS_OFFSET ||
        values[21] != TU104_UNLOCK_VGA_WORKSPACE_OFFSET ||
        values[15] != UINT64_C(0x1f7900000) ||
        values[17] != UINT64_C(0x1fe2d0000) ||
        TU104_UNLOCK_FB_BYTES - values[11] >
            TU104_UNLOCK_PRESCRUB_BYTES ||
        values[15] + values[16] > values[17])
        return TU104_UNLOCK_LAYOUT_GUARD;

    for (index = 0; index < 32; ++index)
        tu104_unlock_layout_put64(meta + index * 8, values[index]);
    meta[TU104_UNLOCK_META_FLAGS_OFFSET] = TU104_UNLOCK_CLOCK_BOOST_FLAG;

    radix = input->pool + TU104_UNLOCK_RADIX_OFFSET;
    tu104_unlock_layout_put64(
        radix, input->pool_physical + TU104_UNLOCK_RADIX_OFFSET +
            TU104_UNLOCK_PAGE_BYTES);
    for (index = 0; index < l2_pages; ++index) {
        tu104_unlock_layout_put64(
            radix + TU104_UNLOCK_PAGE_BYTES + index * 8,
            input->pool_physical + TU104_UNLOCK_RADIX_OFFSET +
                UINT64_C(2) * TU104_UNLOCK_PAGE_BYTES +
                index * TU104_UNLOCK_PAGE_BYTES);
    }
    for (index = 0; index < pages; ++index) {
        tu104_unlock_layout_put64(
            radix + UINT64_C(2) * TU104_UNLOCK_PAGE_BYTES + index * 8,
            input->pool_physical + TU104_UNLOCK_DATA_OFFSET +
                index * TU104_UNLOCK_PAGE_BYTES);
    }

    if (tu104_unlock_layout_crc32(
            input->pool + TU104_UNLOCK_BOOTER_OFFSET,
            TU104_UNLOCK_BOOTER_BYTES) !=
            TU104_UNLOCK_PATCHED_BOOTER_CRC32 ||
        tu104_unlock_layout_crc32(
            input->pool + TU104_UNLOCK_V67_OFFSET,
            TU104_UNLOCK_V67_BYTES) != TU104_UNLOCK_V67_CRC32 ||
        tu104_unlock_layout_crc32(
            input->pool + TU104_UNLOCK_BOOT_OFFSET,
            TU104_UNLOCK_BOOT_IMAGE_BYTES) !=
            TU104_UNLOCK_BOOT_PREFIX_CRC32)
        return TU104_UNLOCK_LAYOUT_VERIFY;
    return TU104_UNLOCK_LAYOUT_OK;
}

#endif
