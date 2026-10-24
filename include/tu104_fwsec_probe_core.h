#ifndef TU104_FWSEC_PROBE_CORE_H
#define TU104_FWSEC_PROBE_CORE_H

/*
 * Host-testable, FRTS-only TU104 FWSEC execution core.
 *
 * All platform access is injected.  This file does not allocate memory,
 * discover PCI devices, or perform MMIO by itself.  The future UEFI wrapper
 * must retain the staging pages and stop in UEFI after every non-success
 * result once `mutated` is set.  Returning to firmware or freeing the pages
 * in that state is forbidden because a failed MMIO write may still have
 * reached the GPU.
 */

#include <stddef.h>
#include <stdint.h>

#include "tu104_falcon_profile.h"
#include "tu104_fwsec_loader.h"

#define TU104_FWSEC_STORED_BYTES UINT32_C(0x00009df0)
#define TU104_FWSEC_NATIVE_CRC32 UINT32_C(0x1587385c)
#define TU104_GENERIC_BL_CRC32 UINT32_C(0xc4b89955)
#define TU104_FWSEC_PATCHED_STAGING_CRC32 UINT32_C(0x5dfd79e0)

#define TU104_PROM_WINDOW UINT32_C(0x00300000)
#define TU104_FWSEC_DESCRIPTOR_PROM_OFFSET UINT32_C(0x0003e3f8)
#define TU104_FWSEC_IMAGE_PROM_OFFSET UINT32_C(0x0003e434)
#define TU104_FWSEC_DESCRIPTOR_MMIO \
    (TU104_PROM_WINDOW + TU104_FWSEC_DESCRIPTOR_PROM_OFFSET)
#define TU104_FWSEC_IMAGE_MMIO \
    (TU104_PROM_WINDOW + TU104_FWSEC_IMAGE_PROM_OFFSET)

#define TU104_FWSEC_INTERFACE_OFFSET UINT32_C(0x000000e0)
#define TU104_FWSEC_MAPPER_OFFSET UINT32_C(0x00000360)
#define TU104_FWSEC_INIT_COMMAND_OFFSET UINT32_C(0x0000038c)
#define TU104_FWSEC_COMMAND_OFFSET UINT32_C(0x000003b0)
#define TU104_FWSEC_COMMAND_CAPACITY UINT32_C(0x00000040)
#define TU104_FWSEC_FRTS_COMMAND_BYTES UINT32_C(0x00000030)
#define TU104_FWSEC_FRTS_COMMAND UINT32_C(0x00000015)

#define TU104_FB_BYTES UINT64_C(0x0000000200000000)
#define TU104_FRTS_OFFSET UINT64_C(0x00000001ffee0000)
#define TU104_FRTS_OFFSET_4K UINT32_C(0x001ffee0)
#define TU104_FRTS_SIZE_4K UINT32_C(0x00000100)
#define TU104_FRTS_MEDIA_FB UINT32_C(2)
#define TU104_WPR2_EXPECTED_LO UINT32_C(0x01ffee00)
#define TU104_WPR2_EXPECTED_HI UINT32_C(0x01fffc00)

#define TU104_BOOT0 UINT32_C(0x164000a1)
#define TU104_WPR2_LO_OFFSET UINT32_C(0x001fa824)
#define TU104_WPR2_HI_OFFSET UINT32_C(0x001fa828)
#define TU104_FRTS_SCRATCH_OFFSET UINT32_C(0x00001438)

