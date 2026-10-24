#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tu104_unlock_core.h"

#ifndef TU104_TEST_PROM_FIXTURE
#define TU104_TEST_PROM_FIXTURE \
    "artifacts/2070s-efirom/rtx2070s-gigabyte-90.04.76.00.D5-prom512k-run1.bin"
#endif

enum result_mode {
    RESULT_SUCCESS = 0,
    RESULT_FRTS_SCRATCH,
    RESULT_FRTS_WPR,
    RESULT_BOOTER_MAILBOX,
    RESULT_BOOTER_SS0,
    RESULT_BOOTER_SS1,
    RESULT_BOOTER_PLM,
    RESULT_BOOTER_WPR,
    RESULT_BOOTER_HALT
};

struct model {
    uint32_t regs[2][0x700 / 4];
    uint32_t imem[2][0x10000 / 4];
    uint32_t dmem[2][0x10000 / 4];
    uint32_t imem_control[2];
    uint32_t dmem_control[2];
    uint32_t scrub_reads[2];
    uint32_t hwcfg2_scrub_reads[2];
    const uint8_t *prom;
    struct tu104_unlock_core *core;
    uint32_t boot0;
    uint32_t plm;
    uint32_t feature_readout;
    uint32_t ss0;
    uint32_t speed_readout;
    uint32_t ss1;
    uint32_t wpr2_lo;
    uint32_t wpr2_hi;
    uint32_t gfw;
    uint32_t workspace;
    uint32_t scratch;
    unsigned reads;
    unsigned writes;
    unsigned delays;
    unsigned barriers;
    unsigned events;
    unsigned next_inputs;
    unsigned gsp_starts;
    unsigned sec2_starts;
    unsigned fail_read;
    unsigned fail_write;
    unsigned fail_delay;
    unsigned fail_barrier;
    unsigned fail_event_stage;
    unsigned fail_next_input;
    unsigned writes_at_stage[TU104_UNLOCK_STAGE_COUNT];
    enum result_mode result_mode;
    int reset_stuck[2];
    int reset_seen[2];
    int bad_imem_readback;
    int bad_dmem_readback;
};

static uint8_t prom[0x80000];
static uint8_t native_fwsec[TU104_FWSEC_STORED_BYTES];
static uint8_t generic_bl[TU104_GENERIC_BL_IMAGE_BYTES];
static uint8_t booter[TU104_UNLOCK_BOOTER_BYTES];
static uint8_t booter_signature[TU104_UNLOCK_BOOTER_SIGNATURE_BYTES];
static uint8_t v67[TU104_UNLOCK_V67_BYTES];
static uint8_t boot_container[TU104_UNLOCK_BOOT_CONTAINER_BYTES];
static uint8_t *pool;
static struct tu104_unlock_core prepared_template;
static struct tu104_unlock_core core;
static struct model model;
static unsigned cases;

