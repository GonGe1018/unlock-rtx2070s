#ifndef DMA_MAP_POLICY_H
#define DMA_MAP_POLICY_H

#include <stddef.h>
#include <stdint.h>

#define TU104_DMA_PROBE_BYTES UINT64_C(0x02000000)
#define TU104_DMA_PAGE_BYTES  UINT64_C(0x00001000)
#define TU104_DMA32_LIMIT     UINT64_C(0x100000000)
#define TU104_DMA_LIMIT       UINT64_C(0x10000000000)

static inline int
tu104_dma_geometry_suitable(uint64_t device_address, uint64_t mapped_bytes)
{
    return mapped_bytes == TU104_DMA_PROBE_BYTES &&
           device_address != 0 &&
           (device_address & (TU104_DMA_PAGE_BYTES - 1)) == 0 &&
           device_address <= TU104_DMA_LIMIT - TU104_DMA_PROBE_BYTES;
}

static inline int
tu104_dma64_geometry_suitable(uint64_t address, uint64_t mapped_bytes)
{
    return tu104_dma_geometry_suitable(address, mapped_bytes) &&
           address >= TU104_DMA32_LIMIT;
}

static inline uint64_t
tu104_high_dma_candidate(uint64_t range_start, uint64_t range_end)
{
    uint64_t usable_start;
    uint64_t usable_end;
    uint64_t candidate;

    if ((range_start & (TU104_DMA_PAGE_BYTES - 1)) != 0 ||
        (range_end & (TU104_DMA_PAGE_BYTES - 1)) != 0 ||
        range_end <= range_start)
        return 0;
    usable_start = range_start < TU104_DMA32_LIMIT ?
        TU104_DMA32_LIMIT : range_start;
    usable_end = range_end > TU104_DMA_LIMIT ? TU104_DMA_LIMIT : range_end;
    if (usable_end <= usable_start ||
        usable_end - usable_start < TU104_DMA_PROBE_BYTES)
        return 0;
    candidate = (usable_end - TU104_DMA_PROBE_BYTES) &
        ~(TU104_DMA_PAGE_BYTES - 1);
    return candidate >= usable_start ? candidate : 0;
}

static inline int
tu104_dma32_geometry_suitable(uint64_t address, uint64_t mapped_bytes)
{
    return mapped_bytes == TU104_DMA_PROBE_BYTES &&
           address != 0 &&
           (address & (TU104_DMA_PAGE_BYTES - 1)) == 0 &&
           address <= TU104_DMA32_LIMIT - TU104_DMA_PROBE_BYTES;
}

static inline uint32_t tu104_dma_pattern_word(size_t index)
{
    return UINT32_C(0x6d617000) ^ (uint32_t)index ^
           ((uint32_t)index * UINT32_C(0x9e3779b1));
}

#endif