#define TU104_FALCON_RM_OFFSET UINT32_C(0x0084)
#define TU104_FALCON_CPUCTL_OFFSET UINT32_C(0x0100)
#define TU104_FALCON_BOOTVEC_OFFSET UINT32_C(0x0104)
#define TU104_FALCON_HWCFG_OFFSET UINT32_C(0x0108)
#define TU104_FALCON_DMACTL_OFFSET UINT32_C(0x010c)
#define TU104_FALCON_CPUCTL_ALIAS_OFFSET UINT32_C(0x0130)
#define TU104_FALCON_IMEMC_OFFSET UINT32_C(0x0180)
#define TU104_FALCON_IMEMD_OFFSET UINT32_C(0x0184)
#define TU104_FALCON_IMEMT_OFFSET UINT32_C(0x0188)
#define TU104_FALCON_DMEMC_OFFSET UINT32_C(0x01c0)
#define TU104_FALCON_DMEMD_OFFSET UINT32_C(0x01c4)
#define TU104_FALCON_ENGINE_OFFSET UINT32_C(0x03c0)
#define TU104_FALCON_TRANSCFG4_OFFSET UINT32_C(0x0610)
#define TU104_FALCON_FBIF_CTL_OFFSET UINT32_C(0x0624)

#define TU104_FALCON_CPUCTL_START UINT32_C(0x00000002)
#define TU104_FALCON_CPUCTL_HALTED UINT32_C(0x00000010)
#define TU104_FALCON_CPUCTL_ALIAS_ENABLE UINT32_C(0x00000040)
#define TU104_FALCON_DMACTL_SCRUBBING UINT32_C(0x00000006)
#define TU104_FALCON_MEMC_AINCW UINT32_C(0x01000000)
#define TU104_FALCON_IMEMC_SECURE UINT32_C(0x10000000)

#define TU104_RESET_PROPAGATION_READS 10u
#define TU104_SCRUB_POLL_LIMIT 3000u
#define TU104_HALT_POLL_LIMIT 5000u
#define TU104_POLL_DELAY_US UINT32_C(1000)
#define TU104_POST_START_DELAY_US UINT32_C(100000)

enum tu104_fwsec_probe_status {
    TU104_FWSEC_PROBE_OK = 0,
    TU104_FWSEC_PROBE_GUARD = 1,
    TU104_FWSEC_PROBE_IO = 2,
    TU104_FWSEC_PROBE_TIMEOUT = 3,
    TU104_FWSEC_PROBE_VERIFY = 4,
    TU104_FWSEC_PROBE_LOG = 5
};

enum tu104_fwsec_probe_stage {
    TU104_FWSEC_STAGE_NONE = 0,
    TU104_FWSEC_STAGE_GUARDS,
    TU104_FWSEC_STAGE_RESET,
    TU104_FWSEC_STAGE_LOADER,
    TU104_FWSEC_STAGE_START,
    TU104_FWSEC_STAGE_RESULT,
    TU104_FWSEC_STAGE_CLEANUP,
    TU104_FWSEC_STAGE_DONE,
    TU104_FWSEC_STAGE_COUNT
};

struct tu104_fwsec_probe {
    void *opaque;
    int (*read32)(void *opaque, uint32_t offset, uint32_t *value);
    int (*write32)(void *opaque, uint32_t offset, uint32_t value);
    int (*delay_us)(void *opaque, uint32_t microseconds);
    int (*barrier)(void *opaque);
    int (*event)(void *opaque, unsigned stage, uint32_t first,
                 uint32_t second);

    uint8_t *staging;
    size_t staging_bytes;
    uint64_t staging_physical;
    const uint8_t *native_fwsec;
    size_t native_fwsec_bytes;
    const uint8_t *generic_bl;
    size_t generic_bl_bytes;

    unsigned stage;
    int prepared;
    int mutated;
    int firmware_started;
    int result_verified;
    int quiescent;
    uint32_t scratch_result;
    uint32_t wpr2_lo_result;
    uint32_t wpr2_hi_result;
};