static void load_exact(const char *path, uint8_t *output, size_t bytes)
{
    FILE *file = fopen(path, "rb");

    assert(file != NULL);
    assert(fread(output, 1, bytes, file) == bytes);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

static unsigned falcon_bank(uint32_t offset)
{
    uint32_t base = offset & UINT32_C(0xffff0000);

    assert(base == TU104_GSP_BASE || base == TU104_SEC2_BASE);
    return base == TU104_SEC2_BASE;
}

static int model_read32(void *opaque, uint32_t offset, uint32_t *value)
{
    struct model *current = opaque;
    uint32_t relative;
    unsigned bank;

    assert(current != NULL && value != NULL);
    ++current->reads;
    if (current->reads == current->fail_read)
        return 1;
    if (offset >= TU104_PROM_WINDOW &&
        offset < TU104_PROM_WINDOW + sizeof(prom)) {
        *value = tu104_unlock_layout_le32(
            current->prom + offset - TU104_PROM_WINDOW);
        return 0;
    }
    switch (offset) {
    case UINT32_C(0x00000000): *value = current->boot0; return 0;
    case TU104_UNLOCK_PLM_OFFSET: *value = current->plm; return 0;
    case UINT32_C(0x00409660): *value = current->feature_readout; return 0;
    case TU104_UNLOCK_SS0_OFFSET: *value = current->ss0; return 0;
    case UINT32_C(0x00409668): *value = current->speed_readout; return 0;
    case TU104_UNLOCK_SS1_OFFSET: *value = current->ss1; return 0;
    case TU104_WPR2_LO_OFFSET: *value = current->wpr2_lo; return 0;
    case TU104_WPR2_HI_OFFSET: *value = current->wpr2_hi; return 0;
    case UINT32_C(0x00118234): *value = current->gfw; return 0;
    case UINT32_C(0x00625f04): *value = current->workspace; return 0;
    case TU104_FRTS_SCRATCH_OFFSET: *value = current->scratch; return 0;
    default: break;
    }

    bank = falcon_bank(offset);
    relative = offset & UINT32_C(0x0000ffff);
    assert(relative < UINT32_C(0x700));
    if (relative == TU104_FALCON_IMEMD_OFFSET) {
        *value = current->imem[bank][
            (current->imem_control[bank] & UINT32_C(0xffff)) / 4];
        if (current->bad_imem_readback)
            *value ^= UINT32_C(1);
    } else if (relative == TU104_FALCON_DMEMD_OFFSET) {
        *value = current->dmem[bank][
            (current->dmem_control[bank] & UINT32_C(0xffff)) / 4];
        if (current->bad_dmem_readback)
            *value ^= UINT32_C(1);
    } else if (relative == TU104_FALCON_DMACTL_OFFSET &&
               current->reset_stuck[bank] && current->reset_seen[bank]) {
        *value = TU104_FALCON_DMACTL_SCRUBBING;
    } else if (relative == TU104_FALCON_DMACTL_OFFSET &&
               current->scrub_reads[bank] != 0) {
        --current->scrub_reads[bank];
        *value = TU104_FALCON_DMACTL_SCRUBBING;
    } else if (relative == TU104_UNLOCK_HWCFG2_OFFSET &&
               current->hwcfg2_scrub_reads[bank] != 0) {
        --current->hwcfg2_scrub_reads[bank];
        *value = current->regs[bank][relative / 4] |
            TU104_UNLOCK_HWCFG2_SCRUBBING;
    } else {
        *value = current->regs[bank][relative / 4];
    }
    return 0;
}

static void verify_frts_start(struct model *current)
{
    uint64_t code_base = current->dmem[0][0x24 / 4] |
        ((uint64_t)current->dmem[0][0x28 / 4] << 32);
    uint64_t data_base = current->dmem[0][0x40 / 4] |
        ((uint64_t)current->dmem[0][0x44 / 4] << 32);

    assert(current->dmem[0][0x20 / 4] == TU104_FWSEC_LOADER_CTX_DMA);
    assert(code_base == current->core->pool_physical);
    assert(data_base == current->core->pool_physical +
                        TU104_FWSEC_IMEM_BYTES);
    assert(current->dmem[0][0x30 / 4] == TU104_FWSEC_NONSECURE_BYTES);
    assert(current->dmem[0][0x34 / 4] == TU104_FWSEC_SECURE_OFFSET);
    assert(current->dmem[0][0x38 / 4] == TU104_FWSEC_SECURE_BYTES);
    assert(current->dmem[0][0x48 / 4] == TU104_FWSEC_DMEM_BYTES);
    assert(current->imem[0][TU104_GENERIC_BL_EXPECTED_DEST / 4] ==
           tu104_unlock_layout_le32(generic_bl));
    assert(current->regs[0][TU104_FALCON_BOOTVEC_OFFSET / 4] ==
           TU104_GENERIC_BL_BOOT_VECTOR);
}

static void verify_booter_start(struct model *current)
{
    const uint8_t *meta = pool + TU104_UNLOCK_META_OFFSET;
    const uint8_t *radix = pool + TU104_UNLOCK_RADIX_OFFSET;
    uint64_t meta_physical = current->regs[1][TU104_UNLOCK_MAILBOX0_OFFSET / 4] |
        ((uint64_t)current->regs[1][TU104_UNLOCK_MAILBOX1_OFFSET / 4] << 32);

    assert(meta_physical == current->core->pool_physical +
                            TU104_UNLOCK_META_OFFSET);
    assert(current->imem[1][0] == tu104_unlock_layout_le32(
               pool + TU104_UNLOCK_BOOTER_OFFSET));
    assert(current->dmem[1][TU104_UNLOCK_BOOTER_PATCH_DMEM_OFFSET / 4] ==
           tu104_unlock_layout_le32(booter_signature));
    assert(tu104_unlock_layout_le64(meta) == TU104_UNLOCK_META_MAGIC);
    assert(tu104_unlock_layout_le64(meta + 2 * 8) ==
           current->core->pool_physical + TU104_UNLOCK_RADIX_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 9 * 8) ==
           current->core->pool_physical + TU104_UNLOCK_V67_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 19 * 8) ==
           TU104_UNLOCK_FRTS_OFFSET);
    assert(tu104_unlock_layout_le64(meta + 22 * 8) ==
           TU104_UNLOCK_FB_BYTES);
    assert(tu104_unlock_layout_le64(radix) ==
           current->core->pool_physical + TU104_UNLOCK_RADIX_OFFSET +
               TU104_UNLOCK_PAGE_BYTES);
}

