#ifndef TU104_UNLOCK_CORE_H
#define TU104_UNLOCK_CORE_H

/*
 * Host-testable state machine for one target-pinned, volatile TU104 unlock.
 *
 * All hardware and logging access is injected. The only permitted writes are
 * Falcon GSP/SEC2 ports; direct host writes to WPR2, feature selectors,
 * flash, display, clocks, power, PCI configuration, and BAR placement are
 * impossible through this interface. A platform wrapper must retain the
 * 32 MiB pool and stop in firmware whenever `mutated && !quiescent`.
 */

#include <stddef.h>
#include <stdint.h>

#include "tu104_fwsec_probe_core.h"
#include "tu104_unlock_layout.h"

#define TU104_UNLOCK_MAILBOX0_OFFSET UINT32_C(0x0040)
#define TU104_UNLOCK_MAILBOX1_OFFSET UINT32_C(0x0044)
#define TU104_UNLOCK_HWCFG2_OFFSET UINT32_C(0x00f4)
#define TU104_UNLOCK_HWCFG2_SCRUBBING UINT32_C(0x00001000)

#define TU104_UNLOCK_SS0_OFFSET UINT32_C(0x00409664)
#define TU104_UNLOCK_SS1_OFFSET UINT32_C(0x0040966c)
#define TU104_UNLOCK_PLM_OFFSET UINT32_C(0x00409650)
#define TU104_UNLOCK_SS0_RESULT UINT32_C(0x88888888)
#define TU104_UNLOCK_SS1_RESULT UINT32_C(0x00000008)
#define TU104_UNLOCK_PLM_RESULT UINT32_C(0xffffff8f)
#define TU104_UNLOCK_COLD_WPR2_LO UINT32_C(0x1ffffe00)
#define TU104_UNLOCK_COLD_WPR2_HI UINT32_C(0x00000000)

#define TU104_UNLOCK_BOOTER_NONSECURE_BYTES UINT32_C(0x00000100)
#define TU104_UNLOCK_BOOTER_SECURE_OFFSET UINT32_C(0x00000100)
#define TU104_UNLOCK_BOOTER_SECURE_BYTES UINT32_C(0x00008400)
#define TU104_UNLOCK_BOOTER_DMEM_OFFSET UINT32_C(0x00008500)
#define TU104_UNLOCK_BOOTER_DMEM_BYTES UINT32_C(0x00006200)
#define TU104_UNLOCK_BOOTER_PATCH_DMEM_OFFSET UINT32_C(0x00000200)

#define TU104_UNLOCK_RESET_READS 16u
#define TU104_UNLOCK_SCRUB_POLL_LIMIT 3000u
#define TU104_UNLOCK_HALT_POLL_LIMIT 5000u
#define TU104_UNLOCK_POLL_DELAY_US UINT32_C(1000)
#define TU104_UNLOCK_RESET_DELAY_US UINT32_C(50)
#define TU104_UNLOCK_BOOTER_DELAY_US UINT32_C(3000000)

enum tu104_unlock_status {
    TU104_UNLOCK_OK = 0,
    TU104_UNLOCK_GUARD = 1,
    TU104_UNLOCK_IO = 2,
    TU104_UNLOCK_TIMEOUT = 3,
    TU104_UNLOCK_VERIFY = 4,
    TU104_UNLOCK_LOG = 5
};

_Static_assert((int)TU104_UNLOCK_OK == (int)TU104_FWSEC_PROBE_OK &&
               (int)TU104_UNLOCK_GUARD ==
                   (int)TU104_FWSEC_PROBE_GUARD &&
               (int)TU104_UNLOCK_IO == (int)TU104_FWSEC_PROBE_IO &&
               (int)TU104_UNLOCK_TIMEOUT ==
                   (int)TU104_FWSEC_PROBE_TIMEOUT &&
               (int)TU104_UNLOCK_VERIFY ==
                   (int)TU104_FWSEC_PROBE_VERIFY &&
               (int)TU104_UNLOCK_LOG == (int)TU104_FWSEC_PROBE_LOG,
               "FWSEC/full-unlock status values must remain compatible");

