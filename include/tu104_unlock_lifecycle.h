#ifndef TU104_UNLOCK_LIFECYCLE_H
#define TU104_UNLOCK_LIFECYCLE_H

/*
 * Platform-free ownership policy for the volatile unlock's 32 MiB pool.
 *
 * A UEFI wrapper may release the allocation before any GPU write.  After the
 * first write, release and OS handoff are permitted only after the complete
 * one-shot core reports the exact success tuple and both Falcons are proven
 * quiescent.  Every incomplete or uncertain post-write state retains the
 * pool and halts in firmware so a device cannot DMA into reused pages.
 */

#include <stddef.h>
#include <stdint.h>

#include "tu104_unlock_core.h"

#define TU104_UNLOCK_ALLOCATION_BYTES TU104_UNLOCK_POOL_BYTES
#define TU104_UNLOCK_ALLOCATION_PAGES \
    (TU104_UNLOCK_ALLOCATION_BYTES / TU104_UNLOCK_PAGE_BYTES)

/* Exact 32 MiB AllocateAddress result field-validated by 0.6.2. */
#define TU104_UNLOCK_OBSERVED_HIGH_BASE UINT64_C(0x000000086c000000)

enum tu104_unlock_lifecycle_action {
    TU104_UNLOCK_RETURN_WITHOUT_ALLOCATION = 0,
    TU104_UNLOCK_RELEASE_AND_RETURN = 1,
    TU104_UNLOCK_RELEASE_AND_HANDOFF = 2,
    TU104_UNLOCK_RETAIN_AND_HALT = 3
};

struct tu104_unlock_lifecycle_state {
    uint64_t allocation_physical;
    size_t allocation_bytes;
    int allocation_owned;
    int handoff_preloaded;
    int mutated;
    int run_status;
    unsigned stage;
    int unlocked;
    int quiescent;
};

_Static_assert(TU104_UNLOCK_ALLOCATION_BYTES % TU104_UNLOCK_PAGE_BYTES == 0,
               "unlock allocation must contain whole EFI pages");
_Static_assert(TU104_UNLOCK_ALLOCATION_PAGES == UINT64_C(8192),
               "unlock allocation must remain exactly 32 MiB");

/* Ranges are page-aligned, half-open [start, end) EFI memory descriptors. */
static inline uint64_t
tu104_unlock_high_candidate(uint64_t range_start, uint64_t range_end)
{
    uint64_t usable_start;
    uint64_t usable_end;
    uint64_t candidate;

    if ((range_start & (TU104_UNLOCK_PAGE_BYTES - 1)) != 0 ||
        (range_end & (TU104_UNLOCK_PAGE_BYTES - 1)) != 0 ||
        range_end <= range_start)
        return 0;

    usable_start = range_start < TU104_UNLOCK_PHYS_MIN ?
        TU104_UNLOCK_PHYS_MIN : range_start;
    usable_end = range_end > TU104_UNLOCK_PHYS_LIMIT ?
        TU104_UNLOCK_PHYS_LIMIT : range_end;
    if (usable_end <= usable_start ||
        usable_end - usable_start < TU104_UNLOCK_ALLOCATION_BYTES)
        return 0;

    candidate = (usable_end - TU104_UNLOCK_ALLOCATION_BYTES) &
        ~(TU104_UNLOCK_PAGE_BYTES - 1);
    return candidate >= usable_start ? candidate : 0;
}

static inline int
tu104_unlock_allocation_suitable(uint64_t physical, size_t bytes)
{
    return bytes == (size_t)TU104_UNLOCK_ALLOCATION_BYTES &&
           tu104_unlock_pool_geometry_suitable(physical, bytes);
}

static inline enum tu104_unlock_lifecycle_action
tu104_unlock_lifecycle_decide(
    const struct tu104_unlock_lifecycle_state *state)
{
    if (state == NULL)
        return TU104_UNLOCK_RETAIN_AND_HALT;

    if (!state->allocation_owned) {
        return state->mutated ? TU104_UNLOCK_RETAIN_AND_HALT :
            TU104_UNLOCK_RETURN_WITHOUT_ALLOCATION;
    }

    if (!tu104_unlock_allocation_suitable(state->allocation_physical,
                                           state->allocation_bytes))
        return TU104_UNLOCK_RETAIN_AND_HALT;

    if (!state->mutated)
        return TU104_UNLOCK_RELEASE_AND_RETURN;

    if (state->handoff_preloaded &&
        state->run_status == TU104_UNLOCK_OK &&
        state->stage == TU104_UNLOCK_STAGE_DONE &&
        state->unlocked && state->quiescent)
        return TU104_UNLOCK_RELEASE_AND_HANDOFF;

    return TU104_UNLOCK_RETAIN_AND_HALT;
}

#endif
