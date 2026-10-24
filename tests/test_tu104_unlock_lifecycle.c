#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tu104_unlock_lifecycle.h"

static unsigned cases;

static void test_allocation_geometry(void)
{
    const uint64_t observed_range_start = UINT64_C(0x100000000);
    const uint64_t observed_range_end = UINT64_C(0x86e000000);

    assert(TU104_UNLOCK_ALLOCATION_BYTES == UINT64_C(0x02000000));
    assert(TU104_UNLOCK_ALLOCATION_PAGES == UINT64_C(8192));
    assert(TU104_UNLOCK_OBSERVED_HIGH_BASE == UINT64_C(0x86c000000));
    assert(tu104_unlock_high_candidate(observed_range_start,
                                       observed_range_end) ==
           TU104_UNLOCK_OBSERVED_HIGH_BASE);
    assert(tu104_unlock_allocation_suitable(
        TU104_UNLOCK_OBSERVED_HIGH_BASE, TU104_UNLOCK_ALLOCATION_BYTES));
    ++cases;

    assert(tu104_unlock_high_candidate(
        0, TU104_UNLOCK_PHYS_MIN + TU104_UNLOCK_ALLOCATION_BYTES) ==
        TU104_UNLOCK_PHYS_MIN);
    assert(tu104_unlock_high_candidate(
        TU104_UNLOCK_PHYS_MIN,
        TU104_UNLOCK_PHYS_MIN + TU104_UNLOCK_ALLOCATION_BYTES -
            TU104_UNLOCK_PAGE_BYTES) == 0);
    assert(tu104_unlock_high_candidate(
        TU104_UNLOCK_PHYS_MIN + 1,
        TU104_UNLOCK_PHYS_MIN + TU104_UNLOCK_ALLOCATION_BYTES + 1) == 0);
    assert(tu104_unlock_high_candidate(
        TU104_UNLOCK_PHYS_LIMIT - TU104_UNLOCK_ALLOCATION_BYTES,
        UINT64_MAX & ~(TU104_UNLOCK_PAGE_BYTES - 1)) ==
        TU104_UNLOCK_PHYS_LIMIT - TU104_UNLOCK_ALLOCATION_BYTES);
    assert(!tu104_unlock_allocation_suitable(
        TU104_UNLOCK_OBSERVED_HIGH_BASE + 1,
        TU104_UNLOCK_ALLOCATION_BYTES));
    assert(!tu104_unlock_allocation_suitable(
        TU104_UNLOCK_OBSERVED_HIGH_BASE,
        TU104_UNLOCK_ALLOCATION_BYTES - TU104_UNLOCK_PAGE_BYTES));
    assert(!tu104_unlock_allocation_suitable(
        TU104_UNLOCK_PHYS_MIN - TU104_UNLOCK_PAGE_BYTES,
        TU104_UNLOCK_ALLOCATION_BYTES));
    assert(!tu104_unlock_allocation_suitable(
        TU104_UNLOCK_PHYS_LIMIT - TU104_UNLOCK_ALLOCATION_BYTES +
            TU104_UNLOCK_PAGE_BYTES,
        TU104_UNLOCK_ALLOCATION_BYTES));
    ++cases;
}

static enum tu104_unlock_lifecycle_action expected_action(
    int owned, int valid_geometry, int preloaded, int mutated,
    int run_ok, int done, int unlocked, int quiescent)
{
    if (!owned)
        return mutated ? TU104_UNLOCK_RETAIN_AND_HALT :
            TU104_UNLOCK_RETURN_WITHOUT_ALLOCATION;
    if (!valid_geometry)
        return TU104_UNLOCK_RETAIN_AND_HALT;
    if (!mutated)
        return TU104_UNLOCK_RELEASE_AND_RETURN;
    if (preloaded && run_ok && done && unlocked && quiescent)
        return TU104_UNLOCK_RELEASE_AND_HANDOFF;
    return TU104_UNLOCK_RETAIN_AND_HALT;
}

static void test_exhaustive_disposition(void)
{
    unsigned mask;

    assert(tu104_unlock_lifecycle_decide(NULL) ==
           TU104_UNLOCK_RETAIN_AND_HALT);
    ++cases;

    for (mask = 0; mask < 256; ++mask) {
        struct tu104_unlock_lifecycle_state state;
        int owned = !!(mask & (1u << 0));
        int valid_geometry = !!(mask & (1u << 1));
        int preloaded = !!(mask & (1u << 2));
        int mutated = !!(mask & (1u << 3));
        int run_ok = !!(mask & (1u << 4));
        int done = !!(mask & (1u << 5));
        int unlocked = !!(mask & (1u << 6));
        int quiescent = !!(mask & (1u << 7));

        memset(&state, 0, sizeof(state));
        state.allocation_owned = owned;
        state.allocation_physical = valid_geometry ?
            TU104_UNLOCK_OBSERVED_HIGH_BASE :
            TU104_UNLOCK_OBSERVED_HIGH_BASE + 1;
        state.allocation_bytes = TU104_UNLOCK_ALLOCATION_BYTES;
        state.handoff_preloaded = preloaded;
        state.mutated = mutated;
        state.run_status = run_ok ? TU104_UNLOCK_OK : TU104_UNLOCK_VERIFY;
        state.stage = done ? TU104_UNLOCK_STAGE_DONE :
            TU104_UNLOCK_STAGE_BOOTER_RESULT;
        state.unlocked = unlocked;
        state.quiescent = quiescent;

        assert(tu104_unlock_lifecycle_decide(&state) ==
               expected_action(owned, valid_geometry, preloaded, mutated,
                               run_ok, done, unlocked, quiescent));
        ++cases;
    }
}

int main(void)
{
    test_allocation_geometry();
    test_exhaustive_disposition();
    printf("PASS: %u TU104 full-unlock allocation/lifecycle cases; "
           "32-MiB exact ownership and fail-closed post-write retention "
           "verified; no hardware access.\n", cases);
    return 0;
}