enum tu104_unlock_stage {
    TU104_UNLOCK_STAGE_NONE = 0,
    TU104_UNLOCK_STAGE_FRTS_GUARDS,
    TU104_UNLOCK_STAGE_FRTS_RESET,
    TU104_UNLOCK_STAGE_FRTS_LOADER,
    TU104_UNLOCK_STAGE_FRTS_START,
    TU104_UNLOCK_STAGE_FRTS_RESULT,
    TU104_UNLOCK_STAGE_FRTS_CLEANUP,
    TU104_UNLOCK_STAGE_FRTS_DONE,
    TU104_UNLOCK_STAGE_BOOTER_RESET,
    TU104_UNLOCK_STAGE_BOOTER_LOAD,
    TU104_UNLOCK_STAGE_BOOTER_START,
    TU104_UNLOCK_STAGE_BOOTER_RESULT,
    TU104_UNLOCK_STAGE_CLEANUP,
    TU104_UNLOCK_STAGE_DONE,
    TU104_UNLOCK_STAGE_COUNT
};

_Static_assert((int)TU104_UNLOCK_STAGE_FRTS_GUARDS ==
                   (int)TU104_FWSEC_STAGE_GUARDS &&
               (int)TU104_UNLOCK_STAGE_FRTS_RESET ==
                   (int)TU104_FWSEC_STAGE_RESET &&
               (int)TU104_UNLOCK_STAGE_FRTS_LOADER ==
                   (int)TU104_FWSEC_STAGE_LOADER &&
               (int)TU104_UNLOCK_STAGE_FRTS_START ==
                   (int)TU104_FWSEC_STAGE_START &&
               (int)TU104_UNLOCK_STAGE_FRTS_RESULT ==
                   (int)TU104_FWSEC_STAGE_RESULT &&
               (int)TU104_UNLOCK_STAGE_FRTS_CLEANUP ==
                   (int)TU104_FWSEC_STAGE_CLEANUP &&
               (int)TU104_UNLOCK_STAGE_FRTS_DONE ==
                   (int)TU104_FWSEC_STAGE_DONE,
               "FWSEC/full-unlock stage values must remain compatible");

struct tu104_unlock_core {
    void *opaque;
    int (*read32)(void *opaque, uint32_t offset, uint32_t *value);
    int (*write32)(void *opaque, uint32_t offset, uint32_t value);
    int (*delay_us)(void *opaque, uint32_t microseconds);
    int (*barrier)(void *opaque);
    int (*event)(void *opaque, unsigned stage, uint32_t first,
                 uint32_t second);
    int (*next_input)(void *opaque); /* optional protected-input transition */

    uint8_t *pool;
    size_t pool_bytes;
    uint64_t pool_physical;
    const uint8_t *native_fwsec;
    size_t native_fwsec_bytes;
    const uint8_t *generic_bl;
    size_t generic_bl_bytes;
    const uint8_t *booter;
    size_t booter_bytes;
    const uint8_t *booter_signature;
    size_t booter_signature_bytes;
    const uint8_t *v67;
    size_t v67_bytes;
    const uint8_t *boot_container;
    size_t boot_container_bytes;

    struct tu104_fwsec_probe frts;
    uint32_t prepared_control_crc32;
    unsigned stage;
    int prepared;
    int mutated;
    int fwsec_started;
    int fwsec_verified;
    int booter_started;
    int unlocked;
    int quiescent;
    uint32_t booter_mailbox0;
    uint32_t ss0_result;
    uint32_t ss1_result;
};