static inline uint32_t tu104_fwsec_probe_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline void
tu104_fwsec_probe_put32(uint8_t *bytes, uint32_t value)
{
    unsigned index;

    for (index = 0; index < 4; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static inline void
tu104_fwsec_probe_put64(uint8_t *bytes, uint64_t value)
{
    tu104_fwsec_probe_put32(bytes, (uint32_t)value);
    tu104_fwsec_probe_put32(bytes + 4, (uint32_t)(value >> 32));
}

static inline void
tu104_fwsec_probe_copy(uint8_t *destination, const uint8_t *source,
                       size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index)
        destination[index] = source[index];
}

static inline void tu104_fwsec_probe_zero(uint8_t *data, size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index)
        data[index] = 0;
}

static inline uint32_t
tu104_fwsec_probe_crc32(const uint8_t *data, size_t bytes)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;

    if (data == NULL)
        return 0;
    for (index = 0; index < bytes; ++index) {
        unsigned bit;

        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                (UINT32_C(0xedb88320) &
                 (uint32_t)-(int32_t)(crc & UINT32_C(1)));
    }
    return ~crc;
}

static inline int
tu104_fwsec_probe_native_layout_matches(const uint8_t *native_fwsec,
                                        size_t bytes)
{
    const uint8_t *dmem;
    const uint8_t *interface;
    const uint8_t *mapper;

    if (native_fwsec == NULL || bytes != TU104_FWSEC_STORED_BYTES)
        return 0;
    dmem = native_fwsec + TU104_FWSEC_IMEM_BYTES;
    interface = dmem + TU104_FWSEC_INTERFACE_OFFSET;
    mapper = dmem + TU104_FWSEC_MAPPER_OFFSET;

    if (interface[0] != 1 || interface[1] != 4 ||
        interface[2] != 8 || interface[3] != 2)
        return 0;
    if (tu104_fwsec_probe_le32(interface + 4) != UINT32_C(4) ||
        tu104_fwsec_probe_le32(interface + 8) !=
            TU104_FWSEC_MAPPER_OFFSET)
        return 0;
    if (tu104_fwsec_probe_le32(mapper) != UINT32_C(0x50414d44) ||
        tu104_fwsec_probe_le32(mapper + 4) != UINT32_C(0x00400003) ||
        tu104_fwsec_probe_le32(mapper + 8) !=
            TU104_FWSEC_COMMAND_OFFSET ||
        tu104_fwsec_probe_le32(mapper + 12) !=
            TU104_FWSEC_COMMAND_CAPACITY)
        return 0;
    if (TU104_FWSEC_MAPPER_OFFSET + UINT32_C(0x2c) !=
            TU104_FWSEC_INIT_COMMAND_OFFSET ||
        TU104_FWSEC_COMMAND_OFFSET + TU104_FWSEC_FRTS_COMMAND_BYTES >
            TU104_FWSEC_DMEM_BYTES)
        return 0;
    return 1;
}

