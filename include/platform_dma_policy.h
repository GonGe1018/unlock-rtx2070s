#ifndef TU104_PLATFORM_DMA_POLICY_H
#define TU104_PLATFORM_DMA_POLICY_H

#include <stddef.h>
#include <stdint.h>

#define TU104_SETUP_BYTES ((size_t)0x16f1)
#define TU104_SETUP_VTD_OFFSET ((size_t)0x0a1d)
#define TU104_SETUP_ABOVE4G_MMIO_OFFSET ((size_t)0x0a1e)
#define TU104_SETUP_MEMORY_REMAP_OFFSET ((size_t)0x0aef)
#define TU104_SETUP_ABOVE4G_MINING_OFFSET ((size_t)0x1432)
#define TU104_SETUP_CSM_OFFSET ((size_t)0x145e)

#define TU104_MCHBAR_VTD2_OFFSET UINT64_C(0x5410)
#define TU104_EXPECTED_MCHBAR UINT64_C(0xfed10000)
#define TU104_EXPECTED_VTD2_BASE UINT64_C(0xfed91000)

#define TU104_VTD_GSTS_TES UINT32_C(0x80000000)
#define TU104_VTD_CAP_PLMR UINT64_C(0x20)
#define TU104_VTD_CAP_PHMR UINT64_C(0x40)
#define TU104_VTD_PMEN_EPM UINT32_C(0x80000000)
#define TU104_VTD_PMEN_PRS UINT32_C(0x00000001)

static inline int
tu104_setup_dma_profile_matches(const uint8_t *data, size_t size)
{
    return data != NULL && size == TU104_SETUP_BYTES &&
        data[TU104_SETUP_VTD_OFFSET] == 0 &&
        data[TU104_SETUP_ABOVE4G_MMIO_OFFSET] == 0 &&
        data[TU104_SETUP_MEMORY_REMAP_OFFSET] == 1 &&
        data[TU104_SETUP_ABOVE4G_MINING_OFFSET] == 0 &&
        data[TU104_SETUP_CSM_OFFSET] == 1;
}

static inline uint64_t
tu104_mchbar_address(uint32_t low, uint32_t high)
{
    return (((uint64_t)high << 32) | low) & ~UINT64_C(0x7fff);
}

static inline int
tu104_mchbar_profile_matches(uint32_t low, uint32_t high)
{
    uint64_t raw = ((uint64_t)high << 32) | low;

    return (raw & UINT64_C(1)) != 0 &&
        tu104_mchbar_address(low, high) == TU104_EXPECTED_MCHBAR;
}

static inline uint64_t
tu104_vtd2_address(uint32_t raw)
{
    return (uint64_t)(raw & ~UINT32_C(0xfff));
}

static inline int
tu104_vtd2_enabled(uint32_t raw)
{
    return (raw & UINT32_C(1)) != 0;
}

static inline int
tu104_vtd2_profile_matches(uint32_t raw)
{
    return tu104_vtd2_address(raw) == TU104_EXPECTED_VTD2_BASE;
}

static inline int
tu104_vtd_has_protected_memory(uint64_t capability)
{
    return (capability & (TU104_VTD_CAP_PLMR | TU104_VTD_CAP_PHMR)) != 0;
}

static inline int
tu104_vtd_translation_enabled(uint32_t global_status)
{
    return (global_status & TU104_VTD_GSTS_TES) != 0;
}

static inline int
tu104_vtd_protected_memory_active(uint32_t protected_memory_enable)
{
    return (protected_memory_enable &
            (TU104_VTD_PMEN_EPM | TU104_VTD_PMEN_PRS)) != 0;
}

static inline int
tu104_vtd_observation_clear(uint32_t version, uint64_t capability,
                            uint32_t global_status,
                            uint32_t protected_memory_enable,
                            int protected_memory_was_read)
{
    if (version != UINT32_C(0x10) ||
        tu104_vtd_translation_enabled(global_status))
        return 0;
    if (tu104_vtd_has_protected_memory(capability) !=
        !!protected_memory_was_read)
        return 0;
    return !protected_memory_was_read ||
        !tu104_vtd_protected_memory_active(protected_memory_enable);
}

#endif