static inline int
tu104_unlock_write_allowed(uint32_t offset)
{
    uint32_t base = offset & UINT32_C(0xffff0000);
    uint32_t relative = offset & UINT32_C(0x0000ffff);

    if (base != TU104_GSP_BASE && base != TU104_SEC2_BASE)
        return 0;
    switch (relative) {
    case TU104_UNLOCK_MAILBOX0_OFFSET:
    case TU104_UNLOCK_MAILBOX1_OFFSET:
    case TU104_FALCON_RM_OFFSET:
    case TU104_FALCON_CPUCTL_OFFSET:
    case TU104_FALCON_BOOTVEC_OFFSET:
    case TU104_FALCON_DMACTL_OFFSET:
    case TU104_FALCON_CPUCTL_ALIAS_OFFSET:
    case TU104_FALCON_IMEMC_OFFSET:
    case TU104_FALCON_IMEMD_OFFSET:
    case TU104_FALCON_IMEMT_OFFSET:
    case TU104_FALCON_DMEMC_OFFSET:
    case TU104_FALCON_DMEMD_OFFSET:
    case TU104_FALCON_ENGINE_OFFSET:
    case TU104_FALCON_TRANSCFG4_OFFSET:
    case TU104_FALCON_FBIF_CTL_OFFSET:
        return 1;
    default:
        return 0;
    }
}