static inline int
tu104_fwsec_probe_write_allowed(uint32_t offset)
{
    uint32_t relative;

    if ((offset & UINT32_C(0xffff0000)) != TU104_GSP_BASE)
        return 0;
    relative = offset & UINT32_C(0x0000ffff);
    switch (relative) {
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
tu104_fwsec_probe_event(struct tu104_fwsec_probe *probe, unsigned stage,
                        uint32_t first, uint32_t second)
{
    probe->stage = stage;
    return probe->event(probe->opaque, stage, first, second) ?
        TU104_FWSEC_PROBE_LOG : TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_read(struct tu104_fwsec_probe *probe, uint32_t offset,
                       uint32_t *value)
{
    if (probe->read32(probe->opaque, offset, value))
        return TU104_FWSEC_PROBE_IO;
    if (*value == UINT32_C(0xffffffff) ||
        (*value & UINT32_C(0xffff0000)) == UINT32_C(0xbadf0000))
        return TU104_FWSEC_PROBE_VERIFY;
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_read_raw(struct tu104_fwsec_probe *probe, uint32_t offset,
                           uint32_t *value)
{
    return probe->read32(probe->opaque, offset, value) ?
        TU104_FWSEC_PROBE_IO : TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_write(struct tu104_fwsec_probe *probe, uint32_t offset,
                        uint32_t value)
{
    if (!tu104_fwsec_probe_write_allowed(offset))
        return TU104_FWSEC_PROBE_GUARD;
    probe->mutated = 1;
    return probe->write32(probe->opaque, offset, value) ?
        TU104_FWSEC_PROBE_IO : TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_delay(struct tu104_fwsec_probe *probe, uint32_t usec)
{
    return probe->delay_us(probe->opaque, usec) ?
        TU104_FWSEC_PROBE_IO : TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_barrier(struct tu104_fwsec_probe *probe)
{
    return probe->barrier(probe->opaque) ?
        TU104_FWSEC_PROBE_IO : TU104_FWSEC_PROBE_OK;
}

#define TU104_FWSEC_TRY(operation)                 \
    do {                                           \
        int tu104_fwsec_status = (operation);      \
        if (tu104_fwsec_status != TU104_FWSEC_PROBE_OK) \
            return tu104_fwsec_status;             \
    } while (0)

static inline int
tu104_fwsec_probe_prepare(struct tu104_fwsec_probe *probe)
{
    uint8_t *dmem;
    uint8_t command[TU104_FWSEC_FRTS_COMMAND_BYTES];
    size_t index;

    if (probe == NULL || probe->mutated || probe->read32 == NULL ||
        probe->write32 == NULL || probe->delay_us == NULL ||
        probe->barrier == NULL || probe->event == NULL ||
        probe->staging == NULL || probe->native_fwsec == NULL ||
        probe->generic_bl == NULL ||
        probe->staging_bytes != TU104_FWSEC_STAGING_BYTES ||
        probe->native_fwsec_bytes != TU104_FWSEC_STORED_BYTES ||
        probe->generic_bl_bytes != TU104_GENERIC_BL_IMAGE_BYTES ||
        !tu104_fwsec_staging_geometry_suitable(
            probe->staging_physical, probe->staging_bytes))
        return TU104_FWSEC_PROBE_GUARD;
    if (tu104_fwsec_probe_crc32(probe->native_fwsec,
                                probe->native_fwsec_bytes) !=
            TU104_FWSEC_NATIVE_CRC32 ||
        tu104_fwsec_probe_crc32(probe->generic_bl,
                                probe->generic_bl_bytes) !=
            TU104_GENERIC_BL_CRC32 ||
        !tu104_fwsec_probe_native_layout_matches(
            probe->native_fwsec, probe->native_fwsec_bytes))
        return TU104_FWSEC_PROBE_GUARD;

    tu104_fwsec_probe_zero(probe->staging, probe->staging_bytes);
    tu104_fwsec_probe_copy(probe->staging, probe->native_fwsec,
                           probe->native_fwsec_bytes);
    dmem = probe->staging + TU104_FWSEC_IMEM_BYTES;
    tu104_fwsec_probe_put32(dmem + TU104_FWSEC_INIT_COMMAND_OFFSET,
                            TU104_FWSEC_FRTS_COMMAND);

    tu104_fwsec_probe_zero(command, sizeof(command));
    tu104_fwsec_probe_put32(command + 0, UINT32_C(1));
    tu104_fwsec_probe_put32(command + 4, UINT32_C(24));
    tu104_fwsec_probe_put64(command + 8, UINT64_C(0));
    tu104_fwsec_probe_put32(command + 16, UINT32_C(0));
    tu104_fwsec_probe_put32(command + 20, UINT32_C(2));
    tu104_fwsec_probe_put32(command + 24, UINT32_C(1));
    tu104_fwsec_probe_put32(command + 28, UINT32_C(20));
    tu104_fwsec_probe_put32(command + 32, TU104_FRTS_OFFSET_4K);
    tu104_fwsec_probe_put32(command + 36, TU104_FRTS_SIZE_4K);
    tu104_fwsec_probe_put32(command + 40, TU104_FRTS_MEDIA_FB);
    tu104_fwsec_probe_copy(dmem + TU104_FWSEC_COMMAND_OFFSET,
                           command, sizeof(command));

    if (tu104_fwsec_probe_crc32(probe->staging,
                                probe->staging_bytes) !=
        TU104_FWSEC_PATCHED_STAGING_CRC32)
        return TU104_FWSEC_PROBE_VERIFY;
    for (index = probe->native_fwsec_bytes;
         index < probe->staging_bytes; ++index) {
        if (probe->staging[index] != 0)
            return TU104_FWSEC_PROBE_VERIFY;
    }

    probe->stage = TU104_FWSEC_STAGE_NONE;
    probe->prepared = 1;
    probe->firmware_started = 0;
    probe->result_verified = 0;
    probe->quiescent = 0;
    probe->scratch_result = 0;
    probe->wpr2_lo_result = 0;
    probe->wpr2_hi_result = 0;
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_control_snapshot(struct tu104_fwsec_probe *probe)
{
    static const uint32_t offsets[] = {
        UINT32_C(0x00000000), UINT32_C(0x00409650),
        UINT32_C(0x00409660), UINT32_C(0x00409664),
        UINT32_C(0x00409668), UINT32_C(0x0040966c),
        TU104_WPR2_LO_OFFSET, TU104_WPR2_HI_OFFSET,
        UINT32_C(0x00118234), UINT32_C(0x00625f04),
        TU104_FRTS_SCRATCH_OFFSET,
    };
    static const uint32_t expected[] = {
        TU104_BOOT0, UINT32_C(0xffffff8f), UINT32_C(0x000002f3),
        UINT32_C(0x00000000), UINT32_C(0x00400080),
        UINT32_C(0x00000006), UINT32_C(0x1ffffe00),
        UINT32_C(0x00000000), UINT32_C(0x000003ff),
        UINT32_C(0x00020e09), UINT32_C(0x00000000),
    };
    const uint32_t *falcon_expected[2] = {
        tu104_gsp_initial_values, tu104_sec2_initial_values
    };
    const uint32_t falcon_bases[2] = { TU104_GSP_BASE, TU104_SEC2_BASE };
    uint32_t value;
    size_t index;
    size_t engine;

    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        TU104_FWSEC_TRY(tu104_fwsec_probe_read(probe, offsets[index],
                                               &value));
        if (value != expected[index])
            return TU104_FWSEC_PROBE_GUARD;
    }
    for (engine = 0; engine < 2; ++engine) {
        for (index = 0; index < TU104_FALCON_FIELD_COUNT; ++index) {
            TU104_FWSEC_TRY(tu104_fwsec_probe_read(
                probe, falcon_bases[engine] +
                    tu104_falcon_relative_offsets[index], &value));
            if (value != falcon_expected[engine][index])
                return TU104_FWSEC_PROBE_GUARD;
        }
    }
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_guards(struct tu104_fwsec_probe *probe)
{
    static const uint32_t descriptor[15] = {
        UINT32_C(0x003c0201), UINT32_C(0x00009df0),
        UINT32_C(0x00009df0), UINT32_C(0x00000000),
        UINT32_C(0x000000e0), UINT32_C(0x00000000),
        UINT32_C(0x00009a00), UINT32_C(0x00000000),
        UINT32_C(0x00000400), UINT32_C(0x00009600),
        UINT32_C(0x00009a00), UINT32_C(0x00000000),
        UINT32_C(0x000003f0), UINT32_C(0x00009a00),
        UINT32_C(0x00006638),
    };
    uint32_t value;
    size_t index;

    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_GUARDS, 0, 0));
    TU104_FWSEC_TRY(tu104_fwsec_probe_control_snapshot(probe));

    for (index = 0; index < sizeof(descriptor) / sizeof(descriptor[0]);
         ++index) {
        TU104_FWSEC_TRY(tu104_fwsec_probe_read_raw(
            probe, TU104_FWSEC_DESCRIPTOR_MMIO + (uint32_t)(index * 4),
            &value));
        if (value != descriptor[index])
            return TU104_FWSEC_PROBE_GUARD;
    }
    for (index = 0; index < TU104_FWSEC_STORED_BYTES; index += 4) {
        TU104_FWSEC_TRY(tu104_fwsec_probe_read_raw(
            probe, TU104_FWSEC_IMAGE_MMIO + (uint32_t)index, &value));
        if (value != tu104_fwsec_probe_le32(probe->native_fwsec + index))
            return TU104_FWSEC_PROBE_GUARD;
    }

    /* Re-pin all mutable control state immediately before the first write. */
    return tu104_fwsec_probe_control_snapshot(probe);
}

static inline int
tu104_fwsec_probe_reset_gsp(struct tu104_fwsec_probe *probe)
{
    uint32_t value = 0;
    unsigned index;

    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_ENGINE_OFFSET, 1));
    for (index = 0; index < TU104_RESET_PROPAGATION_READS; ++index)
        TU104_FWSEC_TRY(tu104_fwsec_probe_read(
            probe, TU104_GSP_BASE + TU104_FALCON_ENGINE_OFFSET, &value));
    if ((value & UINT32_C(1)) == 0)
        return TU104_FWSEC_PROBE_VERIFY;

    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_ENGINE_OFFSET, 0));
    for (index = 0; index < TU104_RESET_PROPAGATION_READS; ++index)
        TU104_FWSEC_TRY(tu104_fwsec_probe_read(
            probe, TU104_GSP_BASE + TU104_FALCON_ENGINE_OFFSET, &value));
    if ((value & UINT32_C(1)) != 0)
        return TU104_FWSEC_PROBE_VERIFY;

    for (index = 0; index < TU104_SCRUB_POLL_LIMIT; ++index) {
        TU104_FWSEC_TRY(tu104_fwsec_probe_read(
            probe, TU104_GSP_BASE + TU104_FALCON_DMACTL_OFFSET, &value));
        if ((value & TU104_FALCON_DMACTL_SCRUBBING) == 0) {
            TU104_FWSEC_TRY(tu104_fwsec_probe_write(
                probe, TU104_GSP_BASE + TU104_FALCON_RM_OFFSET,
                TU104_BOOT0));
            return TU104_FWSEC_PROBE_OK;
        }
        TU104_FWSEC_TRY(tu104_fwsec_probe_delay(
            probe, TU104_POLL_DELAY_US));
    }
    return TU104_FWSEC_PROBE_TIMEOUT;
}

static inline int
tu104_fwsec_probe_dmem_copy(struct tu104_fwsec_probe *probe,
                            uint32_t destination, const uint8_t *source,
                            uint32_t bytes)
{
    uint32_t index;

    if (source == NULL || (destination & UINT32_C(3)) != 0 ||
        (bytes & UINT32_C(3)) != 0)
        return TU104_FWSEC_PROBE_GUARD;
    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_DMEMC_OFFSET,
        destination | TU104_FALCON_MEMC_AINCW));
    for (index = 0; index < bytes; index += 4)
        TU104_FWSEC_TRY(tu104_fwsec_probe_write(
            probe, TU104_GSP_BASE + TU104_FALCON_DMEMD_OFFSET,
            tu104_fwsec_probe_le32(source + index)));
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_imem_copy(struct tu104_fwsec_probe *probe,
                            uint32_t destination, const uint8_t *source,
                            uint32_t bytes, uint32_t tag, int secure)
{
    uint32_t index;

    if (source == NULL || (destination & UINT32_C(0xff)) != 0 ||
        (tag & UINT32_C(0xff)) != 0 || (bytes & UINT32_C(3)) != 0)
        return TU104_FWSEC_PROBE_GUARD;
    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_IMEMC_OFFSET,
        destination | TU104_FALCON_MEMC_AINCW |
            (secure ? TU104_FALCON_IMEMC_SECURE : 0)));
    for (index = 0; index < bytes; index += 4) {
        if ((index & UINT32_C(0xff)) == 0)
            TU104_FWSEC_TRY(tu104_fwsec_probe_write(
                probe, TU104_GSP_BASE + TU104_FALCON_IMEMT_OFFSET,
                (tag + index) >> 8));
        TU104_FWSEC_TRY(tu104_fwsec_probe_write(
            probe, TU104_GSP_BASE + TU104_FALCON_IMEMD_OFFSET,
            tu104_fwsec_probe_le32(source + index)));
    }
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_load(struct tu104_fwsec_probe *probe)
{
    struct tu104_fwsec_loader_desc descriptor;
    uint32_t value;
    uint32_t destination;

    if (!tu104_fwsec_loader_desc_init_contiguous(
            &descriptor, probe->staging_physical, probe->staging_bytes))
        return TU104_FWSEC_PROBE_GUARD;

    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_FBIF_CTL_OFFSET, &value));
    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_FBIF_CTL_OFFSET,
        tu104_falcon_allow_physical_no_context(value)));
    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_DMACTL_OFFSET, 0));

    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_TRANSCFG4_OFFSET, &value));
    if (value != UINT32_C(0x00000110))
        return TU104_FWSEC_PROBE_GUARD;
    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_TRANSCFG4_OFFSET,
        tu104_falcon_loader_transcfg(value)));

    TU104_FWSEC_TRY(tu104_fwsec_probe_dmem_copy(
        probe, 0, (const uint8_t *)&descriptor,
        (uint32_t)sizeof(descriptor)));

    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_HWCFG_OFFSET, &value));
    if (value != UINT32_C(0x40420100))
        return TU104_FWSEC_PROBE_GUARD;
    destination = tu104_generic_bl_imem_destination(value);
    if (destination != TU104_GENERIC_BL_EXPECTED_DEST)
        return TU104_FWSEC_PROBE_GUARD;
    TU104_FWSEC_TRY(tu104_fwsec_probe_imem_copy(
        probe, destination, probe->generic_bl,
        TU104_GENERIC_BL_CODE_BYTES, TU104_GENERIC_BL_BOOT_VECTOR, 0));

    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_IMEMC_OFFSET, destination));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_IMEMD_OFFSET, &value));
    if (value != tu104_fwsec_probe_le32(probe->generic_bl))
        return TU104_FWSEC_PROBE_VERIFY;

    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_DMEMC_OFFSET,
        (uint32_t)offsetof(struct tu104_fwsec_loader_desc, ctx_dma)));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_DMEMD_OFFSET, &value));
    if (value != TU104_FWSEC_LOADER_CTX_DMA)
        return TU104_FWSEC_PROBE_VERIFY;

    TU104_FWSEC_TRY(tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + TU104_FALCON_BOOTVEC_OFFSET,
        TU104_GENERIC_BL_BOOT_VECTOR));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_BOOTVEC_OFFSET, &value));
    if (value != TU104_GENERIC_BL_BOOT_VECTOR)
        return TU104_FWSEC_PROBE_VERIFY;
    return TU104_FWSEC_PROBE_OK;
}

