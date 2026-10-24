/*
 * RTX 2070 SUPER / SM75 Tensor Core issue-rate validator.
 *
 * Uses only the CUDA Driver API and driver JIT: no CUDA Toolkit, runtime, or
 * cuBLAS dependency.  The kernel performs non-zero FP16 MMA with FP32
 * accumulation, validates the exact result, records per-SM clock64 cycles,
 * and refuses every GPU except the pinned 40-SM SM75 device at 01:00.0.
 * It performs no management, clock, firmware, PCI, or register writes.
 */

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int CUresult;
typedef int CUdevice;
typedef uint64_t CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUevent_st *CUevent;
typedef struct CUstream_st *CUstream;

enum {
    CUDA_SUCCESS = 0,
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76
};

extern CUresult cuInit(unsigned flags);
extern CUresult cuDriverGetVersion(int *version);
extern CUresult cuDeviceGetByPCIBusId(CUdevice *device, const char *pci_id);
extern CUresult cuDeviceGetName(char *name, int length, CUdevice device);
extern CUresult cuDeviceGetAttribute(int *value, int attribute,
                                     CUdevice device);
extern CUresult cuCtxCreate_v2(CUcontext *context, unsigned flags,
                               CUdevice device);
extern CUresult cuCtxDestroy_v2(CUcontext context);
extern CUresult cuModuleLoadData(CUmodule *module, const void *image);
extern CUresult cuModuleUnload(CUmodule module);
extern CUresult cuModuleGetFunction(CUfunction *function, CUmodule module,
                                    const char *name);
extern CUresult cuMemAlloc_v2(CUdeviceptr *pointer, size_t bytes);
extern CUresult cuMemFree_v2(CUdeviceptr pointer);
extern CUresult cuMemcpyDtoH_v2(void *destination, CUdeviceptr source,
                                size_t bytes);
extern CUresult cuEventCreate(CUevent *event, unsigned flags);
extern CUresult cuEventDestroy_v2(CUevent event);
extern CUresult cuEventRecord(CUevent event, CUstream stream);
extern CUresult cuEventSynchronize(CUevent event);
extern CUresult cuEventElapsedTime(float *milliseconds, CUevent start,
                                   CUevent end);
extern CUresult cuLaunchKernel(CUfunction function,
                               unsigned grid_x, unsigned grid_y,
                               unsigned grid_z, unsigned block_x,
                               unsigned block_y, unsigned block_z,
                               unsigned shared_bytes, CUstream stream,
                               void **arguments, void **extra);
extern CUresult cuGetErrorName(CUresult error, const char **name);
extern CUresult cuGetErrorString(CUresult error, const char **description);

#define TARGET_BDF "0000:01:00.0"
#define TARGET_SMS 40
#define TARGET_THREADS 1024
#define TARGET_ROUNDS 2048u
#define OUTPUTS_PER_THREAD 16u
#define SAMPLES 7
#define WARMUPS 2

static void cuda_fail(CUresult result, const char *operation,
                      const char *file, int line)
{
    const char *name = "unknown";
    const char *description = "unknown";

    cuGetErrorName(result, &name);
    cuGetErrorString(result, &description);
    fprintf(stderr, "%s:%d: %s failed: %s (%s, code %d)\n",
            file, line, operation, name, description, result);
    exit(1);
}

#define CUDA(operation)                                                   \
    do {                                                                  \
        CUresult cuda_result = (operation);                               \
        if (cuda_result != CUDA_SUCCESS)                                  \
            cuda_fail(cuda_result, #operation, __FILE__, __LINE__);       \
    } while (0)

static void append_text(char *buffer, size_t capacity, size_t *length,
                        const char *text)
{
    size_t bytes = strlen(text);

    if (*length > capacity || bytes >= capacity - *length) {
        fprintf(stderr, "PTX construction overflow\n");
        exit(1);
    }
    memcpy(buffer + *length, text, bytes);
    *length += bytes;
    buffer[*length] = '\0';
}

static void append_mma_group(char *ptx, size_t capacity, size_t *length)
{
    static const char body[] =
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f0,%f1,%f2,%f3}, {%r6,%r6}, {%r6}, "
        "{%f0,%f1,%f2,%f3};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f4,%f5,%f6,%f7}, {%r6,%r6}, {%r6}, "
        "{%f4,%f5,%f6,%f7};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f8,%f9,%f10,%f11}, {%r6,%r6}, {%r6}, "
        "{%f8,%f9,%f10,%f11};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f12,%f13,%f14,%f15}, {%r6,%r6}, {%r6}, "
        "{%f12,%f13,%f14,%f15};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f0,%f1,%f2,%f3}, {%r6,%r6}, {%r7}, "
        "{%f0,%f1,%f2,%f3};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f4,%f5,%f6,%f7}, {%r6,%r6}, {%r7}, "
        "{%f4,%f5,%f6,%f7};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f8,%f9,%f10,%f11}, {%r6,%r6}, {%r7}, "
        "{%f8,%f9,%f10,%f11};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f12,%f13,%f14,%f15}, {%r6,%r6}, {%r7}, "
        "{%f12,%f13,%f14,%f15};\n";

    append_text(ptx, capacity, length, body);
}

