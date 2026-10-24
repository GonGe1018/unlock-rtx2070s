/*
 * RTX 2070 SUPER cuBLAS FP16-input / FP32-accumulation GEMM benchmark.
 *
 * The program uses the CUDA Driver API for allocation and timing, and loads
 * cuBLAS at runtime so building it does not require the CUDA Toolkit headers.
 * It benchmarks square 4096 and 8192 GEMMs with FP16 inputs, FP32 output and
 * CUBLAS_COMPUTE_32F.  The executable refuses every GPU except the pinned
 * 40-SM SM75 device at 01:00.0 and performs no management, clock, firmware,
 * PCI, or register writes.
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int CUresult;
typedef int CUdevice;
typedef uint64_t CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUevent_st *CUevent;
typedef struct CUstream_st *CUstream;

enum {
    CUDA_SUCCESS = 0,
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
extern CUresult cuCtxSynchronize(void);
extern CUresult cuMemAlloc_v2(CUdeviceptr *pointer, size_t bytes);
extern CUresult cuMemFree_v2(CUdeviceptr pointer);
extern CUresult cuMemsetD16_v2(CUdeviceptr destination, unsigned short value,
                               size_t count);
extern CUresult cuMemsetD32_v2(CUdeviceptr destination, unsigned value,
                               size_t count);
extern CUresult cuMemcpyDtoH_v2(void *destination, CUdeviceptr source,
                                size_t bytes);
extern CUresult cuEventCreate(CUevent *event, unsigned flags);
extern CUresult cuEventDestroy_v2(CUevent event);
extern CUresult cuEventRecord(CUevent event, CUstream stream);
extern CUresult cuEventSynchronize(CUevent event);
extern CUresult cuEventElapsedTime(float *milliseconds, CUevent start,
                                   CUevent end);
extern CUresult cuGetErrorName(CUresult error, const char **name);
extern CUresult cuGetErrorString(CUresult error, const char **description);

typedef void *cublasHandle_t;
typedef int cublasStatus_t;

typedef cublasStatus_t (*cublasCreate_fn)(cublasHandle_t *handle);
typedef cublasStatus_t (*cublasDestroy_fn)(cublasHandle_t handle);
typedef cublasStatus_t (*cublasGetVersion_fn)(cublasHandle_t handle,
                                              int *version);
typedef cublasStatus_t (*cublasSetMathMode_fn)(cublasHandle_t handle,
                                               int mode);
typedef cublasStatus_t (*cublasGemmEx_fn)(
    cublasHandle_t handle, int transa, int transb, int m, int n, int k,
    const void *alpha, const void *a, int a_type, int lda, const void *b,
    int b_type, int ldb, const void *beta, void *c, int c_type, int ldc,
    int compute_type, int algorithm);

enum {
    CUBLAS_STATUS_SUCCESS = 0,
    CUBLAS_OP_N = 0,
    CUDA_R_32F = 0,
    CUDA_R_16F = 2,
    CUBLAS_TENSOR_OP_MATH = 1,
    CUBLAS_COMPUTE_32F = 68,
    CUBLAS_GEMM_DEFAULT_TENSOR_OP = 99
};

#define TARGET_BDF "0000:01:00.0"
#define TARGET_SMS 40
#define TARGET_CC_MAJOR 7
#define TARGET_CC_MINOR 5
#define WARMUP_SECONDS 2.0
#define BATCHES 7
#define OPERATIONS_PER_BATCH 30
#define CHECK_VALUES 4

struct cublas_api {
    void *library;
    cublasCreate_fn create;
    cublasDestroy_fn destroy;
    cublasGetVersion_fn get_version;
    cublasSetMathMode_fn set_math_mode;
    cublasGemmEx_fn gemm_ex;
};

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

static const char *cublas_status_name(cublasStatus_t status)
{
    switch (status) {
    case 0:
        return "CUBLAS_STATUS_SUCCESS";
    case 1:
        return "CUBLAS_STATUS_NOT_INITIALIZED";
    case 3:
        return "CUBLAS_STATUS_ALLOC_FAILED";
    case 7:
        return "CUBLAS_STATUS_INVALID_VALUE";
    case 8:
        return "CUBLAS_STATUS_ARCH_MISMATCH";
    case 11:
        return "CUBLAS_STATUS_MAPPING_ERROR";
    case 13:
        return "CUBLAS_STATUS_EXECUTION_FAILED";
    case 14:
        return "CUBLAS_STATUS_INTERNAL_ERROR";
    case 15:
        return "CUBLAS_STATUS_NOT_SUPPORTED";
    case 16:
        return "CUBLAS_STATUS_LICENSE_ERROR";
    default:
        return "CUBLAS_STATUS_UNKNOWN";
    }
}

static void cublas_fail(cublasStatus_t status, const char *operation,
                        const char *file, int line)
{
    fprintf(stderr, "%s:%d: %s failed: %s (code %d)\n",
            file, line, operation, cublas_status_name(status), status);
    exit(1);
}

#define CUBLAS(operation)                                                 \
    do {                                                                  \
        cublasStatus_t cublas_result = (operation);                       \
        if (cublas_result != CUBLAS_STATUS_SUCCESS)                       \
            cublas_fail(cublas_result, #operation, __FILE__, __LINE__);   \
    } while (0)

static void *load_symbol(void *library, const char *name)
{
    void *symbol;
    const char *error;

    dlerror();
    symbol = dlsym(library, name);
    error = dlerror();
    if (error) {
        fprintf(stderr, "dlsym(%s) failed: %s\n", name, error);
        exit(1);
    }
    return symbol;
}

static struct cublas_api load_cublas(void)
{
    const char *configured = getenv("CUBLAS_LIBRARY");
    const char *candidates[] = {"libcublas.so.12", "libcublas.so", NULL};
    struct cublas_api api = {0};
    const char *error = NULL;
    size_t index;

    if (configured && configured[0] != '\0') {
        api.library = dlopen(configured, RTLD_NOW | RTLD_LOCAL);
        if (!api.library)
            error = dlerror();
    } else {
        for (index = 0; candidates[index]; ++index) {
            api.library = dlopen(candidates[index], RTLD_NOW | RTLD_LOCAL);
            if (api.library)
                break;
            error = dlerror();
        }
    }
    if (!api.library) {
        fprintf(stderr,
                "unable to load cuBLAS: %s\n"
                "Install libcublas12 or set CUBLAS_LIBRARY to "
                "libcublas.so.12.\n",
                error ? error : "no loader error available");
        exit(1);
    }

    *(void **)(&api.create) = load_symbol(api.library, "cublasCreate_v2");
    *(void **)(&api.destroy) = load_symbol(api.library, "cublasDestroy_v2");
    *(void **)(&api.get_version) =
        load_symbol(api.library, "cublasGetVersion_v2");
    *(void **)(&api.set_math_mode) =
        load_symbol(api.library, "cublasSetMathMode");
    *(void **)(&api.gemm_ex) = load_symbol(api.library, "cublasGemmEx");
    return api;
}

static int compare_double(const void *left, const void *right)
{
    double first = *(const double *)left;
    double second = *(const double *)right;

    return (first > second) - (first < second);
}

static double monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1.0e9;
}

static void run_gemm(const struct cublas_api *api, cublasHandle_t handle,
                     CUdeviceptr a, CUdeviceptr b, CUdeviceptr c, int n)
{
    const float alpha = 1.0f;
    const float beta = 0.0f;

    CUBLAS(api->gemm_ex(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n,
                        &alpha, (const void *)(uintptr_t)a, CUDA_R_16F, n,
                        (const void *)(uintptr_t)b, CUDA_R_16F, n, &beta,
                        (void *)(uintptr_t)c, CUDA_R_32F, n,
                        CUBLAS_COMPUTE_32F,
                        CUBLAS_GEMM_DEFAULT_TENSOR_OP));
}

static void benchmark_size(const struct cublas_api *api,
                           cublasHandle_t handle, int n)
{
    const size_t elements = (size_t)n * (size_t)n;
    const size_t input_bytes = elements * sizeof(uint16_t);
    const size_t output_bytes = elements * sizeof(float);
    const double operations = 2.0 * (double)n * (double)n * (double)n;
    CUdeviceptr a = 0;
    CUdeviceptr b = 0;
    CUdeviceptr c = 0;
    CUevent start = NULL;
    CUevent end = NULL;
    double tflops[BATCHES];
    double milliseconds[BATCHES];
    double sorted_tflops[BATCHES];
    double sorted_milliseconds[BATCHES];
    float checks[CHECK_VALUES];
    size_t check_indices[CHECK_VALUES];
    double max_abs_error = 0.0;
    double warmup_start;
    double warmup_elapsed;
    unsigned warmup_operations = 0;
    int batch;
    int iteration;
    int check;

    CUDA(cuMemAlloc_v2(&a, input_bytes));
    CUDA(cuMemAlloc_v2(&b, input_bytes));
    CUDA(cuMemAlloc_v2(&c, output_bytes));
    CUDA(cuMemsetD16_v2(a, 0x3c00u, elements));
    CUDA(cuMemsetD16_v2(b, 0x3c00u, elements));
    CUDA(cuMemsetD32_v2(c, 0u, elements));
    CUDA(cuEventCreate(&start, 0));
    CUDA(cuEventCreate(&end, 0));

    warmup_start = monotonic_seconds();
    do {
        run_gemm(api, handle, a, b, c, n);
        CUDA(cuCtxSynchronize());
        ++warmup_operations;
        warmup_elapsed = monotonic_seconds() - warmup_start;
    } while (warmup_elapsed < WARMUP_SECONDS);
    printf("{\"phase\":\"warmup\",\"n\":%d,"
           "\"operations\":%u,\"elapsed_s\":%.6f}\n",
           n, warmup_operations, warmup_elapsed);
    fflush(stdout);

    for (batch = 0; batch < BATCHES; ++batch) {
        float elapsed_ms;

        CUDA(cuEventRecord(start, NULL));
        for (iteration = 0; iteration < OPERATIONS_PER_BATCH; ++iteration)
            run_gemm(api, handle, a, b, c, n);
        CUDA(cuEventRecord(end, NULL));
        CUDA(cuEventSynchronize(end));
        CUDA(cuEventElapsedTime(&elapsed_ms, start, end));

        milliseconds[batch] =
            (double)elapsed_ms / (double)OPERATIONS_PER_BATCH;
        tflops[batch] = operations / (milliseconds[batch] * 1.0e9);
        printf("{\"test\":\"cublas_gemm_f16_f32acc\","
               "\"n\":%d,\"batch\":%d,\"operations\":%d,"
               "\"ms_per_gemm\":%.6f,\"tflops\":%.6f}\n",
               n, batch, OPERATIONS_PER_BATCH, milliseconds[batch],
               tflops[batch]);
        fflush(stdout);
    }

    check_indices[0] = 0;
    check_indices[1] = (size_t)n - 1u;
    check_indices[2] = elements - (size_t)n;
    check_indices[3] = elements - 1u;
    for (check = 0; check < CHECK_VALUES; ++check) {
        CUDA(cuMemcpyDtoH_v2(&checks[check],
                             c + check_indices[check] * sizeof(float),
                             sizeof(float)));
        if (fabs((double)checks[check] - (double)n) > max_abs_error)
            max_abs_error = fabs((double)checks[check] - (double)n);
    }
    if (max_abs_error != 0.0) {
        fprintf(stderr,
                "GEMM correctness check failed for n=%d: max error %.9g\n",
                n, max_abs_error);
        exit(1);
    }

    memcpy(sorted_tflops, tflops, sizeof(tflops));
    memcpy(sorted_milliseconds, milliseconds, sizeof(milliseconds));
    qsort(sorted_tflops, BATCHES, sizeof(sorted_tflops[0]), compare_double);
    qsort(sorted_milliseconds, BATCHES, sizeof(sorted_milliseconds[0]),
          compare_double);
    printf("{\"summary\":\"cublas_gemm_f16_f32acc\","
           "\"n\":%d,\"median_ms\":%.6f,\"median_tflops\":%.6f,"
           "\"min_tflops\":%.6f,\"max_tflops\":%.6f,"
           "\"checked_outputs\":%d,\"max_abs_error\":%.1f}\n",
           n, sorted_milliseconds[BATCHES / 2],
           sorted_tflops[BATCHES / 2], sorted_tflops[0],
           sorted_tflops[BATCHES - 1], CHECK_VALUES, max_abs_error);
    fflush(stdout);

    CUDA(cuEventDestroy_v2(end));
    CUDA(cuEventDestroy_v2(start));
    CUDA(cuMemFree_v2(c));
    CUDA(cuMemFree_v2(b));
    CUDA(cuMemFree_v2(a));
}

static const char *parse_label(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--label") != 0 ||
        (strcmp(argv[2], "locked") != 0 &&
         strcmp(argv[2], "unlocked") != 0)) {
        fprintf(stderr, "usage: %s --label locked|unlocked\n", argv[0]);
        exit(2);
    }
    return argv[2];
}

int main(int argc, char **argv)
{
    const char *label = parse_label(argc, argv);
    struct cublas_api api;
    cublasHandle_t handle = NULL;
    CUcontext context = NULL;
    CUdevice device;
    char name[128] = {0};
    int driver_version = 0;
    int cublas_version = 0;
    int sm_count = 0;
    int cc_major = 0;
    int cc_minor = 0;
    int clock_khz = 0;

    CUDA(cuInit(0));
    CUDA(cuDriverGetVersion(&driver_version));
    CUDA(cuDeviceGetByPCIBusId(&device, TARGET_BDF));
    CUDA(cuDeviceGetName(name, (int)sizeof(name), device));
    CUDA(cuDeviceGetAttribute(&sm_count,
                              CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT,
                              device));
    CUDA(cuDeviceGetAttribute(&cc_major,
                              CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                              device));
    CUDA(cuDeviceGetAttribute(&cc_minor,
                              CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                              device));
    CUDA(cuDeviceGetAttribute(&clock_khz, CU_DEVICE_ATTRIBUTE_CLOCK_RATE,
                              device));
    if (sm_count != TARGET_SMS || cc_major != TARGET_CC_MAJOR ||
        cc_minor != TARGET_CC_MINOR ||
        strcmp(name, "NVIDIA GeForce RTX 2070 SUPER") != 0) {
        fprintf(stderr,
                "refusing unexpected GPU at %s: name=%s SMs=%d cc=%d.%d\n",
                TARGET_BDF, name, sm_count, cc_major, cc_minor);
        return 1;
    }

    CUDA(cuCtxCreate_v2(&context, 0, device));
    api = load_cublas();
    CUBLAS(api.create(&handle));
    CUBLAS(api.get_version(handle, &cublas_version));
    CUBLAS(api.set_math_mode(handle, CUBLAS_TENSOR_OP_MATH));

    printf("{\"schema\":\"rtx2070s-cublas-gemm/1\","
           "\"label\":\"%s\",\"gpu\":\"%s\","
           "\"bdf\":\"%s\",\"sms\":%d,\"sm\":%d,"
           "\"driver_api\":%d,\"cublas_version\":%d,"
           "\"reported_clock_khz\":%d,\"input\":\"FP16\","
           "\"output\":\"FP32\",\"accumulation\":\"FP32\","
           "\"warmup_seconds\":%.1f,\"batches\":%d,"
           "\"operations_per_batch\":%d}\n",
           label, name, TARGET_BDF, sm_count, cc_major * 10 + cc_minor,
           driver_version, cublas_version, clock_khz, WARMUP_SECONDS, BATCHES,
           OPERATIONS_PER_BATCH);
    fflush(stdout);

    benchmark_size(&api, handle, 4096);
    benchmark_size(&api, handle, 8192);

    CUBLAS(api.destroy(handle));
    dlclose(api.library);
    CUDA(cuCtxDestroy_v2(context));
    return 0;
}
