#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tu104_fwsec_probe_core.h"

#ifndef TU104_TEST_PROM_FIXTURE
#define TU104_TEST_PROM_FIXTURE \
    "artifacts/2070s-efirom/rtx2070s-gigabyte-90.04.76.00.D5-prom512k-run1.bin"
#endif

#define PROM_BYTES 0x80000u
#define FALCON_REGISTER_WORDS (0x700u / 4u)
#define FALCON_MEMORY_WORDS (0x10000u / 4u)

enum result_mode {
    RESULT_NORMAL = 0,
    RESULT_SCRATCH_ERROR,
    RESULT_BAD_WPR_LO,
    RESULT_BAD_WPR_HI,
    RESULT_HALT_TIMEOUT
};

struct model {
    uint32_t regs[2][FALCON_REGISTER_WORDS];
    uint32_t imem[FALCON_MEMORY_WORDS];
    uint32_t dmem[FALCON_MEMORY_WORDS];
    uint32_t imem_control;
    uint32_t dmem_control;
    uint32_t boot0;
    uint32_t feature_plm;
    uint32_t feature_readout;
    uint32_t ss0;
    uint32_t speed_readout;
    uint32_t ss1;
    uint32_t wpr2_lo;
    uint32_t wpr2_hi;
    uint32_t gfw;
    uint32_t workspace;
    uint32_t scratch;
    uint32_t scrub_reads_remaining;
    uint64_t total_delay;
    uint64_t write_trace_hash;
    unsigned reads;
    unsigned writes;
    unsigned delays;
    unsigned barriers;
    unsigned events;
    unsigned starts;
    unsigned alias_starts;
    unsigned fail_read;
    unsigned fail_write;
    unsigned fail_delay;
    unsigned fail_barrier;
    unsigned fail_event_stage;
    unsigned writes_at_stage[TU104_FWSEC_STAGE_COUNT];
    uint32_t rom_xor_offset;
    enum result_mode result_mode;
    int alias_after_reset;
    int reset_stuck;
    int bad_engine_assert;
    int bad_engine_deassert;
    int bad_transcfg_after_reset;
    int bad_hwcfg_after_reset;
    int bad_imem_readback;
    int bad_dmem_readback;
    int bad_bootvec_readback;
    const uint8_t *prom;
    struct tu104_fwsec_probe *probe;
};

static uint8_t prom[PROM_BYTES];
static uint8_t native_fwsec[TU104_FWSEC_STORED_BYTES];
static uint8_t generic_bl[TU104_GENERIC_BL_IMAGE_BYTES];
static uint8_t staging[TU104_FWSEC_STAGING_BYTES];
static struct model model;
static struct tu104_fwsec_probe probe;
static unsigned cases;

