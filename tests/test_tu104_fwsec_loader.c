#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tu104_fwsec_loader.h"

int main(void)
{
    const uint64_t base = UINT64_C(0x86c000000);
    const uint64_t last_base =
        TU104_FWSEC_PLATFORM_PHYS_LIMIT - TU104_FWSEC_STAGING_BYTES;
    const uint8_t expected[0x54] = {
        [0x20] = 0x04,
        [0x27] = 0x6c,
        [0x28] = 0x08,
        [0x31] = 0x04,
        [0x35] = 0x04,
        [0x39] = 0x96,
        [0x41] = 0x9a,
        [0x43] = 0x6c,
        [0x44] = 0x08,
        [0x48] = 0xf0,
        [0x49] = 0x03,
    };
    struct tu104_fwsec_loader_desc desc;
    struct tu104_fwsec_loader_desc unchanged;

    assert(sizeof(desc) == 0x54);
    assert(offsetof(struct tu104_fwsec_loader_desc, reserved) == 0x00);
    assert(offsetof(struct tu104_fwsec_loader_desc, signature) == 0x10);
    assert(offsetof(struct tu104_fwsec_loader_desc, ctx_dma) == 0x20);
    assert(offsetof(struct tu104_fwsec_loader_desc, code_dma_base_lo) == 0x24);
    assert(offsetof(struct tu104_fwsec_loader_desc, code_dma_base_hi) == 0x28);
    assert(offsetof(struct tu104_fwsec_loader_desc,
                    nonsecure_code_offset) == 0x2c);
    assert(offsetof(struct tu104_fwsec_loader_desc,
                    nonsecure_code_bytes) == 0x30);
    assert(offsetof(struct tu104_fwsec_loader_desc, secure_code_offset) == 0x34);
    assert(offsetof(struct tu104_fwsec_loader_desc, secure_code_bytes) == 0x38);
    assert(offsetof(struct tu104_fwsec_loader_desc, code_entry_point) == 0x3c);
    assert(offsetof(struct tu104_fwsec_loader_desc, data_dma_base_lo) == 0x40);
    assert(offsetof(struct tu104_fwsec_loader_desc, data_dma_base_hi) == 0x44);
    assert(offsetof(struct tu104_fwsec_loader_desc, data_bytes) == 0x48);
    assert(offsetof(struct tu104_fwsec_loader_desc, argc) == 0x4c);
    assert(offsetof(struct tu104_fwsec_loader_desc, argv) == 0x50);

    assert(TU104_FWSEC_IMEM_BYTES == UINT64_C(0x9a00));
    assert(TU104_FWSEC_DMEM_BYTES == UINT32_C(0x3f0));
    assert(TU104_FWSEC_DMEM_BACKING_BYTES == UINT64_C(0x400));
    assert(TU104_FWSEC_STAGING_BYTES == UINT64_C(0x9e00));
    assert(TU104_FWSEC_NONSECURE_BYTES + TU104_FWSEC_SECURE_BYTES ==
           TU104_FWSEC_IMEM_BYTES);

    assert(tu104_fwsec_staging_geometry_suitable(
        base, TU104_FWSEC_STAGING_BYTES));
    assert(tu104_fwsec_staging_geometry_suitable(
        TU104_FWSEC_DMA32_LIMIT, TU104_FWSEC_STAGING_BYTES));
    assert(tu104_fwsec_staging_geometry_suitable(
        last_base, TU104_FWSEC_STAGING_BYTES));
    assert(!tu104_fwsec_staging_geometry_suitable(
        TU104_FWSEC_DMA32_LIMIT - TU104_FWSEC_DMA_ALIGNMENT,
        TU104_FWSEC_STAGING_BYTES));
    assert(!tu104_fwsec_staging_geometry_suitable(
        base + 1, TU104_FWSEC_STAGING_BYTES));
    assert(!tu104_fwsec_staging_geometry_suitable(
        base, UINT64_C(0x9df0)));
    assert(!tu104_fwsec_staging_geometry_suitable(
        last_base + TU104_FWSEC_DMA_ALIGNMENT,
        TU104_FWSEC_STAGING_BYTES));

    memset(&desc, 0xa5, sizeof(desc));
    unchanged = desc;
    assert(!tu104_fwsec_loader_desc_init_contiguous(
        &desc, base + 1, TU104_FWSEC_STAGING_BYTES));
    assert(memcmp(&desc, &unchanged, sizeof(desc)) == 0);
    assert(!tu104_fwsec_loader_desc_init_contiguous(
        NULL, base, TU104_FWSEC_STAGING_BYTES));

    assert(tu104_fwsec_loader_desc_init_contiguous(
        &desc, base, TU104_FWSEC_STAGING_BYTES));
    assert(desc.ctx_dma == UINT32_C(4));
    assert(desc.code_dma_base_lo == UINT32_C(0x6c000000));
    assert(desc.code_dma_base_hi == UINT32_C(0x00000008));
    assert(desc.nonsecure_code_offset == UINT32_C(0));
    assert(desc.nonsecure_code_bytes == UINT32_C(0x400));
    assert(desc.secure_code_offset == UINT32_C(0x400));
    assert(desc.secure_code_bytes == UINT32_C(0x9600));
    assert(desc.code_entry_point == UINT32_C(0));
    assert(desc.data_dma_base_lo == UINT32_C(0x6c009a00));
    assert(desc.data_dma_base_hi == UINT32_C(0x00000008));
    assert(desc.data_bytes == UINT32_C(0x3f0));
    assert(desc.argc == UINT32_C(0));
    assert(desc.argv == UINT32_C(0));
    assert(memcmp(&desc, expected, sizeof(expected)) == 0);

    puts("TU104 FWSEC loader model tests passed");
    return 0;
}
