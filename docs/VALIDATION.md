# Validation record — 2026-10-25

[Overview](../README.md) · [Reproduction](REPRODUCING.md) · [Experiment history](EXPERIMENT-HISTORY.md)

Two independent power cycles reproduced approximately 2× Tensor MMA instruction throughput on the tested board. A separate cuBLAS A/B measured 1.63–1.65× dense GEMM throughput. This document records the methods, supporting evidence, and limits of those claims.

## Tensor MMA measurements

The same 25,960-byte benchmark binary was used in all three states:

`585accca9ed3959ccae3c11e50dcf6ea0a5cc447b123e31484f418df788a15dd`

It uses the CUDA Driver API and JITs SM75 PTX for `mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32`. It launches one 1,024-thread block per SM, runs 2,048 rounds, uses nonzero exact FP16 operands, expects an exact FP32 result of 8, and records one `clock64` interval and SM ID per block. There are two warm-ups followed by seven reported samples.

| State | Median TFLOPS | TFLOPS range | Median FLOP/SM-cycle |
| --- | ---: | ---: | ---: |
| Locked | 32.4437 | 32.4367–32.4457 | 511.6813 |
| Unlocked run 1 | 65.1408 | 65.1250–65.1570 | 1021.9858 |
| Unlocked run 2 | 64.5049 | 64.4991–64.5131 | 1021.9858 |

All 21 samples had `max_abs_error=0.0` and `unique_sms=40`. All reported the same 1,905,000 kHz device-clock attribute. Unlocked run 1 divided by locked is 2.00781045 for TFLOPS and 1.99730926 for FLOP/SM-cycle. Unlocked run 2 differs from run 1 by -0.9762% in wall-clock TFLOPS and 0.0000% at the recorded cycle-normalized precision.

## cuBLAS GEMM measurements

A second benchmark exercises `cublasGemmEx` rather than an inline-PTX microkernel. The exact same 25,928-byte executable was used in both states:

`0c6c5dd6e908e9e15d98baf7b74fa9f8c2d61f3a47a0dc85103081567853f580`

It dynamically loaded Ubuntu's `libcublas12` package `12.0.2.224~12.0.1-4build4` (`cublasGetVersion=120002`). Inputs were FP16; output and accumulation were FP32. Each square size received a two-second warm-up, then seven batches of 30 GEMMs timed with CUDA events. The table uses the median of the seven per-GEMM batch times and throughputs.

| N | Locked ms | Locked TFLOPS | Unlocked ms | Unlocked TFLOPS | Ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4096 | 3.515529 | 39.094819 | 2.155122 | 63.773163 | 1.63124334× |
| 8192 | 27.603284 | 39.832639 | 16.741280 | 65.676676 | 1.64881559× |

The unlocked measurement was preserved first. The machine was then fully shut down, the unlock USB was removed, power remained off for ten seconds, and the normal Ubuntu boot was used for the locked measurement. Immediately before that run, the inline-PTX validator independently reported the locked rate of approximately 512 FLOP/SM-cycle. Both cuBLAS records report the same driver API, cuBLAS version, target metadata, and 1,905,000 kHz device-clock attribute.

All-one inputs make the expected FP32 result exactly representable. Four positions per size were sampled after the final GEMM and had zero error. These checks cover only the sampled positions. There is one cuBLAS boot measurement per state, supporting an observed 1.63–1.65× gain for these dense GEMMs. Application-level performance and repeated cuBLAS boot measurements remain unmeasured.

## Boot evidence and locked control

### Active UEFI runs

The tested active image was built as `0.8.0-TU104-VOLATILE-UNLOCK-CANDIDATE`, 389,999 bytes, SHA-256:

`053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e`

The first field boot reached `READY` but timed out before physical confirmation. It performed no GPU write, released the allocation, and safely returned to the firmware's normal Ubuntu path.

Two later attempts, separated by full power cycles, each completed exactly one active core invocation. Their durable UEFI logs are retained privately and identified by hash:

| Successful run | UEFI log SHA-256 | Terminal result |
| --- | --- | --- |
| 1 | `6be6fbc7b08b428334acf7a40cd50fe47ed2aade4e7dca3042ce8e86075f6ea7` | `SS0=88888888`, `SS1=00000008`, mailbox 0, stage 13, quiescent |
| 2 | `9965b9586e817fd091bdc21b6e0a53adc4eef7590755bf7672ae24313ebb9896` | same exact terminal state |

