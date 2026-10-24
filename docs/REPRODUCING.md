# Reproducing the report

[Overview](../README.md) · [Build guide](BUILD.md) · [Validation](VALIDATION.md)

Choose the level that matches what you want to verify. Each level establishes a different claim; passing one does not establish the next.

| Level | What it establishes | Additional requirement |
| --- | --- | --- |
| 1 | Published measurements and arithmetic match the raw records | Python 3 and this repository |
| 2 | Public inputs and host tests reproduce | The build environment |
| 3 | The field-tested EFI bytes reproduce | Exact private fixture and field profile |
| 4 | Physical A/B measurements reproduce | The validated machine or an independently reviewed port |

The commands below do not install or boot an active EFI image.

## Level 1 — Recompute the published result

This level needs only Python 3 and the public repository. It checks the exact SHA-256 of all five raw JSONL files, validates target metadata, Tensor sample and cuBLAS batch ordering, 40/40 SM coverage and bounded output checks, recomputes medians and ratios, and cross-checks the sanitized field summary.

```sh
make verify-results
```

Expected Tensor MMA medians:

| State | TFLOPS | FLOP/SM-cycle |
| --- | ---: | ---: |
| Locked | 32.4437 | 511.6813 |
| Unlocked run 1 | 65.1408 | 1021.9858 |
| Unlocked run 2 | 64.5049 | 1021.9858 |

Run 1 / locked ratios are `2.00781045×` for TFLOPS and `1.99730926×` for FLOP/SM-cycle. These ratios are dimensionless.

Expected cuBLAS medians:

| N | Locked TFLOPS | Unlocked TFLOPS | Ratio |
| ---: | ---: | ---: | ---: |
| 4096 | 39.094819 | 63.773163 | 1.63124334× |
| 8192 | 39.832639 | 65.676676 | 1.64881559× |

The verifier fails on any changed raw byte, missing sample, wrong GPU metadata, nonzero result error, incomplete SM coverage, or summary mismatch.

## Level 2 — Reproduce public extraction and host tests

On Ubuntu 24.04 x86-64 with the dependencies in [BUILD.md](BUILD.md):

```sh
make reproduce-public
```

This downloads only hash-pinned public inputs from the original repositories, extracts the exact NVIDIA TU10x assets, and runs the hardware-free test set and result verifier. It does not open PCI devices, map MMIO, execute firmware, write a USB, or require root.

Expected public test highlights:

- all source safety audits pass;
- 259 unlock-lifecycle cases pass, including all 256 state combinations;
- exact unlock-layout and Ubuntu-handoff profile tests pass;
- parser/profile and public-result unit tests pass;
- the report verifier checks 21 Tensor samples and 28 cuBLAS timing batches;
- every published bounded correctness check has zero numerical error.

## Level 3 — Reproduce the field-tested EFI bytes

This level additionally requires:

- the exact compiler/binutils/GNU-EFI versions in [BUILD.md](BUILD.md);
- a 512 KiB capture matching the tested PROM SHA-256 `df1b9144a31dda10e0b2ae65ecf80af6e1a0b5bf23981a98884159df5af7b9cc`;
- the exact field profile retained in the public source, including the installed GRUB structure/configuration and root filesystem UUID.

The PROM is not linked into the EFI. It is required as a private fixture so the 10,460-case FRTS suite and 15,163-case full-unlock suite test the exact native firmware bytes before the active image is produced.

```sh
make reproduce-field-build \
  PROM_FIXTURE=/absolute/path/to/verified-prom512k.bin
```

Expected output:

```text
build/TU104UNLOCK.EFI
389999 bytes
053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e
```

This proves byte reproduction of the tested build. It does not prove another machine is compatible and does not authorize booting the output.

## Level 4 — Repeat the physical A/B experiment

Physical reproduction is possible only after completing a new target/host port under [PORTING.md](PORTING.md), or on the exact already validated machine. It requires the staged history in [EXPERIMENT-HISTORY.md](EXPERIMENT-HISTORY.md), independent review, and the operator protocol in [SAFETY.md](SAFETY.md).

The measurement design is:

1. Full power-off and ordinary Ubuntu boot with the unlock USB absent.
2. Build the benchmark programs described in [BUILD.md](BUILD.md), then run `build/tensor_mma_driver` and `build/cublas_gemm_driver --label locked`. Preserve all raw locked JSONL records and the current-boot Xid/AER check.
3. Full power-off, one-time USB boot, one physical `U` confirmation, exact success state, and automatic same-boot Ubuntu handoff.
4. Run both byte-identical benchmarks, using `--label unlocked` for cuBLAS, and preserve the unlocked JSONL plus Xid/AER check.
5. Perform another full power cycle and successful unlock to test repeatability rather than reusing one unlocked boot.
6. Publish raw JSONL, binary/log hashes, failures, and limitations—not only a headline percentage.

The repository intentionally has no generic active installer. A port must not reuse the original USB serial, fixed high address, bootloader profile, or board guards without independently proving them.

## Privacy and exact-byte reproduction

Exact reproduction of the historical EFI requires the exact GRUB configuration, which contains a unique root filesystem UUID. It is a device identifier, not a credential, but it can contribute to machine fingerprinting. USB/disk serials, boot IDs, local usernames/paths, full logs, and raw firmware captures are not needed for byte reproduction and remain excluded.

Redacting the root UUID would preserve reproduction of the method and public measurements while changing the rebuilt EFI bytes. Any such change must state that the historical EFI SHA-256 can no longer be reproduced.