static char *build_ptx(void)
{
    static const char header[] =
        ".version 7.0\n"
        ".target sm_75\n"
        ".address_size 64\n\n"
        ".visible .entry issue_rate(\n"
        "  .param .u64 p_out,\n"
        "  .param .u64 p_ticks,\n"
        "  .param .u64 p_smids,\n"
        "  .param .u32 p_rounds,\n"
        "  .param .u32 p_one,\n"
        "  .param .u32 p_negone\n"
        ")\n"
        "{\n"
        "  .reg .pred %p<3>;\n"
        "  .reg .b32 %r<10>;\n"
        "  .reg .b64 %rd<12>;\n"
        "  .reg .f32 %f<16>;\n"
        "  ld.param.u64 %rd0, [p_out];\n"
        "  ld.param.u64 %rd1, [p_ticks];\n"
        "  ld.param.u64 %rd2, [p_smids];\n"
        "  ld.param.u32 %r5, [p_rounds];\n"
        "  ld.param.u32 %r6, [p_one];\n"
        "  ld.param.u32 %r7, [p_negone];\n"
        "  mov.u32 %r0, %tid.x;\n"
        "  mov.u32 %r1, %ctaid.x;\n"
        "  mov.u32 %r2, %ntid.x;\n"
        "  mad.lo.u32 %r3, %r1, %r2, %r0;\n"
        "  mov.f32 %f0, 0f00000000;\n"
        "  mov.f32 %f1, 0f00000000;\n"
        "  mov.f32 %f2, 0f00000000;\n"
        "  mov.f32 %f3, 0f00000000;\n"
        "  mov.f32 %f4, 0f00000000;\n"
        "  mov.f32 %f5, 0f00000000;\n"
        "  mov.f32 %f6, 0f00000000;\n"
        "  mov.f32 %f7, 0f00000000;\n"
        "  mov.f32 %f8, 0f00000000;\n"
        "  mov.f32 %f9, 0f00000000;\n"
        "  mov.f32 %f10, 0f00000000;\n"
        "  mov.f32 %f11, 0f00000000;\n"
        "  mov.f32 %f12, 0f00000000;\n"
        "  mov.f32 %f13, 0f00000000;\n"
        "  mov.f32 %f14, 0f00000000;\n"
        "  mov.f32 %f15, 0f00000000;\n"
        "  bar.sync 0;\n"
        "  mov.u64 %rd3, %clock64;\n"
        "outer_loop:\n";
    static const char final_mma[] =
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f0,%f1,%f2,%f3}, {%r6,%r6}, {%r6}, "
        "{%f0,%f1,%f2,%f3};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f4,%f5,%f6,%f7}, {%r6,%r6}, {%r6}, "
        "{%f4,%f5,%f6,%f7};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f8,%f9,%f10,%f11}, {%r6,%r6}, {%r6}, "
        "{%f8,%f9,%f10,%f11};\n"
        "  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%f12,%f13,%f14,%f15}, {%r6,%r6}, {%r6}, "
        "{%f12,%f13,%f14,%f15};\n";
    static const char footer[] =
        "  sub.u32 %r5, %r5, 1;\n"
        "  setp.ne.u32 %p0, %r5, 0;\n"
        "  @%p0 bra outer_loop;\n"
        "FINAL_MMA_PLACEHOLDER"
        "  bar.sync 0;\n"
        "  mov.u64 %rd4, %clock64;\n"
        "  setp.ne.u32 %p1, %r0, 0;\n"
        "  @%p1 bra skip_metadata;\n"
        "  sub.u64 %rd4, %rd4, %rd3;\n"
        "  mul.wide.u32 %rd5, %r1, 8;\n"
        "  add.u64 %rd5, %rd1, %rd5;\n"
        "  st.global.u64 [%rd5], %rd4;\n"
        "  mov.u32 %r8, %smid;\n"
        "  mul.wide.u32 %rd6, %r1, 4;\n"
        "  add.u64 %rd6, %rd2, %rd6;\n"
        "  st.global.u32 [%rd6], %r8;\n"
        "skip_metadata:\n"
        "  mul.wide.u32 %rd7, %r3, 64;\n"
        "  add.u64 %rd7, %rd0, %rd7;\n"
        "  st.global.f32 [%rd7+0], %f0;\n"
        "  st.global.f32 [%rd7+4], %f1;\n"
        "  st.global.f32 [%rd7+8], %f2;\n"
        "  st.global.f32 [%rd7+12], %f3;\n"
        "  st.global.f32 [%rd7+16], %f4;\n"
        "  st.global.f32 [%rd7+20], %f5;\n"
        "  st.global.f32 [%rd7+24], %f6;\n"
        "  st.global.f32 [%rd7+28], %f7;\n"
        "  st.global.f32 [%rd7+32], %f8;\n"
        "  st.global.f32 [%rd7+36], %f9;\n"
        "  st.global.f32 [%rd7+40], %f10;\n"
        "  st.global.f32 [%rd7+44], %f11;\n"
        "  st.global.f32 [%rd7+48], %f12;\n"
        "  st.global.f32 [%rd7+52], %f13;\n"
        "  st.global.f32 [%rd7+56], %f14;\n"
        "  st.global.f32 [%rd7+60], %f15;\n"
        "  ret;\n"
        "}\n";
    const char placeholder[] = "FINAL_MMA_PLACEHOLDER";
    const char *marker;
    char *ptx = calloc(1, 65536);
    size_t length = 0;
    unsigned repeat;

    if (!ptx) {
        perror("calloc PTX");
        exit(1);
    }
    append_text(ptx, 65536, &length, header);
    for (repeat = 0; repeat < 8; ++repeat)
        append_mma_group(ptx, 65536, &length);
    marker = strstr(footer, placeholder);
    if (!marker) {
        fprintf(stderr, "internal PTX placeholder missing\n");
        exit(1);
    }
    {
        char prefix[512];
        size_t prefix_bytes = (size_t)(marker - footer);

        if (prefix_bytes >= sizeof(prefix)) {
            fprintf(stderr, "internal PTX footer prefix overflow\n");
            exit(1);
        }
        memcpy(prefix, footer, prefix_bytes);
        prefix[prefix_bytes] = '\0';
        append_text(ptx, 65536, &length, prefix);
    }
    append_text(ptx, 65536, &length, final_mma);
    append_text(ptx, 65536, &length, marker + strlen(placeholder));
    return ptx;
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t first = *(const uint64_t *)left;
    uint64_t second = *(const uint64_t *)right;

    return first > second ? 1 : (first < second ? -1 : 0);
}

