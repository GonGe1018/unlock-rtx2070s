#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tu104_unlock_layout.h"

static uint8_t booter[TU104_UNLOCK_BOOTER_BYTES];
static uint8_t signature[TU104_UNLOCK_BOOTER_SIGNATURE_BYTES];
static uint8_t v67[TU104_UNLOCK_V67_BYTES];
static uint8_t boot_container[TU104_UNLOCK_BOOT_CONTAINER_BYTES];
static unsigned cases;

static void load_exact(const char *path, uint8_t *output, size_t bytes)
{
    FILE *file = fopen(path, "rb");

    assert(file != NULL);
    assert(fread(output, 1, bytes, file) == bytes);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

static void setup(struct tu104_unlock_layout_input *input, uint8_t *pool)
{
    memset(input, 0, sizeof(*input));
    memset(pool, 0xa5, TU104_UNLOCK_POOL_BYTES);
    input->pool = pool;
    input->pool_bytes = TU104_UNLOCK_POOL_BYTES;
    input->pool_physical = UINT64_C(0x86c000000);
    input->booter = booter;
    input->booter_bytes = sizeof(booter);
    input->booter_signature = signature;
    input->booter_signature_bytes = sizeof(signature);
    input->v67 = v67;
    input->v67_bytes = sizeof(v67);
    input->boot_container = boot_container;
    input->boot_container_bytes = sizeof(boot_container);
}

static void test_exact_layout(uint8_t *pool)
{
    struct tu104_unlock_layout_input input;
    const uint8_t *meta;
    const uint8_t *radix;
    uint64_t last_page;
    size_t index;

    setup(&input, pool);
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_OK);
    meta = pool + TU104_UNLOCK_META_OFFSET;
    radix = pool + TU104_UNLOCK_RADIX_OFFSET;

    assert(tu104_unlock_layout_le64(meta + 0 * 8) ==
           TU104_UNLOCK_META_MAGIC);
    assert(tu104_unlock_layout_le64(meta + 1 * 8) == 1);
    assert(tu104_unlock_layout_le64(meta + 2 * 8) ==
           input.pool_physical + TU104_UNLOCK_RADIX_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 3 * 8) ==
           TU104_UNLOCK_DUMMY_ELF_BYTES);
    assert(tu104_unlock_layout_le64(meta + 4 * 8) ==
           input.pool_physical + TU104_UNLOCK_BOOT_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 5 * 8) ==
           TU104_UNLOCK_BOOT_IMAGE_BYTES);
    assert(tu104_unlock_layout_le64(meta + 6 * 8) == UINT64_C(0x1800));
    assert(tu104_unlock_layout_le64(meta + 7 * 8) == UINT64_C(0x800));
    assert(tu104_unlock_layout_le64(meta + 8 * 8) == 0);
    assert(tu104_unlock_layout_le64(meta + 9 * 8) ==
           input.pool_physical + TU104_UNLOCK_V67_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 10 * 8) ==
           TU104_UNLOCK_V67_BYTES);
    assert(tu104_unlock_layout_le64(meta + 11 * 8) ==
           UINT64_C(0x1f7700000));
    assert(tu104_unlock_layout_le64(meta + 12 * 8) ==
           UINT64_C(0x1f7700000));
    assert(tu104_unlock_layout_le64(meta + 13 * 8) ==
           UINT64_C(0x100000));
    assert(tu104_unlock_layout_le64(meta + 14 * 8) ==
           UINT64_C(0x1f7800000));
    assert(tu104_unlock_layout_le64(meta + 15 * 8) ==
           UINT64_C(0x1f7900000));
    assert(tu104_unlock_layout_le64(meta + 16 * 8) ==
           TU104_UNLOCK_HEAP_BYTES);
    assert(tu104_unlock_layout_le64(meta + 17 * 8) ==
           UINT64_C(0x1fe2d0000));
    assert(tu104_unlock_layout_le64(meta + 18 * 8) ==
           UINT64_C(0x1ffeda000));
    assert(tu104_unlock_layout_le64(meta + 19 * 8) ==
           TU104_UNLOCK_FRTS_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 20 * 8) ==
           TU104_UNLOCK_FRTS_BYTES);
    assert(tu104_unlock_layout_le64(meta + 21 * 8) ==
           TU104_UNLOCK_VGA_WORKSPACE_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 22 * 8) ==
           TU104_UNLOCK_FB_BYTES);
    assert(tu104_unlock_layout_le64(meta + 23 * 8) ==
           TU104_UNLOCK_VGA_WORKSPACE_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 24 * 8) ==
           TU104_UNLOCK_VGA_WORKSPACE_BYTES);
    assert(meta[TU104_UNLOCK_META_FLAGS_OFFSET] == 1);
    assert(tu104_unlock_layout_le64(meta + 248) == 0);

    assert(tu104_unlock_layout_le64(radix) ==
           input.pool_physical + TU104_UNLOCK_RADIX_OFFSET + 0x1000);
    for (index = 0; index < TU104_UNLOCK_RADIX_L2_PAGES; ++index) {
        assert(tu104_unlock_layout_le64(radix + 0x1000 + index * 8) ==
               input.pool_physical + TU104_UNLOCK_RADIX_OFFSET + 0x2000 +
                   index * 0x1000);
    }
    assert(tu104_unlock_layout_le64(radix + 0x2000) ==
           input.pool_physical + TU104_UNLOCK_DATA_OFFSET);
    last_page = tu104_unlock_layout_le64(
        radix + 0x2000 +
        (TU104_UNLOCK_DUMMY_ELF_PAGES - UINT64_C(1)) * 8);
    assert(last_page == input.pool_physical + TU104_UNLOCK_DATA_OFFSET +
                            (TU104_UNLOCK_DUMMY_ELF_PAGES - UINT64_C(1)) *
                                TU104_UNLOCK_PAGE_BYTES);
    assert(last_page + TU104_UNLOCK_PAGE_BYTES <=
           input.pool_physical + TU104_UNLOCK_POOL_BYTES);
    assert(tu104_unlock_layout_crc32(pool + TU104_UNLOCK_BOOTER_OFFSET,
                                     TU104_UNLOCK_BOOTER_BYTES) ==
           TU104_UNLOCK_PATCHED_BOOTER_CRC32);
    assert(memcmp(pool + TU104_UNLOCK_V67_OFFSET, v67, sizeof(v67)) == 0);
    assert(memcmp(pool + TU104_UNLOCK_BOOT_OFFSET, boot_container,
                  TU104_UNLOCK_BOOT_IMAGE_BYTES) == 0);
    assert(pool[TU104_UNLOCK_DATA_OFFSET] == 0);
    assert(pool[TU104_UNLOCK_POOL_BYTES - 1] == 0);
    ++cases;
}