static void reset_falcon_model(struct model *current, unsigned bank)
{
    size_t index;

    memset(current->imem[bank], 0, sizeof(current->imem[bank]));
    memset(current->dmem[bank], 0, sizeof(current->dmem[bank]));
    current->imem_control[bank] = 0;
    current->dmem_control[bank] = 0;
    current->scrub_reads[bank] = 2;
    current->hwcfg2_scrub_reads[bank] = 2;
    for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
        current->regs[bank][tu104_falcon_relative_offsets[index] / 4] =
            bank ? tu104_sec2_initial_values[index] :
                   tu104_gsp_initial_values[index];
    }
    current->regs[bank][TU104_FALCON_CPUCTL_OFFSET / 4] = 0;
    current->regs[bank][TU104_FALCON_ENGINE_OFFSET / 4] = 0;
}

static int model_write32(void *opaque, uint32_t offset, uint32_t value)
{
    struct model *current = opaque;
    uint32_t relative;
    unsigned bank;

    assert(current != NULL);
    assert(tu104_unlock_write_allowed(offset));
    assert(offset != TU104_UNLOCK_SS0_OFFSET);
    assert(offset != TU104_UNLOCK_SS1_OFFSET);
    assert(offset != TU104_WPR2_LO_OFFSET);
    assert(offset != TU104_WPR2_HI_OFFSET);
    ++current->writes;
    if (current->writes == current->fail_write)
        return 1;

    bank = falcon_bank(offset);
    relative = offset & UINT32_C(0x0000ffff);
    current->regs[bank][relative / 4] = value;
    switch (relative) {
    case TU104_FALCON_ENGINE_OFFSET:
        if (value == 1) {
            current->regs[bank][relative / 4] = 1;
        } else {
            assert(value == 0);
            reset_falcon_model(current, bank);
            current->reset_seen[bank] = 1;
        }
        break;
    case TU104_FALCON_IMEMC_OFFSET:
        current->imem_control[bank] = value;
        break;
    case TU104_FALCON_DMEMC_OFFSET:
        current->dmem_control[bank] = value;
        break;
    case TU104_FALCON_IMEMD_OFFSET:
        current->imem[bank][
            (current->imem_control[bank] & UINT32_C(0xffff)) / 4] = value;
        if ((current->imem_control[bank] &
             TU104_FALCON_MEMC_AINCW) != 0)
            current->imem_control[bank] += 4;
        break;
    case TU104_FALCON_DMEMD_OFFSET:
        current->dmem[bank][
            (current->dmem_control[bank] & UINT32_C(0xffff)) / 4] = value;
        if ((current->dmem_control[bank] &
             TU104_FALCON_MEMC_AINCW) != 0)
            current->dmem_control[bank] += 4;
        break;
    case TU104_FALCON_CPUCTL_OFFSET:
    case TU104_FALCON_CPUCTL_ALIAS_OFFSET:
        if (value == TU104_FALCON_CPUCTL_START) {
            if (bank == 0) {
                verify_frts_start(current);
                ++current->gsp_starts;
                current->regs[0][TU104_FALCON_CPUCTL_OFFSET / 4] =
                    TU104_FALCON_CPUCTL_HALTED;
                current->scratch =
                    current->result_mode == RESULT_FRTS_SCRATCH ?
                        UINT32_C(0x00010000) : 0;
                current->wpr2_lo =
                    current->result_mode == RESULT_FRTS_WPR ?
                        TU104_WPR2_EXPECTED_LO - UINT32_C(0x10) :
                        TU104_WPR2_EXPECTED_LO;
                current->wpr2_hi = TU104_WPR2_EXPECTED_HI;
            } else {
                verify_booter_start(current);
                ++current->sec2_starts;
                current->regs[1][TU104_FALCON_CPUCTL_OFFSET / 4] =
                    current->result_mode == RESULT_BOOTER_HALT ?
                        0 : TU104_FALCON_CPUCTL_HALTED;
                current->regs[1][TU104_UNLOCK_MAILBOX0_OFFSET / 4] =
                    current->result_mode == RESULT_BOOTER_MAILBOX ? 1 : 0;
                current->ss0 = current->result_mode == RESULT_BOOTER_SS0 ?
                    UINT32_C(0x88888880) : TU104_UNLOCK_SS0_RESULT;
                current->ss1 = current->result_mode == RESULT_BOOTER_SS1 ?
                    UINT32_C(7) : TU104_UNLOCK_SS1_RESULT;
                current->plm = current->result_mode == RESULT_BOOTER_PLM ?
                    UINT32_C(0xffffffff) : TU104_UNLOCK_PLM_RESULT;
                current->wpr2_lo =
                    current->result_mode == RESULT_BOOTER_WPR ?
                        TU104_UNLOCK_COLD_WPR2_LO - UINT32_C(0x10) :
                        TU104_UNLOCK_COLD_WPR2_LO;
                current->wpr2_hi = TU104_UNLOCK_COLD_WPR2_HI;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static int model_delay_us(void *opaque, uint32_t microseconds)
{
    struct model *current = opaque;

    (void)microseconds;
    ++current->delays;
    return current->delays == current->fail_delay;
}

static int model_barrier(void *opaque)
{
    struct model *current = opaque;

    ++current->barriers;
    return current->barriers == current->fail_barrier;
}

static int model_event(void *opaque, unsigned stage, uint32_t first,
                       uint32_t second)
{
    struct model *current = opaque;

    assert(stage > TU104_UNLOCK_STAGE_NONE &&
           stage < TU104_UNLOCK_STAGE_COUNT);
    if (stage == TU104_UNLOCK_STAGE_BOOTER_START) {
        uint64_t meta = first | ((uint64_t)second << 32);
        assert(meta == current->core->pool_physical +
                       TU104_UNLOCK_META_OFFSET);
    }
    if (stage == TU104_UNLOCK_STAGE_BOOTER_RESULT ||
        stage == TU104_UNLOCK_STAGE_DONE) {
        assert(first == current->ss0 && second == current->ss1);
    }
    ++current->events;
    current->writes_at_stage[stage] = current->writes;
    return stage == current->fail_event_stage;
}

static int model_next_input(void *opaque)
{
    struct model *current = opaque;

    ++current->next_inputs;
    assert(current->core->stage == TU104_UNLOCK_STAGE_BOOTER_RESET);
    assert(current->core->mutated && current->gsp_starts == 1);
    return current->next_inputs == current->fail_next_input;
}

static void configure_core(struct tu104_unlock_core *target)
{
    memset(target, 0, sizeof(*target));
    target->opaque = &model;
    target->read32 = model_read32;
    target->write32 = model_write32;
    target->delay_us = model_delay_us;
    target->barrier = model_barrier;
    target->event = model_event;
    target->next_input = model_next_input;
    target->pool = pool;
    target->pool_bytes = TU104_UNLOCK_POOL_BYTES;
    target->pool_physical = UINT64_C(0x86c000000);
    target->native_fwsec = native_fwsec;
    target->native_fwsec_bytes = sizeof(native_fwsec);
    target->generic_bl = generic_bl;
    target->generic_bl_bytes = sizeof(generic_bl);
    target->booter = booter;
    target->booter_bytes = sizeof(booter);
    target->booter_signature = booter_signature;
    target->booter_signature_bytes = sizeof(booter_signature);
    target->v67 = v67;
    target->v67_bytes = sizeof(v67);
    target->boot_container = boot_container;
    target->boot_container_bytes = sizeof(boot_container);
}

static void initialize_model(void)
{
    size_t index;

    memset(&model, 0, sizeof(model));
    model.prom = prom;
    model.core = &core;
    model.boot0 = TU104_BOOT0;
    model.plm = TU104_UNLOCK_PLM_RESULT;
    model.feature_readout = UINT32_C(0x000002f3);
    model.ss0 = 0;
    model.speed_readout = UINT32_C(0x00400080);
    model.ss1 = UINT32_C(6);
    model.wpr2_lo = TU104_UNLOCK_COLD_WPR2_LO;
    model.wpr2_hi = TU104_UNLOCK_COLD_WPR2_HI;
    model.gfw = UINT32_C(0x000003ff);
    model.workspace = UINT32_C(0x00020e09);
    model.scratch = 0;
    for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
        model.regs[0][tu104_falcon_relative_offsets[index] / 4] =
            tu104_gsp_initial_values[index];
        model.regs[1][tu104_falcon_relative_offsets[index] / 4] =
            tu104_sec2_initial_values[index];
    }
}

static void setup_run(void)
{
    initialize_model();
    core = prepared_template;
    core.opaque = &model;
    core.stage = TU104_UNLOCK_STAGE_NONE;
    core.mutated = 0;
    core.fwsec_started = 0;
    core.fwsec_verified = 0;
    core.booter_started = 0;
    core.unlocked = 0;
    core.quiescent = 0;
    core.booter_mailbox0 = 0;
    core.ss0_result = 0;
    core.ss1_result = 0;
    core.frts.opaque = &core;
    core.frts.stage = TU104_FWSEC_STAGE_NONE;
    core.frts.mutated = 0;
    core.frts.firmware_started = 0;
    core.frts.result_verified = 0;
    core.frts.quiescent = 0;
    core.frts.scratch_result = 0;
    core.frts.wpr2_lo_result = 0;
    core.frts.wpr2_hi_result = 0;
    model.core = &core;
}

static void test_preparation(void)
{
    struct tu104_unlock_core candidate;
    uint8_t saved;

    initialize_model();
    configure_core(&candidate);
    model.core = &candidate;
    assert(tu104_unlock_prepare(&candidate) == TU104_UNLOCK_OK);
    assert(candidate.prepared && !candidate.mutated);
    assert(candidate.frts.prepared);
    assert(candidate.prepared_control_crc32 ==
           tu104_unlock_layout_crc32(pool, TU104_UNLOCK_DATA_OFFSET));
    prepared_template = candidate;
    ++cases;

    saved = v67[0];
    v67[0] ^= 1;
    configure_core(&candidate);
    assert(tu104_unlock_prepare(&candidate) == TU104_UNLOCK_GUARD);
    assert(!candidate.mutated);
    v67[0] = saved;
    assert(tu104_unlock_prepare(&candidate) == TU104_UNLOCK_OK);
    prepared_template = candidate;
    ++cases;
}

static void test_success_and_guards(void)
{
    unsigned success_reads;
    unsigned success_delays;
    unsigned success_barriers;
    unsigned success_events;
    unsigned total_writes;
    unsigned booter_load_first;
    unsigned booter_load_last;
    unsigned direct_load_writes;

    setup_run();
    assert(tu104_unlock_load_booter(&core) == TU104_UNLOCK_OK);
    direct_load_writes = model.writes;
    assert(direct_load_writes > 10000);

    /* Exercise the large port-load loop without replaying FWSEC each time. */
    for (unsigned boundary = 1; boundary <= direct_load_writes; ++boundary) {
        setup_run();
        model.fail_write = boundary;
        assert(tu104_unlock_load_booter(&core) == TU104_UNLOCK_IO);
        assert(core.mutated && !core.quiescent);
        assert(model.writes == boundary);
        ++cases;
    }

    setup_run();
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_OK);
    assert(core.mutated && core.fwsec_started && core.fwsec_verified);
    assert(core.booter_started && core.unlocked && core.quiescent);
    assert(model.gsp_starts == 1 && model.sec2_starts == 1);
    assert(model.next_inputs == 1);
    assert(model.ss0 == TU104_UNLOCK_SS0_RESULT &&
           model.ss1 == TU104_UNLOCK_SS1_RESULT);
    assert(model.wpr2_lo == TU104_UNLOCK_COLD_WPR2_LO &&
           model.wpr2_hi == TU104_UNLOCK_COLD_WPR2_HI);
    assert(model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_RESET] == 166);
    booter_load_first =
        model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_LOAD];
    booter_load_last =
        model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_START];
    total_writes = model.writes;
    assert(booter_load_first > 166 && booter_load_last > booter_load_first);
    assert(booter_load_last - booter_load_first == direct_load_writes);
    assert(total_writes > booter_load_last);
    success_reads = model.reads;
    success_delays = model.delays;
    success_barriers = model.barriers;
    success_events = model.events;
    ++cases;

    setup_run();
    pool[TU104_UNLOCK_META_OFFSET] ^= 1;
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_GUARD);
    assert(!core.mutated && model.reads == 0 && model.writes == 0);
    pool[TU104_UNLOCK_META_OFFSET] ^= 1;
    ++cases;

    setup_run();
    assert(tu104_unlock_write(&core, TU104_UNLOCK_SS0_OFFSET,
                              TU104_UNLOCK_SS0_RESULT) ==
           TU104_UNLOCK_GUARD);
    assert(!core.mutated && model.writes == 0);
    ++cases;

    setup_run();
    assert(tu104_unlock_write(&core, TU104_WPR2_LO_OFFSET, 0) ==
           TU104_UNLOCK_GUARD);
    assert(!core.mutated && model.writes == 0);
    ++cases;

    setup_run();
    assert(tu104_unlock_write(&core, UINT32_C(0x00000000), 0) ==
           TU104_UNLOCK_GUARD);
    assert(!core.mutated && model.writes == 0);
    ++cases;

    printf("unlock success counts: reads=%u writes=%u delays=%u "
           "barriers=%u events=%u; booter-load writes=%u\n",
           success_reads, total_writes, success_delays, success_barriers,
           success_events, direct_load_writes);
}