static inline int
tu104_unlock_read(struct tu104_unlock_core *core, uint32_t offset,
                  uint32_t *value)
{
    if (core->read32(core->opaque, offset, value))
        return TU104_UNLOCK_IO;
    if (*value == UINT32_C(0xffffffff) ||
        (*value & UINT32_C(0xffff0000)) == UINT32_C(0xbadf0000))
        return TU104_UNLOCK_VERIFY;
    return TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_write(struct tu104_unlock_core *core, uint32_t offset,
                   uint32_t value)
{
    if (!tu104_unlock_write_allowed(offset))
        return TU104_UNLOCK_GUARD;
    core->mutated = 1;
    return core->write32(core->opaque, offset, value) ?
        TU104_UNLOCK_IO : TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_delay(struct tu104_unlock_core *core, uint32_t microseconds)
{
    return core->delay_us(core->opaque, microseconds) ?
        TU104_UNLOCK_IO : TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_barrier(struct tu104_unlock_core *core)
{
    return core->barrier(core->opaque) ?
        TU104_UNLOCK_IO : TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_event(struct tu104_unlock_core *core, unsigned stage,
                   uint32_t first, uint32_t second)
{
    core->stage = stage;
    return core->event(core->opaque, stage, first, second) ?
        TU104_UNLOCK_LOG : TU104_UNLOCK_OK;
}

#define TU104_UNLOCK_TRY(operation)                 \
    do {                                            \
        int tu104_unlock_status = (operation);      \
        if (tu104_unlock_status != TU104_UNLOCK_OK) \
            return tu104_unlock_status;             \
    } while (0)

static inline int
tu104_unlock_frts_read_adapter(void *opaque, uint32_t offset,
                               uint32_t *value)
{
    struct tu104_unlock_core *core = opaque;

    return core->read32(core->opaque, offset, value);
}

static inline int
tu104_unlock_frts_write_adapter(void *opaque, uint32_t offset,
                                uint32_t value)
{
    struct tu104_unlock_core *core = opaque;

    return tu104_unlock_write(core, offset, value) != TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_frts_delay_adapter(void *opaque, uint32_t microseconds)
{
    struct tu104_unlock_core *core = opaque;

    return core->delay_us(core->opaque, microseconds);
}

static inline int
tu104_unlock_frts_barrier_adapter(void *opaque)
{
    struct tu104_unlock_core *core = opaque;

    return core->barrier(core->opaque);
}

static inline int
tu104_unlock_frts_event_adapter(void *opaque, unsigned stage,
                                uint32_t first, uint32_t second)
{
    struct tu104_unlock_core *core = opaque;

    if (stage == TU104_FWSEC_STAGE_NONE ||
        stage >= TU104_FWSEC_STAGE_COUNT)
        return 1;
    return tu104_unlock_event(core, stage, first, second) !=
        TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_prepare(struct tu104_unlock_core *core)
{
    struct tu104_unlock_layout_input layout;
    int status;

    if (core == NULL || core->prepared || core->mutated ||
        core->read32 == NULL || core->write32 == NULL ||
        core->delay_us == NULL || core->barrier == NULL ||
        core->event == NULL || core->pool == NULL ||
        core->native_fwsec == NULL || core->generic_bl == NULL)
        return TU104_UNLOCK_GUARD;

    layout.pool = core->pool;
    layout.pool_bytes = core->pool_bytes;
    layout.pool_physical = core->pool_physical;
    layout.booter = core->booter;
    layout.booter_bytes = core->booter_bytes;
    layout.booter_signature = core->booter_signature;
    layout.booter_signature_bytes = core->booter_signature_bytes;
    layout.v67 = core->v67;
    layout.v67_bytes = core->v67_bytes;
    layout.boot_container = core->boot_container;
    layout.boot_container_bytes = core->boot_container_bytes;
    status = tu104_unlock_layout_prepare(&layout);
    if (status == TU104_UNLOCK_LAYOUT_GUARD)
        return TU104_UNLOCK_GUARD;
    if (status != TU104_UNLOCK_LAYOUT_OK)
        return TU104_UNLOCK_VERIFY;

    core->frts.opaque = core;
    core->frts.read32 = tu104_unlock_frts_read_adapter;
    core->frts.write32 = tu104_unlock_frts_write_adapter;
    core->frts.delay_us = tu104_unlock_frts_delay_adapter;
    core->frts.barrier = tu104_unlock_frts_barrier_adapter;
    core->frts.event = tu104_unlock_frts_event_adapter;
    core->frts.staging = core->pool + TU104_UNLOCK_FWSEC_OFFSET;
    core->frts.staging_bytes = TU104_FWSEC_STAGING_BYTES;
    core->frts.staging_physical =
        core->pool_physical + TU104_UNLOCK_FWSEC_OFFSET;
    core->frts.native_fwsec = core->native_fwsec;
    core->frts.native_fwsec_bytes = core->native_fwsec_bytes;
    core->frts.generic_bl = core->generic_bl;
    core->frts.generic_bl_bytes = core->generic_bl_bytes;
    status = tu104_fwsec_probe_prepare(&core->frts);
    if (status != TU104_FWSEC_PROBE_OK)
        return status;

    core->prepared_control_crc32 = tu104_unlock_layout_crc32(
        core->pool, TU104_UNLOCK_DATA_OFFSET);
    core->stage = TU104_UNLOCK_STAGE_NONE;
    core->prepared = 1;
    core->fwsec_started = 0;
    core->fwsec_verified = 0;
    core->booter_started = 0;
    core->unlocked = 0;
    core->quiescent = 0;
    core->booter_mailbox0 = 0;
    core->ss0_result = 0;
    core->ss1_result = 0;
    return TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_reset_falcon(struct tu104_unlock_core *core, uint32_t base)
{
    uint32_t engine;
    uint32_t dmactl;
    uint32_t hwcfg2;
    unsigned index;

    if (base != TU104_GSP_BASE && base != TU104_SEC2_BASE)
        return TU104_UNLOCK_GUARD;
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, base + TU104_FALCON_ENGINE_OFFSET, 1));
    for (index = 0; index < TU104_UNLOCK_RESET_READS; ++index) {
        TU104_UNLOCK_TRY(tu104_unlock_read(
            core, base + TU104_FALCON_ENGINE_OFFSET, &engine));
        if ((engine & UINT32_C(1)) == 0)
            return TU104_UNLOCK_VERIFY;
    }
    TU104_UNLOCK_TRY(tu104_unlock_delay(
        core, TU104_UNLOCK_RESET_DELAY_US));
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, base + TU104_FALCON_ENGINE_OFFSET, 0));
    for (index = 0; index < TU104_UNLOCK_RESET_READS; ++index) {
        TU104_UNLOCK_TRY(tu104_unlock_read(
            core, base + TU104_FALCON_ENGINE_OFFSET, &engine));
        if ((engine & UINT32_C(1)) != 0)
            return TU104_UNLOCK_VERIFY;
    }

    for (index = 0; index < TU104_UNLOCK_SCRUB_POLL_LIMIT; ++index) {
        TU104_UNLOCK_TRY(tu104_unlock_read(
            core, base + TU104_FALCON_DMACTL_OFFSET, &dmactl));
        TU104_UNLOCK_TRY(tu104_unlock_read(
            core, base + TU104_UNLOCK_HWCFG2_OFFSET, &hwcfg2));
        if ((dmactl & TU104_FALCON_DMACTL_SCRUBBING) == 0 &&
            (hwcfg2 & TU104_UNLOCK_HWCFG2_SCRUBBING) == 0) {
            TU104_UNLOCK_TRY(tu104_unlock_write(
                core, base + TU104_FALCON_RM_OFFSET, TU104_BOOT0));
            return TU104_UNLOCK_OK;
        }
        TU104_UNLOCK_TRY(tu104_unlock_delay(
            core, TU104_UNLOCK_POLL_DELAY_US));
    }
    return TU104_UNLOCK_TIMEOUT;
}

static inline int
tu104_unlock_falcon_context(struct tu104_unlock_core *core, uint32_t base)
{
    uint32_t value;

    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, base + TU104_FALCON_FBIF_CTL_OFFSET, &value));
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, base + TU104_FALCON_FBIF_CTL_OFFSET,
        tu104_falcon_allow_physical_no_context(value)));
    return tu104_unlock_write(
        core, base + TU104_FALCON_DMACTL_OFFSET, 0);
}

