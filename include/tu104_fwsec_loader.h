#ifndef TU104_FWSEC_LOADER_H
#define TU104_FWSEC_LOADER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Target-pinned production FWSEC geometry from the captured TU104 VBIOS.
 * NVIDIA 610.57.04 rounds each allocation up to 256 bytes, but places the
 * descriptor's unrounded DMEMLoadSize in dataSize.
 */
#define TU104_FWSEC_DMA_ALIGNMENT        UINT64_C(0x00000100)
#define TU104_FWSEC_DMA32_LIMIT          UINT64_C(0x100000000)
#define TU104_FWSEC_PLATFORM_PHYS_LIMIT  UINT64_C(0x8000000000)

#define TU104_FWSEC_IMEM_BYTES           UINT64_C(0x00009a00)
#define TU104_FWSEC_NONSECURE_OFFSET     UINT32_C(0x00000000)
#define TU104_FWSEC_NONSECURE_BYTES      UINT32_C(0x00000400)
#define TU104_FWSEC_SECURE_OFFSET        UINT32_C(0x00000400)
#define TU104_FWSEC_SECURE_BYTES         UINT32_C(0x00009600)
#define TU104_FWSEC_DMEM_BYTES           UINT32_C(0x000003f0)
#define TU104_FWSEC_DMEM_BACKING_BYTES   UINT64_C(0x00000400)
#define TU104_FWSEC_STAGING_BYTES \
    (TU104_FWSEC_IMEM_BYTES + TU104_FWSEC_DMEM_BACKING_BYTES)
#define TU104_FWSEC_LOADER_CTX_DMA       UINT32_C(4)

/* Exact RM_FLCN_BL_DMEM_DESC wire layout used by the generic Falcon loader. */
struct tu104_fwsec_loader_desc {
    uint32_t reserved[4];
    uint32_t signature[4];
    uint32_t ctx_dma;
    uint32_t code_dma_base_lo;
    uint32_t code_dma_base_hi;
    uint32_t nonsecure_code_offset;
    uint32_t nonsecure_code_bytes;
    uint32_t secure_code_offset;
    uint32_t secure_code_bytes;
    uint32_t code_entry_point;
    uint32_t data_dma_base_lo;
    uint32_t data_dma_base_hi;
    uint32_t data_bytes;
    uint32_t argc;
    uint32_t argv;
};

_Static_assert(sizeof(struct tu104_fwsec_loader_desc) == 0x54,
               "unexpected Falcon loader descriptor size");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, signature) == 0x10,
               "unexpected signature offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, ctx_dma) == 0x20,
               "unexpected ctxDma offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, code_dma_base_lo) ==
                   0x24,
               "unexpected codeDmaBase offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc,
                        nonsecure_code_offset) == 0x2c,
               "unexpected nonSecureCodeOff offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc,
                        secure_code_offset) == 0x34,
               "unexpected secureCodeOff offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, code_entry_point) ==
                   0x3c,
               "unexpected codeEntryPoint offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, data_dma_base_lo) ==
                   0x40,
               "unexpected dataDmaBase offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, data_bytes) == 0x48,
               "unexpected dataSize offset");
_Static_assert(offsetof(struct tu104_fwsec_loader_desc, argv) == 0x50,
               "unexpected argv offset");

static inline int
tu104_fwsec_staging_geometry_suitable(uint64_t physical_base,
                                      uint64_t backing_bytes)
{
    return backing_bytes == TU104_FWSEC_STAGING_BYTES &&
           physical_base >= TU104_FWSEC_DMA32_LIMIT &&
           (physical_base & (TU104_FWSEC_DMA_ALIGNMENT - 1)) == 0 &&
           physical_base <=
               TU104_FWSEC_PLATFORM_PHYS_LIMIT - TU104_FWSEC_STAGING_BYTES;
}

/*
 * Model the future contiguous staging layout only.  This function writes
 * ordinary host memory; it performs no allocation, DMA, MMIO, or GPU action.
 */
static inline int
tu104_fwsec_loader_desc_init_contiguous(
    struct tu104_fwsec_loader_desc *desc,
    uint64_t physical_base,
    uint64_t backing_bytes)
{
    struct tu104_fwsec_loader_desc value = {0};
    uint64_t data_base;

    if (desc == NULL ||
        !tu104_fwsec_staging_geometry_suitable(physical_base, backing_bytes))
        return 0;

    data_base = physical_base + TU104_FWSEC_IMEM_BYTES;
    value.ctx_dma = TU104_FWSEC_LOADER_CTX_DMA;
    value.code_dma_base_lo = (uint32_t)physical_base;
    value.code_dma_base_hi = (uint32_t)(physical_base >> 32);
    value.nonsecure_code_offset = TU104_FWSEC_NONSECURE_OFFSET;
    value.nonsecure_code_bytes = TU104_FWSEC_NONSECURE_BYTES;
    value.secure_code_offset = TU104_FWSEC_SECURE_OFFSET;
    value.secure_code_bytes = TU104_FWSEC_SECURE_BYTES;
    value.code_entry_point = 0;
    value.data_dma_base_lo = (uint32_t)data_base;
    value.data_dma_base_hi = (uint32_t)(data_base >> 32);
    value.data_bytes = TU104_FWSEC_DMEM_BYTES;
    value.argc = 0;
    value.argv = 0;
    *desc = value;
    return 1;
}

#endif
