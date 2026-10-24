#ifndef TU104_FALCON_PROFILE_H
#define TU104_FALCON_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#define TU104_GSP_BASE UINT32_C(0x00110000)
#define TU104_SEC2_BASE UINT32_C(0x00840000)
#define TU104_FALCON_IMEM_BLOCK_BYTES UINT32_C(0x100)
#define TU104_FALCON_EXPECTED_IMEM_BLOCKS UINT32_C(0x100)

#define TU104_GENERIC_BL_IMAGE_BYTES UINT32_C(0x300)
#define TU104_GENERIC_BL_CODE_BYTES UINT32_C(0x200)
#define TU104_GENERIC_BL_DATA_OFFSET UINT32_C(0x200)
#define TU104_GENERIC_BL_DATA_BYTES UINT32_C(0x100)
#define TU104_GENERIC_BL_START_TAG UINT32_C(0xfd)
#define TU104_GENERIC_BL_BOOT_VECTOR UINT32_C(0xfd00)
#define TU104_GENERIC_BL_EXPECTED_DEST UINT32_C(0xfe00)

#define TU104_FALCON_TRANSCFG_FIELDS UINT32_C(0x7)
#define TU104_FALCON_TRANSCFG_COHERENT_PHYSICAL UINT32_C(0x5)
#define TU104_FALCON_FBIF_ALLOW_PHYS_NO_CTX UINT32_C(0x80)

enum tu104_falcon_field {
    TU104_FALCON_MAILBOX0 = 0,
    TU104_FALCON_MAILBOX1,
    TU104_FALCON_RM,
    TU104_FALCON_DEBUGINFO,
    TU104_FALCON_HWCFG2,
    TU104_FALCON_CPUCTL,
    TU104_FALCON_BOOTVEC,
    TU104_FALCON_HWCFG,
    TU104_FALCON_DMACTL,
    TU104_FALCON_ENGINE,
    TU104_FALCON_TRANSCFG0,
    TU104_FALCON_TRANSCFG4,
    TU104_FALCON_FBIF_CTL,
    TU104_FALCON_FIELD_COUNT
};

static const uint32_t
tu104_falcon_relative_offsets[TU104_FALCON_FIELD_COUNT] = {
    UINT32_C(0x0040), UINT32_C(0x0044), UINT32_C(0x0084),
    UINT32_C(0x0094), UINT32_C(0x00f4), UINT32_C(0x0100),
    UINT32_C(0x0104), UINT32_C(0x0108), UINT32_C(0x010c),
    UINT32_C(0x03c0), UINT32_C(0x0600), UINT32_C(0x0610),
    UINT32_C(0x0624),
};

static const uint32_t
tu104_gsp_initial_values[TU104_FALCON_FIELD_COUNT] = {
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x000007f7), UINT32_C(0x00000010),
    UINT32_C(0x00000000), UINT32_C(0x40420100),
    UINT32_C(0x00000080), UINT32_C(0x00000000),
    UINT32_C(0x00000110), UINT32_C(0x00000110),
    UINT32_C(0x00000190),
};

static const uint32_t
tu104_sec2_initial_values[TU104_FALCON_FIELD_COUNT] = {
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x000007f7), UINT32_C(0x00000010),
    UINT32_C(0x00000000), UINT32_C(0x40420100),
    UINT32_C(0x00000001), UINT32_C(0x00000000),
    UINT32_C(0x00000110), UINT32_C(0x00000110),
    UINT32_C(0x00000110),
};

static inline int
tu104_falcon_values_match(const uint32_t *values,
                          const uint32_t *expected)
{
    size_t index;

    if (values == NULL || expected == NULL)
        return 0;
    for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
        if (values[index] != expected[index])
            return 0;
    }
    return 1;
}

static inline int
tu104_falcon_initial_pair_matches(const uint32_t *gsp,
                                  const uint32_t *sec2)
{
    return tu104_falcon_values_match(gsp, tu104_gsp_initial_values) &&
           tu104_falcon_values_match(sec2, tu104_sec2_initial_values);
}

static inline uint32_t tu104_falcon_imem_blocks(uint32_t hwcfg)
{
    return hwcfg & UINT32_C(0x1ff);
}

static inline uint32_t tu104_generic_bl_imem_destination(uint32_t hwcfg)
{
    uint32_t blocks = tu104_falcon_imem_blocks(hwcfg);
    uint32_t loader_blocks =
        TU104_GENERIC_BL_CODE_BYTES / TU104_FALCON_IMEM_BLOCK_BYTES;

    if (blocks < loader_blocks)
        return UINT32_MAX;
    return (blocks - loader_blocks) * TU104_FALCON_IMEM_BLOCK_BYTES;
}

static inline uint32_t
tu104_falcon_loader_transcfg(uint32_t current)
{
    return (current & ~TU104_FALCON_TRANSCFG_FIELDS) |
           TU104_FALCON_TRANSCFG_COHERENT_PHYSICAL;
}

static inline uint32_t
tu104_falcon_allow_physical_no_context(uint32_t current)
{
    return current | TU104_FALCON_FBIF_ALLOW_PHYS_NO_CTX;
}

#endif