Both logs also record successful SEC2 scrub completion, successful release of the 32 MiB pool only after verified quiescence, and handoff to the exact preloaded Ubuntu GRUB. Linux initialized the unmodified 610.57.04 driver and active display in the same boot.

### Locked A/B control

After the first successful unlock measurement, the machine was shut down, the unlock USB was physically removed, power remained off for ten seconds, and Ubuntu was started normally. The new boot ID and absence of every removable USB were checked before measuring. That run provides the same-card locked baseline.

The second unlocked result came from another shutdown, USB reconnection, one-time USB boot, physical `U` confirmation, successful automatic handoff, and a fresh Linux boot. This establishes two independent active successes, not repeated samples from one unlocked boot.

The successful runs numbered 1 and 2 above correspond to UEFI attempts 2 and 3 in the [experiment history](EXPERIMENT-HISTORY.md). Attempt 1 was the pre-write confirmation timeout.

## Driver and platform health

After each measured Linux boot, the target appeared at `0000:01:00.0` as a 40-SM RTX 2070 SUPER with driver 610.57.04 and active display. The current-boot kernel journal contained no matching NVIDIA Xid, fatal/uncorrected PCIe AER, or fallen-off-bus report after the benchmark.

These checks are bounded. They are not an endurance, suspend/resume, gaming, all-CUDA-operation, numerical-stability, or long-term reliability test.

## Offline verification

A clean forced rebuild after the second field run reproduced the exact active EFI hash. The same run passed:

- 10,460 FRTS core cases;
- 15,163 full-unlock core cases;
- 259 lifecycle cases covering all 256 state combinations plus allocation checks;
- exact asset-layout and GRUB-profile tests;
- source audits and an embedded-blob binary audit;
- 14 Python parser/profile tests.

Plain and ASan/UBSan suites were also used during development. Host tests use injected I/O and do not access physical hardware.

The current public suite also includes result-verifier tests: 17 Python tests in total. The [publication check record](../results/publication-checks.json) records that later verification separately from the historical rebuild.

## Scope of the evidence

The strongest supported conclusion is that the exact pre-OS sequence repeatably changed FP16-input/FP32-accumulation Tensor MMA from approximately 512 to 1,022 FLOP per SM cycle on this board and survived the same-boot handoff to the stock driver.

This record does not establish:

- a 2× gain for every application or model;
- compatibility with every RTX 2070 SUPER or TU104 board;
- IMLA/integer, FP16-accumulation, graphics, RT-core, VRAM, or PCIe changes;
- permanent persistence or safe warm-reset behavior;
- long-duration reliability.

## Raw records and hashes

Each raw record is available below. The [sanitized field summary](../results/rtx2070s-field-validation.json) collects the measurements and metadata; [SHA256SUMS](../results/SHA256SUMS) covers the public result files.

| Record | SHA-256 |
| --- | --- |
| [Locked Tensor MMA](../results/rtx2070s-locked-tensor.jsonl) | `8ca5ea30de13b7c9052266aefb117c115b666a5a98cdc741af1d0f37bc4e5f38` |
| [Unlocked Tensor MMA, run 1](../results/rtx2070s-unlocked-tensor-run1.jsonl) | `b63c7cf5cb34fbbc2cfbb50ae39f2c7f349d994aaff7363c1a79fdd332a17d84` |
| [Unlocked Tensor MMA, run 2](../results/rtx2070s-unlocked-tensor-run2.jsonl) | `e4e5e45a2b844801d63545342081458e49e25b04bf686f769e79256bddd8405f` |
| [Locked cuBLAS](../results/rtx2070s-locked-cublas.jsonl) | `629d9e37f77c100f71d15792911321b6659254dd91bd27407baa15aa4224c9de` |
| [Unlocked cuBLAS](../results/rtx2070s-unlocked-cublas.jsonl) | `f94acf774a2779624bee4e3f4e0485bcad6b52d6d83d7fa72e9ba50539d7356b` |

Run `make verify-results` to check raw hashes and recompute the results. [Reproducing the report](REPRODUCING.md) explains the verification levels; the [experiment history](EXPERIMENT-HISTORY.md) records failed and inconclusive attempts alongside the successful ones.