static void load_exact(const char *path, uint8_t *destination, size_t bytes)
{
    FILE *file = fopen(path, "rb");

    assert(file != NULL);
    assert(fread(destination, 1, bytes, file) == bytes);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

static unsigned falcon_bank(uint32_t offset)
{
    if ((offset & UINT32_C(0xffff0000)) == TU104_GSP_BASE)
        return 0;
    assert((offset & UINT32_C(0xffff0000)) == TU104_SEC2_BASE);
    return 1;
}

static int model_read32(void *opaque, uint32_t offset, uint32_t *value)
{
    struct model *current = opaque;
    unsigned bank;
    uint32_t relative;

    assert(current != NULL && value != NULL);
    ++current->reads;
    if (current->reads == current->fail_read)
        return 1;

    if (offset >= TU104_PROM_WINDOW &&
        offset <= TU104_PROM_WINDOW + PROM_BYTES - 4) {
        uint32_t prom_offset = offset - TU104_PROM_WINDOW;

        assert((prom_offset & 3u) == 0);
        *value = tu104_fwsec_probe_le32(current->prom + prom_offset);
        if (prom_offset == current->rom_xor_offset)
            *value ^= UINT32_C(1);
        return 0;
    }

    switch (offset) {
    case UINT32_C(0x00000000): *value = current->boot0; return 0;
    case UINT32_C(0x00409650): *value = current->feature_plm; return 0;
    case UINT32_C(0x00409660): *value = current->feature_readout; return 0;
    case UINT32_C(0x00409664): *value = current->ss0; return 0;
    case UINT32_C(0x00409668): *value = current->speed_readout; return 0;
    case UINT32_C(0x0040966c): *value = current->ss1; return 0;
    case TU104_WPR2_LO_OFFSET: *value = current->wpr2_lo; return 0;
    case TU104_WPR2_HI_OFFSET: *value = current->wpr2_hi; return 0;
    case UINT32_C(0x00118234): *value = current->gfw; return 0;
    case UINT32_C(0x00625f04): *value = current->workspace; return 0;
    case TU104_FRTS_SCRATCH_OFFSET: *value = current->scratch; return 0;
    default: break;
    }

    bank = falcon_bank(offset);
    relative = offset & UINT32_C(0x0000ffff);
    assert(relative < 0x700u);
    if (bank == 0 && relative == TU104_FALCON_IMEMD_OFFSET) {
        *value = current->imem[(current->imem_control & 0xffffu) / 4u];
        if (current->bad_imem_readback)
            *value ^= UINT32_C(1);
    } else if (bank == 0 && relative == TU104_FALCON_DMEMD_OFFSET) {
        *value = current->dmem[(current->dmem_control & 0xffffu) / 4u];
        if (current->bad_dmem_readback)
            *value ^= UINT32_C(1);
    } else if (bank == 0 && relative == TU104_FALCON_DMACTL_OFFSET &&
               current->reset_stuck && current->writes != 0) {
        *value = TU104_FALCON_DMACTL_SCRUBBING;
    } else if (bank == 0 && relative == TU104_FALCON_DMACTL_OFFSET &&
               current->scrub_reads_remaining != 0) {
        --current->scrub_reads_remaining;
        *value = TU104_FALCON_DMACTL_SCRUBBING;
    } else if (bank == 0 && relative == TU104_FALCON_ENGINE_OFFSET &&
               current->bad_engine_assert &&
               current->regs[0][relative / 4u] == 1) {
        *value = 0;
    } else if (bank == 0 && relative == TU104_FALCON_ENGINE_OFFSET &&
               current->bad_engine_deassert &&
               current->regs[0][relative / 4u] == 0 &&
               current->writes != 0) {
        *value = 1;
    } else {
        *value = current->regs[bank][relative / 4u];
        if (bank == 0 && relative == TU104_FALCON_BOOTVEC_OFFSET &&
            current->bad_bootvec_readback &&
            current->regs[0][relative / 4u] ==
                TU104_GENERIC_BL_BOOT_VECTOR)
            *value ^= UINT32_C(1);
    }
    return 0;
}

static void verify_start_inputs(struct model *current)
{
    const struct tu104_fwsec_loader_desc *descriptor =
        (const struct tu104_fwsec_loader_desc *)current->dmem;
    uint64_t code_base = (uint64_t)descriptor->code_dma_base_lo |
        ((uint64_t)descriptor->code_dma_base_hi << 32);
    uint64_t data_base = (uint64_t)descriptor->data_dma_base_lo |
        ((uint64_t)descriptor->data_dma_base_hi << 32);
    size_t index;

    assert(descriptor->ctx_dma == TU104_FWSEC_LOADER_CTX_DMA);
    assert(code_base == current->probe->staging_physical);
    assert(data_base == current->probe->staging_physical +
                        TU104_FWSEC_IMEM_BYTES);
    assert(descriptor->nonsecure_code_offset ==
           TU104_FWSEC_NONSECURE_OFFSET);
    assert(descriptor->nonsecure_code_bytes == TU104_FWSEC_NONSECURE_BYTES);
    assert(descriptor->secure_code_offset == TU104_FWSEC_SECURE_OFFSET);
    assert(descriptor->secure_code_bytes == TU104_FWSEC_SECURE_BYTES);
    assert(descriptor->data_bytes == TU104_FWSEC_DMEM_BYTES);
    assert(current->regs[0][TU104_FALCON_TRANSCFG4_OFFSET / 4u] ==
           UINT32_C(0x115));
    assert((current->regs[0][TU104_FALCON_FBIF_CTL_OFFSET / 4u] &
            TU104_FALCON_FBIF_ALLOW_PHYS_NO_CTX) != 0);
    assert(current->regs[0][TU104_FALCON_BOOTVEC_OFFSET / 4u] ==
           TU104_GENERIC_BL_BOOT_VECTOR);
    for (index = 0; index < TU104_GENERIC_BL_CODE_BYTES / 4u; ++index) {
        assert(current->imem[TU104_GENERIC_BL_EXPECTED_DEST / 4u + index] ==
               tu104_fwsec_probe_le32(generic_bl + index * 4u));
    }
    assert(tu104_fwsec_probe_crc32(current->probe->staging,
                                   current->probe->staging_bytes) ==
           TU104_FWSEC_PATCHED_STAGING_CRC32);
}

static int model_write32(void *opaque, uint32_t offset, uint32_t value)
{
    struct model *current = opaque;
    uint32_t relative;
    unsigned byte;

    assert(current != NULL);
    assert(tu104_fwsec_probe_write_allowed(offset));
    assert((offset & UINT32_C(0xffff0000)) == TU104_GSP_BASE);
    ++current->writes;
    if (current->writes == current->fail_write)
        return 1;
    for (byte = 0; byte < 4; ++byte) {
        current->write_trace_hash ^=
            (uint8_t)(offset >> (byte * 8));
        current->write_trace_hash *= UINT64_C(1099511628211);
    }
    for (byte = 0; byte < 4; ++byte) {
        current->write_trace_hash ^=
            (uint8_t)(value >> (byte * 8));
        current->write_trace_hash *= UINT64_C(1099511628211);
    }

    relative = offset & UINT32_C(0x0000ffff);
    current->regs[0][relative / 4u] = value;
    switch (relative) {
    case TU104_FALCON_ENGINE_OFFSET:
        if (value == 1) {
            current->regs[0][TU104_FALCON_ENGINE_OFFSET / 4u] = 1;
        } else {
            assert(value == 0);
            current->regs[0][TU104_FALCON_ENGINE_OFFSET / 4u] = 0;
            current->regs[0][TU104_FALCON_DMACTL_OFFSET / 4u] = 0;
            current->regs[0][TU104_FALCON_CPUCTL_OFFSET / 4u] =
                current->alias_after_reset ?
                    TU104_FALCON_CPUCTL_ALIAS_ENABLE : 0;
            current->regs[0][TU104_FALCON_TRANSCFG4_OFFSET / 4u] =
                current->bad_transcfg_after_reset ?
                    UINT32_C(0x111) : UINT32_C(0x110);
            current->regs[0][TU104_FALCON_HWCFG_OFFSET / 4u] =
                current->bad_hwcfg_after_reset ?
                    UINT32_C(0x404200ff) : UINT32_C(0x40420100);
            current->scrub_reads_remaining = 2;
        }
        break;
    case TU104_FALCON_IMEMC_OFFSET:
        current->imem_control = value;
        break;
    case TU104_FALCON_DMEMC_OFFSET:
        current->dmem_control = value;
        break;
    case TU104_FALCON_IMEMD_OFFSET:
        current->imem[(current->imem_control & 0xffffu) / 4u] = value;
        if ((current->imem_control & TU104_FALCON_MEMC_AINCW) != 0)
            current->imem_control += 4;
        break;
    case TU104_FALCON_DMEMD_OFFSET:
        current->dmem[(current->dmem_control & 0xffffu) / 4u] = value;
        if ((current->dmem_control & TU104_FALCON_MEMC_AINCW) != 0)
            current->dmem_control += 4;
        break;
    case TU104_FALCON_CPUCTL_OFFSET:
    case TU104_FALCON_CPUCTL_ALIAS_OFFSET:
        if (value == TU104_FALCON_CPUCTL_START) {
            verify_start_inputs(current);
            ++current->starts;
            if (relative == TU104_FALCON_CPUCTL_ALIAS_OFFSET)
                ++current->alias_starts;
            current->regs[0][TU104_FALCON_CPUCTL_OFFSET / 4u] =
                current->result_mode == RESULT_HALT_TIMEOUT ?
                    0 : TU104_FALCON_CPUCTL_HALTED;
            current->scratch =
                current->result_mode == RESULT_SCRATCH_ERROR ?
                    UINT32_C(0x00010000) : 0;
            current->wpr2_lo =
                current->result_mode == RESULT_BAD_WPR_LO ?
                    TU104_WPR2_EXPECTED_LO - UINT32_C(0x10) :
                    TU104_WPR2_EXPECTED_LO;
            current->wpr2_hi =
                current->result_mode == RESULT_BAD_WPR_HI ?
                    0 : TU104_WPR2_EXPECTED_HI;
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

    ++current->delays;
    current->total_delay += microseconds;
    assert(current->total_delay <= UINT64_C(10000000));
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

    assert(stage > TU104_FWSEC_STAGE_NONE &&
           stage < TU104_FWSEC_STAGE_COUNT);
    if (stage == TU104_FWSEC_STAGE_START) {
        assert(first == TU104_GENERIC_BL_BOOT_VECTOR);
        assert(second == TU104_FRTS_OFFSET_4K);
    }
    if (stage == TU104_FWSEC_STAGE_RESULT ||
        stage == TU104_FWSEC_STAGE_DONE) {
        assert(first == current->wpr2_lo);
        assert(second == current->wpr2_hi);
    }
    ++current->events;
    current->writes_at_stage[stage] = current->writes;
    return stage == current->fail_event_stage;
}

static void setup(void)
{
    size_t index;

    memset(&model, 0, sizeof(model));
    memset(&probe, 0, sizeof(probe));
    memset(staging, 0xa5, sizeof(staging));

    model.prom = prom;
    model.boot0 = TU104_BOOT0;
    model.feature_plm = UINT32_C(0xffffff8f);
    model.feature_readout = UINT32_C(0x000002f3);
    model.ss0 = 0;
    model.speed_readout = UINT32_C(0x00400080);
    model.ss1 = UINT32_C(6);
    model.wpr2_lo = UINT32_C(0x1ffffe00);
    model.wpr2_hi = 0;
    model.gfw = UINT32_C(0x000003ff);
    model.workspace = UINT32_C(0x00020e09);
    model.scratch = 0;
    model.rom_xor_offset = UINT32_MAX;
    model.write_trace_hash = UINT64_C(1469598103934665603);

    for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
        model.regs[0][tu104_falcon_relative_offsets[index] / 4u] =
            tu104_gsp_initial_values[index];
        model.regs[1][tu104_falcon_relative_offsets[index] / 4u] =
            tu104_sec2_initial_values[index];
    }

    probe.opaque = &model;
    probe.read32 = model_read32;
    probe.write32 = model_write32;
    probe.delay_us = model_delay_us;
    probe.barrier = model_barrier;
    probe.event = model_event;
    probe.staging = staging;
    probe.staging_bytes = sizeof(staging);
    probe.staging_physical = UINT64_C(0x86c000000);
    probe.native_fwsec = native_fwsec;
    probe.native_fwsec_bytes = sizeof(native_fwsec);
    probe.generic_bl = generic_bl;
    probe.generic_bl_bytes = sizeof(generic_bl);
    model.probe = &probe;
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_OK);
}

static void test_preparation(void)
{
    static const uint8_t command[TU104_FWSEC_FRTS_COMMAND_BYTES] = {
        0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
        0xe0, 0xfe, 0x1f, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t *dmem;
    unsigned changed = 0;
    size_t index;

    setup();
    dmem = staging + TU104_FWSEC_IMEM_BYTES;
    assert(tu104_fwsec_probe_crc32(native_fwsec, sizeof(native_fwsec)) ==
           TU104_FWSEC_NATIVE_CRC32);
    assert(tu104_fwsec_probe_crc32(generic_bl, sizeof(generic_bl)) ==
           TU104_GENERIC_BL_CRC32);
    assert(tu104_fwsec_probe_crc32(staging, sizeof(staging)) ==
           TU104_FWSEC_PATCHED_STAGING_CRC32);
    assert(tu104_fwsec_probe_le32(
               dmem + TU104_FWSEC_INIT_COMMAND_OFFSET) ==
           TU104_FWSEC_FRTS_COMMAND);
    assert(memcmp(dmem + TU104_FWSEC_COMMAND_OFFSET,
                  command, sizeof(command)) == 0);
    for (index = 0; index < sizeof(native_fwsec); ++index) {
        if (staging[index] != native_fwsec[index])
            ++changed;
    }
    assert(changed == 11);
    for (index = sizeof(native_fwsec); index < sizeof(staging); ++index)
        assert(staging[index] == 0);
    ++cases;

    setup();
    probe.staging_physical = TU104_FWSEC_DMA32_LIMIT -
        TU104_FWSEC_DMA_ALIGNMENT;
    memset(staging, 0xa5, sizeof(staging));
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(staging[0] == 0xa5 && staging[sizeof(staging) - 1] == 0xa5);
    ++cases;

    setup();
    probe.staging_physical++;
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_GUARD);
    ++cases;

    setup();
    probe.staging_bytes--;
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_GUARD);
    ++cases;

    setup();
    native_fwsec[0] ^= 1;
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_GUARD);
    native_fwsec[0] ^= 1;
    ++cases;

    setup();
    generic_bl[0] ^= 1;
    assert(tu104_fwsec_probe_prepare(&probe) == TU104_FWSEC_PROBE_GUARD);
    generic_bl[0] ^= 1;
    ++cases;
}

