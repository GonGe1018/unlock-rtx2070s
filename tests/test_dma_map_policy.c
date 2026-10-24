#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "dma_map_policy.h"

int main(void)
{
    assert(TU104_DMA_PROBE_BYTES == UINT64_C(32) * 1024 * 1024);
    assert(tu104_dma_geometry_suitable(UINT64_C(0x800000000),
                                       TU104_DMA_PROBE_BYTES));
    assert(tu104_dma_geometry_suitable(UINT64_C(0x100000000),
                                       TU104_DMA_PROBE_BYTES));
    assert(tu104_dma_geometry_suitable(
        TU104_DMA_LIMIT - TU104_DMA_PROBE_BYTES,
        TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma_geometry_suitable(0, TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma_geometry_suitable(UINT64_C(0x800000001),
                                        TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma_geometry_suitable(UINT64_C(0x800000000),
                                        TU104_DMA_PROBE_BYTES - 4096));
    assert(!tu104_dma_geometry_suitable(
        TU104_DMA_LIMIT - TU104_DMA_PROBE_BYTES + 4096,
        TU104_DMA_PROBE_BYTES));
    assert(tu104_dma64_geometry_suitable(UINT64_C(0x100000000),
                                         TU104_DMA_PROBE_BYTES));
    assert(tu104_dma64_geometry_suitable(UINT64_C(0x800000000),
                                         TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma64_geometry_suitable(UINT64_C(0x80000000),
                                          TU104_DMA_PROBE_BYTES));
    assert(tu104_high_dma_candidate(UINT64_C(0x100000000),
                                    UINT64_C(0x120000000)) ==
           UINT64_C(0x11e000000));
    assert(tu104_high_dma_candidate(UINT64_C(0xff00000000),
                                    UINT64_C(0x10100000000)) ==
           UINT64_C(0xfffe000000));
    assert(tu104_high_dma_candidate(UINT64_C(0xfff00000),
                                    UINT64_C(0x101000000)) == 0);
    assert(tu104_high_dma_candidate(UINT64_C(0x100000001),
                                    UINT64_C(0x120000000)) == 0);
    assert(tu104_high_dma_candidate(UINT64_C(0x120000000),
                                    UINT64_C(0x100000000)) == 0);
    assert(tu104_dma32_geometry_suitable(UINT64_C(0x80000000),
                                         TU104_DMA_PROBE_BYTES));
    assert(tu104_dma32_geometry_suitable(
        TU104_DMA32_LIMIT - TU104_DMA_PROBE_BYTES,
        TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma32_geometry_suitable(UINT64_C(0x100000000),
                                          TU104_DMA_PROBE_BYTES));
    assert(!tu104_dma32_geometry_suitable(UINT64_C(0x80000001),
                                          TU104_DMA_PROBE_BYTES));
    assert(tu104_dma_pattern_word(0) == UINT32_C(0x6d617000));
    assert(tu104_dma_pattern_word(1) == UINT32_C(0xf35609b0));
    assert(tu104_dma_pattern_word(0x100000) == UINT32_C(0xf6617000));

    puts("DMA map policy tests: 23 checks passed");
    return 0;
}