static inline int
tu104_unlock_dmem_copy(struct tu104_unlock_core *core, uint32_t base,
                       uint32_t destination, const uint8_t *source,
                       uint32_t bytes)
{
    uint32_t index;

    if ((base != TU104_GSP_BASE && base != TU104_SEC2_BASE) ||
        source == NULL || (destination & UINT32_C(3)) != 0 ||
        (bytes & UINT32_C(3)) != 0)
        return TU104_UNLOCK_GUARD;
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, base + TU104_FALCON_DMEMC_OFFSET,
        destination | TU104_FALCON_MEMC_AINCW));
    for (index = 0; index < bytes; index += 4) {
        TU104_UNLOCK_TRY(tu104_unlock_write(
            core, base + TU104_FALCON_DMEMD_OFFSET,
            tu104_unlock_layout_le32(source + index)));
    }
    return TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_imem_copy(struct tu104_unlock_core *core, uint32_t base,
                       uint32_t destination, const uint8_t *source,
                       uint32_t bytes, uint32_t tag, int secure)
{
    uint32_t index;

    if ((base != TU104_GSP_BASE && base != TU104_SEC2_BASE) ||
        source == NULL || (destination & UINT32_C(0xff)) != 0 ||
        (tag & UINT32_C(0xff)) != 0 || (bytes & UINT32_C(3)) != 0)
        return TU104_UNLOCK_GUARD;
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, base + TU104_FALCON_IMEMC_OFFSET,
        destination | TU104_FALCON_MEMC_AINCW |
            (secure ? TU104_FALCON_IMEMC_SECURE : 0)));
    for (index = 0; index < bytes; index += 4) {
        if ((index & UINT32_C(0xff)) == 0) {
            TU104_UNLOCK_TRY(tu104_unlock_write(
                core, base + TU104_FALCON_IMEMT_OFFSET,
                (tag + index) >> 8));
        }
        TU104_UNLOCK_TRY(tu104_unlock_write(
            core, base + TU104_FALCON_IMEMD_OFFSET,
            tu104_unlock_layout_le32(source + index)));
    }
    return TU104_UNLOCK_OK;
}

static inline int
tu104_unlock_start_falcon(struct tu104_unlock_core *core, uint32_t base)
{
    uint32_t cpu_control;
    uint32_t start_offset;

    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, base + TU104_FALCON_CPUCTL_OFFSET, &cpu_control));
    start_offset = (cpu_control & TU104_FALCON_CPUCTL_ALIAS_ENABLE) ?
        TU104_FALCON_CPUCTL_ALIAS_OFFSET : TU104_FALCON_CPUCTL_OFFSET;
    TU104_UNLOCK_TRY(tu104_unlock_barrier(core));
    if (base == TU104_SEC2_BASE)
        core->booter_started = 1;
    return tu104_unlock_write(
        core, base + start_offset, TU104_FALCON_CPUCTL_START);
}