static inline int
tu104_fwsec_probe_start(struct tu104_fwsec_probe *probe)
{
    uint32_t cpu_control;
    uint32_t start_offset;

    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_CPUCTL_OFFSET,
        &cpu_control));
    start_offset = (cpu_control & TU104_FALCON_CPUCTL_ALIAS_ENABLE) ?
        TU104_FALCON_CPUCTL_ALIAS_OFFSET : TU104_FALCON_CPUCTL_OFFSET;
    TU104_FWSEC_TRY(tu104_fwsec_probe_barrier(probe));
    probe->firmware_started = 1;
    return tu104_fwsec_probe_write(
        probe, TU104_GSP_BASE + start_offset, TU104_FALCON_CPUCTL_START);
}

static inline int
tu104_fwsec_probe_wait_for_halt(struct tu104_fwsec_probe *probe)
{
    uint32_t value;
    unsigned index;

    for (index = 0; index < TU104_HALT_POLL_LIMIT; ++index) {
        TU104_FWSEC_TRY(tu104_fwsec_probe_read(
            probe, TU104_GSP_BASE + TU104_FALCON_CPUCTL_OFFSET, &value));
        if ((value & TU104_FALCON_CPUCTL_HALTED) != 0)
            return TU104_FWSEC_PROBE_OK;
        TU104_FWSEC_TRY(tu104_fwsec_probe_delay(
            probe, TU104_POLL_DELAY_US));
    }
    return TU104_FWSEC_PROBE_TIMEOUT;
}