static int compare_u32(const void *left, const void *right)
{
    uint32_t first = *(const uint32_t *)left;
    uint32_t second = *(const uint32_t *)right;

    return first > second ? 1 : (first < second ? -1 : 0);
}

int main(void)
{
    const size_t threads = (size_t)TARGET_SMS * TARGET_THREADS;
    const size_t output_count = threads * OUTPUTS_PER_THREAD;
    const size_t output_bytes = output_count * sizeof(float);
    const size_t tick_bytes = TARGET_SMS * sizeof(uint64_t);
    const size_t smid_bytes = TARGET_SMS * sizeof(uint32_t);
    CUdevice device;
    CUcontext context = NULL;
    CUmodule module = NULL;
    CUfunction function = NULL;
    CUdeviceptr device_output = 0;
    CUdeviceptr device_ticks = 0;
    CUdeviceptr device_smids = 0;
    CUevent start = NULL;
    CUevent end = NULL;
    float *output = calloc(output_count, sizeof(*output));
    uint64_t ticks[TARGET_SMS];
    uint32_t smids[TARGET_SMS];
    char name[256];
    char *ptx;
    int driver_version = 0;
    int major = 0;
    int minor = 0;
    int sms = 0;
    int max_threads = 0;
    int clock_rate_khz = 0;
    unsigned rounds = TARGET_ROUNDS;
    unsigned one = UINT32_C(0x3c003c00);
    unsigned negone = UINT32_C(0xbc00bc00);
    void *arguments[] = {
        &device_output, &device_ticks, &device_smids,
        &rounds, &one, &negone
    };
    int sample;

    if (!output) {
        perror("calloc output");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    CUDA(cuInit(0));
    CUDA(cuDriverGetVersion(&driver_version));
    CUDA(cuDeviceGetByPCIBusId(&device, TARGET_BDF));
    CUDA(cuDeviceGetName(name, (int)sizeof(name), device));
    CUDA(cuDeviceGetAttribute(&major,
        CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device));
    CUDA(cuDeviceGetAttribute(&minor,
        CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device));
    CUDA(cuDeviceGetAttribute(&sms,
        CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device));
    CUDA(cuDeviceGetAttribute(&max_threads,
        CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, device));
    CUDA(cuDeviceGetAttribute(&clock_rate_khz,
        CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device));
    if (major != 7 || minor != 5 || sms != TARGET_SMS ||
        max_threads < TARGET_THREADS ||
        strcmp(name, "NVIDIA GeForce RTX 2070 SUPER") != 0) {
        fprintf(stderr, "Refusing non-target GPU: name=%s sm=%d.%d "
                "sms=%d max_threads=%d\n", name, major, minor,
                sms, max_threads);
        return 1;
    }

    CUDA(cuCtxCreate_v2(&context, 0, device));
    ptx = build_ptx();
    CUDA(cuModuleLoadData(&module, ptx));
    free(ptx);
    CUDA(cuModuleGetFunction(&function, module, "issue_rate"));
    CUDA(cuMemAlloc_v2(&device_output, output_bytes));
    CUDA(cuMemAlloc_v2(&device_ticks, tick_bytes));
    CUDA(cuMemAlloc_v2(&device_smids, smid_bytes));
    CUDA(cuEventCreate(&start, 0));
    CUDA(cuEventCreate(&end, 0));

    printf("{\"gpu\":\"%s\",\"bdf\":\"%s\",\"sms\":%d,"
           "\"sm\":%d%d,\"driver_api\":%d,\"threads_per_block\":%d,"
           "\"rounds\":%u,\"reported_clock_khz\":%d}\n",
           name, TARGET_BDF, sms, major, minor, driver_version,
           TARGET_THREADS, rounds, clock_rate_khz);

    for (sample = -WARMUPS; sample < SAMPLES; ++sample) {
        float milliseconds = 0.0f;
        float maximum_error = 0.0f;
        size_t index;
        size_t unique_sms = 0;
        double flops_per_sm;
        double tflops;
        double flops_per_sm_cycle;

        CUDA(cuEventRecord(start, NULL));
        CUDA(cuLaunchKernel(function,
                            TARGET_SMS, 1, 1,
                            TARGET_THREADS, 1, 1,
                            0, NULL, arguments, NULL));
        CUDA(cuEventRecord(end, NULL));
        CUDA(cuEventSynchronize(end));
        CUDA(cuEventElapsedTime(&milliseconds, start, end));
        CUDA(cuMemcpyDtoH_v2(output, device_output, output_bytes));
        CUDA(cuMemcpyDtoH_v2(ticks, device_ticks, tick_bytes));
        CUDA(cuMemcpyDtoH_v2(smids, device_smids, smid_bytes));

        for (index = 0; index < output_count; ++index) {
            float error = fabsf(output[index] - 8.0f);

            if (!isfinite(output[index])) {
                fprintf(stderr, "non-finite Tensor result at %zu\n", index);
                return 2;
            }
            if (error > maximum_error)
                maximum_error = error;
        }
        if (maximum_error != 0.0f) {
            fprintf(stderr, "exact Tensor result mismatch: %.9g\n",
                    maximum_error);
            return 2;
        }
        qsort(ticks, TARGET_SMS, sizeof(ticks[0]), compare_u64);
        qsort(smids, TARGET_SMS, sizeof(smids[0]), compare_u32);
        for (index = 0; index < TARGET_SMS; ++index) {
            if (index == 0 || smids[index] != smids[index - 1])
                ++unique_sms;
        }
        if (unique_sms != TARGET_SMS) {
            fprintf(stderr, "expected one block per SM: %zu/%d unique\n",
                    unique_sms, TARGET_SMS);
            return 2;
        }

        flops_per_sm = ((double)rounds * 64.0 + 4.0) * 32.0 * 2048.0;
        tflops = flops_per_sm * TARGET_SMS /
            ((double)milliseconds * 1.0e9);
        flops_per_sm_cycle = flops_per_sm /
            (double)ticks[TARGET_SMS / 2];
        if (sample >= 0) {
            printf("{\"test\":\"mma_f16_acc_fp32\",\"sample\":%d,"
                   "\"ms\":%.6f,\"tflops\":%.4f,"
                   "\"median_sm_cycles\":%" PRIu64 ","
                   "\"flops_per_sm_cycle\":%.4f,"
                   "\"max_abs_error\":%.1f,\"unique_sms\":%zu}\n",
                   sample, milliseconds, tflops,
                   ticks[TARGET_SMS / 2], flops_per_sm_cycle,
                   maximum_error, unique_sms);
        }
    }

    CUDA(cuEventDestroy_v2(start));
    CUDA(cuEventDestroy_v2(end));
    CUDA(cuMemFree_v2(device_output));
    CUDA(cuMemFree_v2(device_ticks));
    CUDA(cuMemFree_v2(device_smids));
    CUDA(cuModuleUnload(module));
    CUDA(cuCtxDestroy_v2(context));
    free(output);
    return 0;
}