static inline int
tu104_unlock_wait_for_halt(struct tu104_unlock_core *core, uint32_t base)
{
    uint32_t value;
    unsigned index;

    for (index = 0; index < TU104_UNLOCK_HALT_POLL_LIMIT; ++index) {
        TU104_UNLOCK_TRY(tu104_unlock_read(
            core, base + TU104_FALCON_CPUCTL_OFFSET, &value));
        if ((value & TU104_FALCON_CPUCTL_HALTED) != 0)
            return TU104_UNLOCK_OK;
        TU104_UNLOCK_TRY(tu104_unlock_delay(
            core, TU104_UNLOCK_POLL_DELAY_US));
    }
    return TU104_UNLOCK_TIMEOUT;
}

static inline int
tu104_unlock_load_booter(struct tu104_unlock_core *core)
{
    const uint8_t *booter =
        core->pool + TU104_UNLOCK_BOOTER_OFFSET;
    uint32_t value;

    TU104_UNLOCK_TRY(tu104_unlock_falcon_context(
        core, TU104_SEC2_BASE));
    TU104_UNLOCK_TRY(tu104_unlock_imem_copy(
        core, TU104_SEC2_BASE, 0, booter,
        TU104_UNLOCK_BOOTER_NONSECURE_BYTES, 0, 0));
    TU104_UNLOCK_TRY(tu104_unlock_imem_copy(
        core, TU104_SEC2_BASE, TU104_UNLOCK_BOOTER_SECURE_OFFSET,
        booter + TU104_UNLOCK_BOOTER_SECURE_OFFSET,
        TU104_UNLOCK_BOOTER_SECURE_BYTES,
        TU104_UNLOCK_BOOTER_SECURE_OFFSET, 1));
    TU104_UNLOCK_TRY(tu104_unlock_dmem_copy(
        core, TU104_SEC2_BASE, 0,
        booter + TU104_UNLOCK_BOOTER_DMEM_OFFSET,
        TU104_UNLOCK_BOOTER_DMEM_BYTES));

    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_FALCON_IMEMC_OFFSET, 0));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_SEC2_BASE + TU104_FALCON_IMEMD_OFFSET, &value));
    if (value != tu104_unlock_layout_le32(booter))
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_FALCON_DMEMC_OFFSET,
        TU104_UNLOCK_BOOTER_PATCH_DMEM_OFFSET));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_SEC2_BASE + TU104_FALCON_DMEMD_OFFSET, &value));
    if (value != tu104_unlock_layout_le32(core->booter_signature))
        return TU104_UNLOCK_VERIFY;

    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_FALCON_BOOTVEC_OFFSET, 0));
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_UNLOCK_MAILBOX0_OFFSET,
        (uint32_t)(core->pool_physical + TU104_UNLOCK_META_OFFSET)));
    return tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_UNLOCK_MAILBOX1_OFFSET,
        (uint32_t)((core->pool_physical + TU104_UNLOCK_META_OFFSET) >> 32));
}