static void test_refusals(uint8_t *pool)
{
    struct tu104_unlock_layout_input input;
    uint8_t saved;

    setup(&input, pool);
    input.pool_physical = TU104_UNLOCK_PHYS_MIN - TU104_UNLOCK_PAGE_BYTES;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    assert(pool[0] == 0xa5 && pool[TU104_UNLOCK_POOL_BYTES - 1] == 0xa5);
    ++cases;

    setup(&input, pool);
    ++input.pool_physical;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    ++cases;

    setup(&input, pool);
    --input.pool_bytes;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    ++cases;

    setup(&input, pool);
    saved = booter[0];
    booter[0] ^= 1;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    booter[0] = saved;
    ++cases;

    setup(&input, pool);
    saved = signature[0];
    signature[0] ^= 1;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    signature[0] = saved;
    ++cases;

    setup(&input, pool);
    saved = v67[0xf988];
    v67[0xf988] ^= 1;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    v67[0xf988] = saved;
    ++cases;

    setup(&input, pool);
    saved = boot_container[TU104_UNLOCK_BOOT_IMAGE_BYTES + 1];
    boot_container[TU104_UNLOCK_BOOT_IMAGE_BYTES + 1] ^= 1;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    boot_container[TU104_UNLOCK_BOOT_IMAGE_BYTES + 1] = saved;
    ++cases;

    setup(&input, pool);
    input.v67_bytes--;
    assert(tu104_unlock_layout_prepare(&input) == TU104_UNLOCK_LAYOUT_GUARD);
    ++cases;
}

int main(void)
{
    uint8_t *pool = malloc(TU104_UNLOCK_POOL_BYTES);

    assert(pool != NULL);
    load_exact("build/tu10x-assets/booter_ucode_prod.bin",
               booter, sizeof(booter));
    load_exact("build/tu10x-assets/booter_sig_prod.bin",
               signature, sizeof(signature));
    load_exact("references/xrip__cmp50hx-unlock/efi-unlock/blobs/"
               "v67_payload.bin", v67, sizeof(v67));
    load_exact("references/xrip__cmp50hx-unlock/efi-unlock/blobs/"
               "gsp_rm_boot_dbg.bin", boot_container,
               sizeof(boot_container));

    test_exact_layout(pool);
    test_refusals(pool);
    free(pool);
    printf("PASS: %u TU104 unlock-layout offline cases; exact assets, "
           "8-GiB metadata, and radix-3 bounds verified; no hardware "
           "access.\n", cases);
    return 0;
}
