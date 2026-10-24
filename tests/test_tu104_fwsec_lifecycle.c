#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "tu104_fwsec_lifecycle.h"

int main(void)
{
    const uint64_t observed_range_start = UINT64_C(0x100000000);
    const uint64_t observed_range_end = UINT64_C(0x86e000000);
    const uint64_t expected =
        observed_range_end - TU104_FWSEC_ALLOCATION_BYTES;
    unsigned checks = 0;

    assert(TU104_FWSEC_ALLOCATION_BYTES == UINT64_C(0xa000)); checks++;
    assert(TU104_FWSEC_ALLOCATION_PAGES == UINT64_C(10)); checks++;
    assert(TU104_FWSEC_OBSERVED_HIGH_BASE == UINT64_C(0x86c000000));
    checks++;
    assert(tu104_fwsec_allocation_suitable(
        TU104_FWSEC_OBSERVED_HIGH_BASE, TU104_FWSEC_ALLOCATION_BYTES));
    checks++;
    assert(tu104_fwsec_high_candidate(observed_range_start,
                                      observed_range_end) == expected);
    checks++;
    assert(tu104_fwsec_allocation_suitable(
        expected, TU104_FWSEC_ALLOCATION_BYTES)); checks++;
    assert(tu104_fwsec_high_candidate(
        0, TU104_FWSEC_DMA32_LIMIT + TU104_FWSEC_ALLOCATION_BYTES) ==
        TU104_FWSEC_DMA32_LIMIT); checks++;
    assert(tu104_fwsec_high_candidate(
        TU104_FWSEC_DMA32_LIMIT,
        TU104_FWSEC_DMA32_LIMIT + TU104_FWSEC_ALLOCATION_BYTES -
            TU104_FWSEC_PAGE_BYTES) == 0); checks++;
    assert(tu104_fwsec_high_candidate(
        TU104_FWSEC_DMA32_LIMIT + 1,
        TU104_FWSEC_DMA32_LIMIT + TU104_FWSEC_ALLOCATION_BYTES + 1) == 0);
    checks++;
    assert(tu104_fwsec_high_candidate(
        TU104_FWSEC_PLATFORM_PHYS_LIMIT - TU104_FWSEC_PAGE_BYTES,
        TU104_FWSEC_PLATFORM_PHYS_LIMIT + TU104_FWSEC_PAGE_BYTES) == 0);
    checks++;
    assert(!tu104_fwsec_allocation_suitable(
        expected + 1, TU104_FWSEC_ALLOCATION_BYTES)); checks++;
    assert(!tu104_fwsec_allocation_suitable(
        expected, TU104_FWSEC_STAGING_BYTES)); checks++;
    assert(!tu104_fwsec_allocation_suitable(
        TU104_FWSEC_DMA32_LIMIT - TU104_FWSEC_PAGE_BYTES,
        TU104_FWSEC_ALLOCATION_BYTES)); checks++;
    assert(!tu104_fwsec_allocation_suitable(
        TU104_FWSEC_PLATFORM_PHYS_LIMIT -
            TU104_FWSEC_ALLOCATION_BYTES + TU104_FWSEC_PAGE_BYTES,
        TU104_FWSEC_ALLOCATION_BYTES)); checks++;

    assert(tu104_fwsec_page_disposition(0) ==
           TU104_FWSEC_RELEASE_AND_RETURN); checks++;
    assert(tu104_fwsec_page_disposition(1) ==
           TU104_FWSEC_RETAIN_AND_HALT); checks++;
    assert(tu104_fwsec_page_disposition(-1) ==
           TU104_FWSEC_RETAIN_AND_HALT); checks++;

    printf("PASS: %u TU104 FWSEC allocation/lifecycle checks\n", checks);
    return 0;
}
