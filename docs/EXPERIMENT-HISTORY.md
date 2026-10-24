# Experiment and failure history

[한국어](EXPERIMENT-HISTORY.ko.md) · [Overview](../README.md) · [Validation](VALIDATION.md)

This record follows the observations and decisions that led to the validated `0.8.0` result. Refusals, timeouts, and inconclusive diagnostics are retained alongside successes to explain the final guards and lifecycle rules.

Full firmware logs contain machine identifiers and remain private. Each important log is identified below by SHA-256, and the sanitized machine-readable record is [results/experiment-history.json](../results/experiment-history.json).

## Initial investigation

The project first established identity and firmware evidence with read-only stages: PCI preflight, option-ROM capture, bounded BAR0/PROM reads, two matching 512 KiB PROM captures, repeated register inventory, low-address EFI DMA service checks, platform/VT-d inventory, and Falcon-state inventory. No full-unlock attempt preceded those observations.

The entries below follow the investigation order. Diagnostic branches used separate version labels, so the numbers are not strictly increasing. Each entry records the observation, outcome, log hash, interpretation, and next decision.

## Experiment records

### 0.6.2 — EFI high-DMA mapping was unsuitable

The firmware successfully allocated 32 MiB at physical `0x86c000000`, but `BusMasterRead64 Map()` returned device address `0x84dee000`. The mapping was a low bounce address, not an identity mapping and not above 4 GiB. The program unmapped it, verified the host canary, freed the pages, and returned `Unsupported` without asking the GPU to transfer anything.

- **Outcome:** safe, pre-mutation refusal
- Log SHA-256: `af364bdda5b9f4bdbe20d7cc65679802039bb2ce7ac32be801fb284f9260afda`
- **Interpretation:** an above-4-GiB host allocation does not prove a suitable EFI device address. `Unsupported` described this firmware service geometry; it did not prove TU104 hardware could not reach the physical address.
- **Next decision:** inspect platform/IOMMU state read-only and separately prove a tightly guarded direct-physical path.

### 0.7.0 — First active FRTS attempt refused before mutation

All wrapper checks passed, the high pages were prepared, and the operator's physical `U` confirmation reported `Success`. The real core then entered only the `GUARDS` stage and returned `Security Policy Violation`. No first GPU write, GSP reset, or firmware start occurred; the pages were released and returning to the OS was safe.

- **Outcome:** failed active attempt, safely before mutation
- Log SHA-256: `21e387c6ee56206d7628ecf5cd98d1b86e50ecb3ee46f2ed0e2140433803295c`
- **Meaning of the screen message:** `Physical one-shot confirmation status=Success` meant the key confirmation succeeded. It did **not** mean FRTS succeeded. The final durable status and terminal stage are authoritative.
- **Limitation:** this first image logged the failed stage but not the mismatching register/value.
- **Next decision:** never retry the same active image; build read-only diagnostics around the exact guard boundary.

### 0.6.5 — Idle guard inventory passed but was inconclusive

A read-only inventory compared all 15 native descriptor dwords, all 10,108 native FWSEC dwords, and the remaining pinned values. All 10,197 comparisons matched. This ruled out corrupted static firmware/profile data, but it sampled before the physical-confirmation/watchdog timing boundary and therefore did not explain 0.7.0.

- **Outcome:** successful read-only diagnostic, insufficient explanation
- Log SHA-256: `97e3a0b67bdb989f6b13c105c3e2b9e874a5c5ec17fc1f709fcd35be8c5d3f27`
- **Interpretation:** a snapshot taken at a different point in the sequence cannot clear a timing-sensitive guard.
- **Next decision:** replay the exact confirmation, revalidation, and watchdog ordering while making every write callback unreachable.

### 0.6.6 — Exact read-only replay found the mismatch

The replay reproduced the 0.7.0 ordering and called only the real core guard helper. It stopped at zero-based read 32: SEC2 `DMACTL` offset `0x0084010c` was expected to be `0x1` but was `0x7`. The changed mask `0x6` consists of the read-only DMEM/IMEM scrubbing status bits; the stable `REQUIRE_CTX` bit remained set. No GPU write/reset/firmware execution occurred.

- **Outcome:** expected read-only guard refusal with root cause identified
- Log SHA-256: `56add48627b1d12cf9edac694acea5f6803b690d7365a72c04fe8274ac4760dc`
- **Interpretation:** the original guard treated a documented transient status bit as a permanent exact value.
- **Next decision:** do not mask and continue and do not write `DMACTL`; test a bounded read-only wait for the documented scrub bits to clear.

### 0.6.7 — Bounded scrub wait validated twice

Two independent read-only boots both observed `DMACTL=0x7`, then `0x1` after one 1 ms delay. After the wait, the unchanged real guard helper accepted all 10,197 reads. Each run released its pages and made zero write attempts.

- **Outcome:** two successful read-only validations
- Log SHA-256 run 1: `0761263ad389b95f5ca1f630b8a385bef38cff466a69f97b72ae6e5d9ae64b69`
- Log SHA-256 run 2: `e1adab2c1826b33e82084f3cedb32182f8703d77db07af681d8ea1dd099fa0da`
- **Interpretation:** wait on only the documented dynamic mask, require the full stable value afterward, and fail on timeout or any unrelated change.
- **Next decision:** integrate that exact wait into a new one-shot active FRTS image; do not modify the guard's final expected state.

### 0.7.1 — Native FRTS succeeded

With the validated scrub wait, the native signed FWSEC completed all seven stages and produced exact volatile WPR2 bounds `LO=0x01ffee00` and `HI=0x01fffc00` from the high physical allocation.