static void expect_post_mutation_failure(int expected)
{
    int status = tu104_fwsec_probe_run(&probe);

    if (status != expected)
        fprintf(stderr, "unexpected injected result: got=%d expected=%d "
                "stage=%u reads=%u writes=%u starts=%u\n",
                status, expected, probe.stage, model.reads, model.writes,
                model.starts);
    assert(status == expected);
    assert(probe.mutated);
    assert(!probe.quiescent);
    ++cases;
}

static void test_guards_and_results(void)
{
    setup();
    model.boot0 ^= 1;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(!probe.mutated && model.writes == 0);
    ++cases;

    setup();
    model.regs[1][TU104_FALCON_CPUCTL_OFFSET / 4u] ^= 1;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(!probe.mutated && model.writes == 0);
    ++cases;

    setup();
    model.rom_xor_offset = TU104_FWSEC_IMAGE_PROM_OFFSET + 0x100;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(!probe.mutated && model.writes == 0);
    ++cases;

    setup();
    staging[0] ^= 1;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(model.reads == 0 && model.writes == 0);
    ++cases;

    setup();
    assert(tu104_fwsec_probe_write(&probe, TU104_WPR2_LO_OFFSET, 0) ==
           TU104_FWSEC_PROBE_GUARD);
    assert(!probe.mutated && model.writes == 0);
    ++cases;

    setup();
    model.reset_stuck = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_TIMEOUT);
    assert(model.starts == 0);

    setup();
    model.bad_engine_assert = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.bad_engine_deassert = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.bad_transcfg_after_reset = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_GUARD);

    setup();
    model.bad_hwcfg_after_reset = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_GUARD);

    setup();
    model.bad_imem_readback = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.bad_dmem_readback = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.bad_bootvec_readback = 1;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.result_mode = RESULT_SCRATCH_ERROR;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);
    assert(probe.firmware_started && !probe.result_verified);

    setup();
    model.result_mode = RESULT_BAD_WPR_LO;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.result_mode = RESULT_BAD_WPR_HI;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_VERIFY);

    setup();
    model.result_mode = RESULT_HALT_TIMEOUT;
    expect_post_mutation_failure(TU104_FWSEC_PROBE_TIMEOUT);
}

