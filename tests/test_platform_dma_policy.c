#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform_dma_policy.h"

int main(void)
{
    uint8_t setup[TU104_SETUP_BYTES];
    unsigned checks = 0;

    memset(setup, 0, sizeof(setup));
    setup[TU104_SETUP_MEMORY_REMAP_OFFSET] = 1;
    setup[TU104_SETUP_CSM_OFFSET] = 1;
    assert(tu104_setup_dma_profile_matches(setup, sizeof(setup))); checks++;
    assert(!tu104_setup_dma_profile_matches(NULL, sizeof(setup))); checks++;
    assert(!tu104_setup_dma_profile_matches(setup, sizeof(setup) - 1)); checks++;
    setup[TU104_SETUP_VTD_OFFSET] = 1;
    assert(!tu104_setup_dma_profile_matches(setup, sizeof(setup))); checks++;
    setup[TU104_SETUP_VTD_OFFSET] = 0;
    setup[TU104_SETUP_ABOVE4G_MINING_OFFSET] = 1;
    assert(!tu104_setup_dma_profile_matches(setup, sizeof(setup))); checks++;

    assert(tu104_mchbar_address(UINT32_C(0xfed10001), 0) ==
           TU104_EXPECTED_MCHBAR); checks++;
    assert(tu104_mchbar_profile_matches(UINT32_C(0xfed10001), 0)); checks++;
    assert(!tu104_mchbar_profile_matches(UINT32_C(0xfed10000), 0)); checks++;
    assert(!tu104_mchbar_profile_matches(UINT32_C(0xfed18001), 0)); checks++;
    assert(!tu104_mchbar_profile_matches(UINT32_C(0xfed10001), 1)); checks++;

    assert(tu104_vtd2_address(UINT32_C(0xfed91001)) ==
           TU104_EXPECTED_VTD2_BASE); checks++;
    assert(tu104_vtd2_profile_matches(UINT32_C(0xfed91001))); checks++;
    assert(tu104_vtd2_profile_matches(UINT32_C(0xfed91000))); checks++;
    assert(tu104_vtd2_enabled(UINT32_C(0xfed91001))); checks++;
    assert(!tu104_vtd2_enabled(UINT32_C(0xfed91000))); checks++;
    assert(!tu104_vtd2_profile_matches(UINT32_C(0xfed90001))); checks++;

    assert(tu104_vtd_has_protected_memory(UINT64_C(0x20))); checks++;
    assert(tu104_vtd_has_protected_memory(UINT64_C(0x40))); checks++;
    assert(!tu104_vtd_has_protected_memory(0)); checks++;
    assert(tu104_vtd_translation_enabled(UINT32_C(0x80000000))); checks++;
    assert(!tu104_vtd_translation_enabled(0)); checks++;
    assert(tu104_vtd_protected_memory_active(UINT32_C(0x80000000))); checks++;
    assert(tu104_vtd_protected_memory_active(1)); checks++;
    assert(!tu104_vtd_protected_memory_active(0)); checks++;

    assert(tu104_vtd_observation_clear(UINT32_C(0x10), 0, 0, 0, 0)); checks++;
    assert(tu104_vtd_observation_clear(UINT32_C(0x10), UINT64_C(0x60),
                                       0, 0, 1)); checks++;
    assert(!tu104_vtd_observation_clear(UINT32_C(0x11), 0, 0, 0, 0)); checks++;
    assert(!tu104_vtd_observation_clear(UINT32_C(0x10), 0,
                                        UINT32_C(0x80000000), 0, 0)); checks++;
    assert(!tu104_vtd_observation_clear(UINT32_C(0x10), UINT64_C(0x20),
                                        0, 0, 0)); checks++;
    assert(!tu104_vtd_observation_clear(UINT32_C(0x10), UINT64_C(0x20),
                                        0, UINT32_C(0x80000000), 1)); checks++;

    printf("PASS: %u platform-DMA policy checks\n", checks);
    return 0;
}