static inline int tu104_fwsec_probe_run(struct tu104_fwsec_probe *probe)
{
    uint32_t cpu_control;

    if (probe == NULL || !probe->prepared || probe->mutated ||
        probe->stage != TU104_FWSEC_STAGE_NONE ||
        tu104_fwsec_probe_crc32(probe->native_fwsec,
                                probe->native_fwsec_bytes) !=
            TU104_FWSEC_NATIVE_CRC32 ||
        tu104_fwsec_probe_crc32(probe->generic_bl,
                                probe->generic_bl_bytes) !=
            TU104_GENERIC_BL_CRC32 ||
        tu104_fwsec_probe_crc32(probe->staging,
                                probe->staging_bytes) !=
            TU104_FWSEC_PATCHED_STAGING_CRC32)
        return TU104_FWSEC_PROBE_GUARD;

    TU104_FWSEC_TRY(tu104_fwsec_probe_guards(probe));
    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_RESET, 0, 0));
    TU104_FWSEC_TRY(tu104_fwsec_probe_reset_gsp(probe));

    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_LOADER, 0, 0));
    TU104_FWSEC_TRY(tu104_fwsec_probe_load(probe));

    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_START, TU104_GENERIC_BL_BOOT_VECTOR,
        TU104_FRTS_OFFSET_4K));
    TU104_FWSEC_TRY(tu104_fwsec_probe_start(probe));
    TU104_FWSEC_TRY(tu104_fwsec_probe_delay(
        probe, TU104_POST_START_DELAY_US));
    TU104_FWSEC_TRY(tu104_fwsec_probe_wait_for_halt(probe));

    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_FRTS_SCRATCH_OFFSET, &probe->scratch_result));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_WPR2_LO_OFFSET, &probe->wpr2_lo_result));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_WPR2_HI_OFFSET, &probe->wpr2_hi_result));
    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_RESULT, probe->wpr2_lo_result,
        probe->wpr2_hi_result));
    if ((probe->scratch_result >> 16) != 0 ||
        probe->wpr2_lo_result != TU104_WPR2_EXPECTED_LO ||
        probe->wpr2_hi_result != TU104_WPR2_EXPECTED_HI)
        return TU104_FWSEC_PROBE_VERIFY;
    probe->result_verified = 1;

    TU104_FWSEC_TRY(tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_CLEANUP, 0, 0));
    TU104_FWSEC_TRY(tu104_fwsec_probe_reset_gsp(probe));
    TU104_FWSEC_TRY(tu104_fwsec_probe_read(
        probe, TU104_GSP_BASE + TU104_FALCON_CPUCTL_OFFSET,
        &cpu_control));
    if ((cpu_control & TU104_FALCON_CPUCTL_START) != 0)
        return TU104_FWSEC_PROBE_VERIFY;
    probe->quiescent = 1;
    return tu104_fwsec_probe_event(
        probe, TU104_FWSEC_STAGE_DONE, probe->wpr2_lo_result,
        probe->wpr2_hi_result);
}

#undef TU104_FWSEC_TRY

#endif
