#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tu104_falcon_profile.h"

int main(void)
{
    uint32_t gsp[TU104_FALCON_FIELD_COUNT];
    uint32_t sec2[TU104_FALCON_FIELD_COUNT];
    size_t index;
    unsigned checks = 0;

    memcpy(gsp, tu104_gsp_initial_values, sizeof(gsp));
    memcpy(sec2, tu104_sec2_initial_values, sizeof(sec2));
    assert(tu104_falcon_initial_pair_matches(gsp, sec2)); checks++;
    assert(!tu104_falcon_initial_pair_matches(NULL, sec2)); checks++;
    assert(!tu104_falcon_initial_pair_matches(gsp, NULL)); checks++;

    for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
        gsp[index] ^= UINT32_C(1);
        assert(!tu104_falcon_initial_pair_matches(gsp, sec2)); checks++;
        gsp[index] ^= UINT32_C(1);
        sec2[index] ^= UINT32_C(1);
        assert(!tu104_falcon_initial_pair_matches(gsp, sec2)); checks++;
        sec2[index] ^= UINT32_C(1);
    }

    assert(tu104_falcon_imem_blocks(UINT32_C(0x40420100)) ==
           TU104_FALCON_EXPECTED_IMEM_BLOCKS); checks++;
    assert(tu104_generic_bl_imem_destination(UINT32_C(0x40420100)) ==
           TU104_GENERIC_BL_EXPECTED_DEST); checks++;
    assert(tu104_generic_bl_imem_destination(UINT32_C(1)) == UINT32_MAX);
    checks++;
    assert(TU104_GENERIC_BL_BOOT_VECTOR == UINT32_C(0xfd00)); checks++;
    assert(TU104_GENERIC_BL_IMAGE_BYTES ==
           TU104_GENERIC_BL_CODE_BYTES + TU104_GENERIC_BL_DATA_BYTES);
    checks++;
    assert(tu104_falcon_loader_transcfg(UINT32_C(0x110)) ==
           UINT32_C(0x115)); checks++;
    assert(tu104_falcon_loader_transcfg(UINT32_C(0xffffffff)) ==
           UINT32_C(0xfffffffd)); checks++;
    assert(tu104_falcon_allow_physical_no_context(UINT32_C(0x110)) ==
           UINT32_C(0x190)); checks++;
    assert(tu104_falcon_allow_physical_no_context(UINT32_C(0x190)) ==
           UINT32_C(0x190)); checks++;

    printf("PASS: %u TU104 Falcon-profile checks\n", checks);
    return 0;
}