static void test_every_io_boundary(void)
{
    unsigned total_reads;
    unsigned total_writes;
    unsigned total_delays;
    unsigned total_barriers;
    unsigned index;
    uint64_t write_trace_hash;

    setup();
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_OK);
    assert(probe.mutated && probe.firmware_started &&
           probe.result_verified && probe.quiescent);
    assert(model.starts == 1 && model.alias_starts == 0);
    assert(model.wpr2_lo == TU104_WPR2_EXPECTED_LO &&
           model.wpr2_hi == TU104_WPR2_EXPECTED_HI);
    total_reads = model.reads;
    total_writes = model.writes;
    total_delays = model.delays;
    total_barriers = model.barriers;
    write_trace_hash = model.write_trace_hash;
    assert(total_reads > 10000 && total_writes > 100 &&
           total_delays > 0 && total_barriers == 1);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_RESET] == 0);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_LOADER] == 3);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_START] == 162);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_RESULT] == 163);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_CLEANUP] == 163);
    assert(model.writes_at_stage[TU104_FWSEC_STAGE_DONE] == 166);
    assert(write_trace_hash == UINT64_C(0x0f506b2be62492f4));
    ++cases;

    setup();
    model.alias_after_reset = 1;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_OK);
    assert(model.starts == 1 && model.alias_starts == 1 && probe.quiescent);
    ++cases;

    setup();
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_OK);
    index = model.writes;
    assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_GUARD);
    assert(model.writes == index);
    ++cases;

    for (index = 1; index <= total_reads; ++index) {
        int status;

        setup();
        model.fail_read = index;
        status = tu104_fwsec_probe_run(&probe);
        assert(status == TU104_FWSEC_PROBE_IO);
        assert(model.reads == index);
        assert(probe.mutated == (model.writes != 0));
        assert(!probe.quiescent);
        ++cases;
    }

    for (index = 1; index <= total_writes; ++index) {
        setup();
        model.fail_write = index;
        assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_IO);
        assert(model.writes == index && probe.mutated);
        assert(!probe.quiescent);
        ++cases;
    }

    for (index = 1; index <= total_delays; ++index) {
        setup();
        model.fail_delay = index;
        assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_IO);
        assert(model.delays == index && probe.mutated && !probe.quiescent);
        ++cases;
    }

    for (index = 1; index <= total_barriers; ++index) {
        setup();
        model.fail_barrier = index;
        assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_IO);
        assert(model.barriers == index && probe.mutated && !probe.quiescent);
        ++cases;
    }

    for (index = TU104_FWSEC_STAGE_GUARDS;
         index < TU104_FWSEC_STAGE_COUNT; ++index) {
        setup();
        model.fail_event_stage = index;
        assert(tu104_fwsec_probe_run(&probe) == TU104_FWSEC_PROBE_LOG);
        assert(model.writes == model.writes_at_stage[index]);
        if (index <= TU104_FWSEC_STAGE_RESET)
            assert(!probe.mutated);
        else
            assert(probe.mutated);
        if (index == TU104_FWSEC_STAGE_DONE)
            assert(probe.quiescent);
        else
            assert(!probe.quiescent);
        ++cases;
    }

    printf("boundary counts: reads=%u writes=%u delays=%u barriers=%u "
           "events=%u write-trace=%016llx\n", total_reads, total_writes,
           total_delays, total_barriers, TU104_FWSEC_STAGE_COUNT - 1,
           (unsigned long long)write_trace_hash);
}

int main(void)
{
    load_exact(TU104_TEST_PROM_FIXTURE,
               prom, sizeof(prom));
    memcpy(native_fwsec, prom + TU104_FWSEC_IMAGE_PROM_OFFSET,
           sizeof(native_fwsec));
    load_exact("build/tu10x-assets/bl_gsp_tu102.bin",
               generic_bl, sizeof(generic_bl));

    test_preparation();
    test_guards_and_results();
    test_every_io_boundary();

    printf("PASS: %u TU104 FRTS-only offline cases; every successful-path "
           "read/write/delay/barrier/event failure boundary injected; "
           "no hardware access.\n", cases);
    return 0;
}