static void test_result_failures(void)
{
    enum result_mode mode;

    for (mode = RESULT_FRTS_SCRATCH; mode <= RESULT_BOOTER_HALT; ++mode) {
        setup_run();
        model.result_mode = mode;
        assert(tu104_unlock_run(&core) != TU104_UNLOCK_OK);
        assert(core.mutated && !core.quiescent);
        if (mode <= RESULT_FRTS_WPR)
            assert(model.sec2_starts == 0);
        else
            assert(model.sec2_starts == 1);
        ++cases;
    }

    setup_run();
    model.reset_stuck[1] = 1;
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_TIMEOUT);
    assert(core.mutated && !core.booter_started && !core.quiescent);
    ++cases;

    setup_run();
    model.fail_next_input = 1;
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_IO);
    assert(core.mutated && model.next_inputs == 1 &&
           !core.booter_started && !core.quiescent);
    ++cases;
}

static void test_event_and_new_boundary_failures(void)
{
    unsigned successful_reads;
    unsigned successful_writes;
    unsigned successful_delays;
    unsigned successful_barriers;
    unsigned reads_before_booter;
    unsigned writes_before_booter;

    setup_run();
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_OK);
    successful_reads = model.reads;
    successful_writes = model.writes;
    successful_delays = model.delays;
    successful_barriers = model.barriers;
    writes_before_booter =
        model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_RESET];

    setup_run();
    model.fail_event_stage = TU104_UNLOCK_STAGE_BOOTER_RESET;
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_LOG);
    reads_before_booter = model.reads;
    assert(model.writes == writes_before_booter);
    ++cases;

    for (unsigned stage = TU104_UNLOCK_STAGE_FRTS_GUARDS;
         stage < TU104_UNLOCK_STAGE_COUNT; ++stage) {
        setup_run();
        model.fail_event_stage = stage;
        assert(tu104_unlock_run(&core) == TU104_UNLOCK_LOG);
        assert(!core.quiescent || stage == TU104_UNLOCK_STAGE_DONE);
        ++cases;
    }

    /* FRTS already has exhaustive boundary tests. Cover every later read. */
    for (unsigned boundary = reads_before_booter + 1;
         boundary <= successful_reads; ++boundary) {
        setup_run();
        model.fail_read = boundary;
        assert(tu104_unlock_run(&core) == TU104_UNLOCK_IO);
        assert(core.mutated && !core.quiescent);
        assert(model.reads == boundary);
        ++cases;
    }

    /* Cover non-load writes; load writes are exhaustively covered above. */
    setup_run();
    assert(tu104_unlock_run(&core) == TU104_UNLOCK_OK);
    unsigned load_begin =
        model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_LOAD] + 1;
    unsigned load_end =
        model.writes_at_stage[TU104_UNLOCK_STAGE_BOOTER_START];
    for (unsigned boundary = writes_before_booter + 1;
         boundary <= successful_writes; ++boundary) {
        if (boundary >= load_begin && boundary <= load_end)
            continue;
        setup_run();
        model.fail_write = boundary;
        assert(tu104_unlock_run(&core) == TU104_UNLOCK_IO);
        assert(core.mutated && !core.quiescent);
        ++cases;
    }

    for (unsigned boundary = 1; boundary <= successful_delays; ++boundary) {
        setup_run();
        model.fail_delay = boundary;
        assert(tu104_unlock_run(&core) == TU104_UNLOCK_IO);
        assert(core.mutated && !core.quiescent);
        ++cases;
    }
    for (unsigned boundary = 1; boundary <= successful_barriers; ++boundary) {
        setup_run();
        model.fail_barrier = boundary;
        assert(tu104_unlock_run(&core) == TU104_UNLOCK_IO);
        assert(core.mutated && !core.quiescent);
        ++cases;
    }
}

