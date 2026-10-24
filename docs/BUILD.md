# Build guide

[Overview](../README.md) · [Reproduction](REPRODUCING.md) · [Safety](SAFETY.md)

This guide covers dependencies, external inputs, host tests, and build targets. For complete report-reproduction workflows, start with [Reproducing the report](REPRODUCING.md).

Building and running host tests do not access the GPU. The benchmark programs use the installed GPU driver when run. Booting an active UEFI image changes GPU state and is covered separately in [Safety](SAFETY.md).

## Dependencies

| Component | Tested environment |
| --- | --- |
| OS | Ubuntu 24.04 x86-64 |
| Compiler | GCC 13.3.0 |
| Binary tools | GNU binutils 2.42 |
| UEFI library | GNU-EFI 3.0.15 |
| Report and parser tools | Python 3 |

On Debian/Ubuntu, the build dependencies are provided by `build-essential`, `binutils`, `gnu-efi`, `python3`, and `curl`. Once dependencies are installed, fetching inputs and building require no root privileges.

Both benchmarks link to `libcuda.so` from the installed NVIDIA driver. The Tensor MMA benchmark needs no CUDA Toolkit or `nvcc`; the cuBLAS benchmark also needs a cuBLAS runtime library.

## Fetch external inputs

Firmware and third-party payload binaries are not stored in this repository. The retrieval script downloads only exact files from their original public repositories and rejects any byte mismatch:

```sh
make fetch-build-inputs
```

The NVIDIA inputs come from the official `NVIDIA/open-gpu-kernel-modules` tag `610.57.04`. The V67 and boot-container inputs come from `xrip/cmp50hx-unlock` commit `df2fe11b370bb52cc314576d0f2f5ae47ce908f8`. Exact URLs, input hashes, derived asset hashes, and licensing boundaries are in [input-manifest.json](../results/input-manifest.json) and [NOTICE.md](../NOTICE.md).

The script writes under ignored `references/` paths. It never executes an upstream installer or downloaded program.

## Run the public host tests

```sh
make test-public
```

This runs source safety audits, synthetic policy/state-machine tests, exact asset-layout tests, lifecycle tests, handoff-profile tests, and 17 Python parser/profile/result tests. It performs no PCI, MMIO, DMA, firmware, or boot access.

## Run the full target tests

The two largest suites inject simulated I/O failures and require the target card's exact 512 KiB PROM capture as a test fixture. The capture remains private. Two independent captures must match; the tested fixture has SHA-256:

`df1b9144a31dda10e0b2ae65ecf80af6e1a0b5bf23981a98884159df5af7b9cc`

Supply a verified local path if it differs from the ignored default location:

```sh
make test PROM_FIXTURE=/absolute/path/to/verified-prom512k.bin
```

The exact field fixture produced:

- 10,460 FRTS-only core cases;
- 15,163 full-unlock core cases;
- injected failures at every successful-path read, write, delay, barrier, and stage-event boundary;
- no hardware access during any host test.

## Build the active EFI image

Only after the full private suite passes:

```sh
make unlock-active PROM_FIXTURE=/absolute/path/to/verified-prom512k.bin
```

The target runs the source audits, full host tests, active-image binary audit, and PE inspection. In the tested toolchain and exact inputs, the output is:

```text
build/TU104UNLOCK.EFI
389999 bytes
SHA-256 053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e
```

The binary audit extracts all five linked blobs from the ELF/EFI build and byte-compares them with the pinned inputs. It also verifies a PE32+ x86-64 EFI application with no imports and checks the confirmation, fail-closed, success, and same-boot handoff contracts.

There is intentionally no active-image installation target. The current source profile is specific to the original machine and bootloader. Do not boot the output on a different system.

## Diagnostic USB configuration

Diagnostic installation targets require an exact removable-disk serial. Copy `config.local.mk.example` to ignored `config.local.mk`, then fill in values only after inspecting the mounted device. With `USB_SERIAL` unset, every USB installation target refuses. The Makefile dynamically resolves the mounted partition's parent disk and checks serial, model, FAT filesystem, volume label, and writability; it never assumes `/dev/sdX` is stable.

## Benchmark programs

### Tensor MMA

```sh
make tensor-mma-driver
./build/tensor_mma_driver
```

The validator refuses every device except SM75, 40 SMs, and PCI BDF `0000:01:00.0`. It generates PTX through the CUDA Driver API, uses nonzero FP16 inputs with FP32 accumulation, checks the exact result, records per-SM `clock64`, and writes no management, clock, firmware, PCI, or register state. Its stdout is JSONL; preserve it before another power cycle.

### cuBLAS GEMM

On Ubuntu 24.04, install the runtime library if it is not already present:

```sh
sudo apt install libcublas12
make cublas-gemm-driver
./build/cublas_gemm_driver --label locked
```

Use `--label unlocked` only after independently establishing the unlocked state. The label records operator-established state; it does not read the selector registers. `make cublas-gemm-bench STATE=locked` is the equivalent Makefile entry point.

The executable loads `libcublas.so.12` at runtime. `CUBLAS_LIBRARY=/absolute/path/libcublas.so.12` can select an exact library. It refuses devices other than the pinned SM75, 40-SM GPU at `0000:01:00.0`, uses FP16 input with FP32 output/accumulation, and emits JSONL for 4096 and 8192 square GEMMs. Preserve the executable hash and raw stdout for both power states.

The [validation record](VALIDATION.md) describes the measured workloads and bounded output checks. Preserve raw JSONL before another power cycle.
