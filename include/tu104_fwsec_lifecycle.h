#ifndef TU104_FWSEC_LIFECYCLE_H
#define TU104_FWSEC_LIFECYCLE_H

#include <stdint.h>

#include "tu104_fwsec_loader.h"

#define TU104_FWSEC_PAGE_BYTES UINT64_C(0x1000)
#define TU104_FWSEC_ALLOCATION_BYTES \
    ((TU104_FWSEC_STAGING_BYTES + TU104_FWSEC_PAGE_BYTES - 1) & \
     ~(TU104_FWSEC_PAGE_BYTES - 1))
#define TU104_FWSEC_ALLOCATION_PAGES \
    (TU104_FWSEC_ALLOCATION_BYTES / TU104_FWSEC_PAGE_BYTES)

/* Exact address successfully claimed as conventional WB memory by 0.6.2. */
#define TU104_FWSEC_OBSERVED_HIGH_BASE UINT64_C(0x000000086c000000)

enum tu104_fwsec_page_disposition {
    TU104_FWSEC_RELEASE_AND_RETURN = 0,
    TU104_FWSEC_RETAIN_AND_HALT = 1
};

static inline uint64_t
tu104_fwsec_high_candidate(uint64_t range_start, uint64_t range_end)
{
    uint64_t usable_start;
    uint64_t usable_end;
    uint64_t candidate;

    if ((range_start & (TU104_FWSEC_PAGE_BYTES - 1)) != 0 ||
        (range_end & (TU104_FWSEC_PAGE_BYTES - 1)) != 0 ||
        range_end <= range_start)
        return 0;
    usable_start = range_start < TU104_FWSEC_DMA32_LIMIT ?
        TU104_FWSEC_DMA32_LIMIT : range_start;
    usable_end = range_end > TU104_FWSEC_PLATFORM_PHYS_LIMIT ?
        TU104_FWSEC_PLATFORM_PHYS_LIMIT : range_end;
    if (usable_end <= usable_start ||
        usable_end - usable_start < TU104_FWSEC_ALLOCATION_BYTES)
        return 0;
    candidate = (usable_end - TU104_FWSEC_ALLOCATION_BYTES) &
        ~(TU104_FWSEC_PAGE_BYTES - 1);
    return candidate >= usable_start ? candidate : 0;
}

static inline int
tu104_fwsec_allocation_suitable(uint64_t physical, uint64_t bytes)
{
    return bytes == TU104_FWSEC_ALLOCATION_BYTES &&
        (physical & (TU104_FWSEC_PAGE_BYTES - 1)) == 0 &&
        physical >= TU104_FWSEC_DMA32_LIMIT &&
        physical <= TU104_FWSEC_PLATFORM_PHYS_LIMIT - bytes &&
        tu104_fwsec_staging_geometry_suitable(
            physical, TU104_FWSEC_STAGING_BYTES);
}

static inline enum tu104_fwsec_page_disposition
tu104_fwsec_page_disposition(int mutated)
{
    return mutated ? TU104_FWSEC_RETAIN_AND_HALT :
        TU104_FWSEC_RELEASE_AND_RETURN;
}

#endif