int main(void)
{
    pool = malloc(TU104_UNLOCK_POOL_BYTES);
    assert(pool != NULL);
    load_exact(TU104_TEST_PROM_FIXTURE,
               prom, sizeof(prom));
    memcpy(native_fwsec, prom + TU104_FWSEC_IMAGE_PROM_OFFSET,
           sizeof(native_fwsec));
    load_exact("build/tu10x-assets/bl_gsp_tu102.bin",
               generic_bl, sizeof(generic_bl));
    load_exact("build/tu10x-assets/booter_ucode_prod.bin",
               booter, sizeof(booter));
    load_exact("build/tu10x-assets/booter_sig_prod.bin",
               booter_signature, sizeof(booter_signature));
    load_exact("references/xrip__cmp50hx-unlock/efi-unlock/blobs/"
               "v67_payload.bin", v67, sizeof(v67));
    load_exact("references/xrip__cmp50hx-unlock/efi-unlock/blobs/"
               "gsp_rm_boot_dbg.bin", boot_container,
               sizeof(boot_container));

    test_preparation();
    test_success_and_guards();
    test_result_failures();
    test_event_and_new_boundary_failures();
    free(pool);
    printf("PASS: %u TU104 full-unlock offline cases; FRTS inheritance, "
           "Booter ports, results, cleanup, and all new I/O boundaries "
           "verified; no hardware access.\n", cases);
    return 0;
}