static inline int
tu104_unlock_run(struct tu104_unlock_core *core)
{
    uint32_t value;
    uint32_t wpr2_lo;
    uint32_t wpr2_hi;
    int status;

    if (core == NULL || !core->prepared || core->mutated ||
        core->stage != TU104_UNLOCK_STAGE_NONE ||
        core->prepared_control_crc32 != tu104_unlock_layout_crc32(
            core->pool, TU104_UNLOCK_DATA_OFFSET))
        return TU104_UNLOCK_GUARD;

    status = tu104_fwsec_probe_run(&core->frts);
    core->fwsec_started = core->frts.firmware_started;
    core->fwsec_verified = core->frts.result_verified;
    if (status != TU104_FWSEC_PROBE_OK)
        return status;

    TU104_UNLOCK_TRY(tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_BOOTER_RESET, 0, 0));
    TU104_UNLOCK_TRY(tu104_unlock_reset_falcon(core, TU104_GSP_BASE));
    TU104_UNLOCK_TRY(tu104_unlock_reset_falcon(core, TU104_SEC2_BASE));
    if (core->next_input != NULL && core->next_input(core->opaque))
        return TU104_UNLOCK_IO;

    TU104_UNLOCK_TRY(tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_BOOTER_LOAD, 0, 0));
    TU104_UNLOCK_TRY(tu104_unlock_load_booter(core));
    TU104_UNLOCK_TRY(tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_BOOTER_START,
        (uint32_t)(core->pool_physical + TU104_UNLOCK_META_OFFSET),
        (uint32_t)((core->pool_physical + TU104_UNLOCK_META_OFFSET) >> 32)));
    TU104_UNLOCK_TRY(tu104_unlock_start_falcon(core, TU104_SEC2_BASE));
    TU104_UNLOCK_TRY(tu104_unlock_delay(
        core, TU104_UNLOCK_BOOTER_DELAY_US));
    TU104_UNLOCK_TRY(tu104_unlock_wait_for_halt(core, TU104_SEC2_BASE));

    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_SEC2_BASE + TU104_UNLOCK_MAILBOX0_OFFSET,
        &core->booter_mailbox0));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_SS0_OFFSET, &core->ss0_result));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_SS1_OFFSET, &core->ss1_result));
    TU104_UNLOCK_TRY(tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_BOOTER_RESULT,
        core->ss0_result, core->ss1_result));
    if (core->booter_mailbox0 != 0 ||
        core->ss0_result != TU104_UNLOCK_SS0_RESULT ||
        core->ss1_result != TU104_UNLOCK_SS1_RESULT)
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_PLM_OFFSET, &value));
    if (value != TU104_UNLOCK_PLM_RESULT)
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_WPR2_LO_OFFSET, &wpr2_lo));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_WPR2_HI_OFFSET, &wpr2_hi));
    if (wpr2_lo != TU104_UNLOCK_COLD_WPR2_LO ||
        wpr2_hi != TU104_UNLOCK_COLD_WPR2_HI)
        return TU104_UNLOCK_VERIFY;
    core->unlocked = 1;

    TU104_UNLOCK_TRY(tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_CLEANUP, 0, 0));
    TU104_UNLOCK_TRY(tu104_unlock_reset_falcon(core, TU104_SEC2_BASE));
    TU104_UNLOCK_TRY(tu104_unlock_reset_falcon(core, TU104_GSP_BASE));
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_UNLOCK_MAILBOX0_OFFSET, 0));
    TU104_UNLOCK_TRY(tu104_unlock_write(
        core, TU104_SEC2_BASE + TU104_UNLOCK_MAILBOX1_OFFSET, 0));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_SEC2_BASE + TU104_FALCON_CPUCTL_OFFSET, &value));
    if ((value & TU104_FALCON_CPUCTL_START) != 0)
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_GSP_BASE + TU104_FALCON_CPUCTL_OFFSET, &value));
    if ((value & TU104_FALCON_CPUCTL_START) != 0)
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_SS0_OFFSET, &core->ss0_result));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_SS1_OFFSET, &core->ss1_result));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_UNLOCK_PLM_OFFSET, &value));
    if (core->ss0_result != TU104_UNLOCK_SS0_RESULT ||
        core->ss1_result != TU104_UNLOCK_SS1_RESULT ||
        value != TU104_UNLOCK_PLM_RESULT)
        return TU104_UNLOCK_VERIFY;
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_WPR2_LO_OFFSET, &wpr2_lo));
    TU104_UNLOCK_TRY(tu104_unlock_read(
        core, TU104_WPR2_HI_OFFSET, &wpr2_hi));
    if (wpr2_lo != TU104_UNLOCK_COLD_WPR2_LO ||
        wpr2_hi != TU104_UNLOCK_COLD_WPR2_HI)
        return TU104_UNLOCK_VERIFY;

    core->quiescent = 1;
    return tu104_unlock_event(
        core, TU104_UNLOCK_STAGE_DONE,
        core->ss0_result, core->ss1_result);
}

#undef TU104_UNLOCK_TRY

#endif