- **Outcome:** active FRTS success; not yet a Tensor unlock
- Log SHA-256: `a06cb805e73e6df095ffffe0c24750661f102bf83897e5f0685a9d74f274b5c6`
- **Interpretation:** the direct high-physical-address native FRTS path worked on the exact target. Because this diagnostic could not safely hand off after mutation, a complete physical power-off remained mandatory.
- **Next decision:** prove the exact same-boot Ubuntu handoff independently before combining it with the full path.

### 0.7.2 — Read-only same-boot handoff succeeded

The UEFI app validated and preloaded the exact installed GRUB image and config, closed its USB log, and started GRUB without writing boot variables or the system ESP. Ubuntu reached the expected root filesystem seven seconds later.

- **Outcome:** same-boot handoff success, no GPU mutation
- Log SHA-256: `9edb1796b4944119fc250a473da28e60854b837758c6803f1f04ac79e0ba0760`
- **Next decision:** combine preloaded handoff with a lifecycle rule that permits release/boot only after exact completed and quiescent unlock success.

### 0.8.0 run 1 — Physical confirmation timed out safely

The complete pre-mutation integration passed, including exact asset checks, GRUB preload, live PROM, fixed 32 MiB allocation, and layout preparation. No `U` key arrived within 30 seconds. The active core was never entered, no GPU write was attempted, the pool was released, and firmware booted Ubuntu.

- **Outcome:** safe cancellation, not an unlock attempt
- Log SHA-256: `0475e6dadbcbbbfd0f925111a16cd1868ab73934cc566224214d31ee8bf5d8ad`
- **Interpretation:** the timeout/cancel lifecycle works and must not be reported as a failed post-write attempt.

### 0.8.0 runs 2 and 3 — Full unlock reproduced

Two attempts separated by full power cycles reached the exact terminal state: `SS0=0x88888888`, `SS1=0x8`, Booter mailbox 0, stage 13, and quiescent cleanup. Only then were the 32 MiB pages released and preloaded GRUB started. Linux loaded the unmodified driver with no observed Xid/AER error.

- Success log SHA-256 run 2: `6be6fbc7b08b428334acf7a40cd50fe47ed2aade4e7dca3042ce8e86075f6ea7`
- Success log SHA-256 run 3: `9965b9586e817fd091bdc21b6e0a53adc4eef7590755bf7672ae24313ebb9896`
- Run 2 Tensor result: median `65.1408 TFLOPS`, `1021.9858 FLOP/SM-cycle`, exact output, 40/40 SMs.
- A full shutdown and normal USB-absent boot produced the locked control: `32.4437 TFLOPS`, `511.6813 FLOP/SM-cycle`.
- Run 3 reproduced `1021.9858 FLOP/SM-cycle`; wall-clock TFLOPS differed from run 2 by less than 1%.

## Interpreting outcomes

1. **Before the first write:** a guard refusal, Escape, or timeout may release owned memory and return only when the lifecycle explicitly says so. Preserve the log and diagnose; do not weaken the guard.
2. **After a possible write:** any non-exact or uncertain state retains the backing allocation and halts. Fully power off; never continue into the OS or retry in the same power state.
3. **Success:** the word `Success` in an intermediate EFI call is insufficient. Require the exact terminal selectors, mailbox, stage, quiescence, pool release, same-boot handoff, driver health, correct compute output, and an independent locked control.

That distinction is the central lesson of the failed 0.7.0 run.

## Source and build availability

The table identifies historical stages that remain buildable from the current source tree. Hardware runs still require the exact target profile, local USB guard, and [Safety](SAFETY.md).

| Stage | Current build target | Boundary | Current-tree reproducibility |
| --- | --- | --- | --- |
| 0.1.0 PCI preflight | `make` | PCI config read only | yes |
| 0.2.0 option-ROM capture | `make rom-capture` | firmware-owned ROM buffer read | yes |
| 0.3.0 4 KiB MMIO probe | `make mmio-probe` | bounded BAR0 reads | yes |
| 0.4.0 paired PROM capture | `make prom-capture` | bounded BAR0/PROM reads | yes |
| 0.5.0 register inventory | `make register-probe` | named MMIO reads | yes |
| 0.6.1 low DMA service | `make dma-map-probe` | EFI Map/Unmap, no device transfer | yes |
| 0.6.2 high DMA service | `make high-dma-map-probe` | EFI Map/Unmap, no device transfer | yes |
| 0.6.3 platform inventory | `make platform-dma-probe` | CPU/chipset/UEFI reads | yes |
| 0.6.4 Falcon inventory | `make falcon-state-probe` | named MMIO reads | yes |
| 0.6.5 idle guard inventory | historical source revision | GPU read only | log/hash record only at the current source tree |
| 0.6.6 exact guard replay | historical source revision | GPU read only | log/hash record only at the current source tree |
| 0.6.7 scrub-wait replay | `make fwsec-guard-probe` | GPU read only | yes; current guard diagnostic |
| 0.7.0 first FRTS attempt | historical source revision | active-capable, stopped pre-write | log/hash record only; intentionally not reintroduced |
| 0.7.1 FRTS with wait | `make fwsec-frts-probe` | GPU-mutating one-shot | yes, private PROM fixture required |
| 0.7.2 Ubuntu handoff | `make handoff-probe` | no GPU mutation | yes |
| 0.8.0 full unlock | `make reproduce-field-build` | GPU-mutating one-shot build only | exact byte build with private PROM fixture |

`make fetch-build-inputs` must precede public builds. The read-only stage targets depend only on `test-public`; the FRTS guard/active and full-unlock targets also require the exact private PROM-backed core suites. Historical 0.6.5, 0.6.6, and 0.7.0 sources were superseded before this repository had version-control history. Their exact logs and result records are retained by hash; their binaries cannot be rebuilt from the current source tree. The known-bad 0.7.0 path is deliberately not offered as a build option.
