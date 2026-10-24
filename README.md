# RTX 2070 SUPER Tensor unlock

[한국어](README.ko.md)

This project documents a volatile Tensor-compute unlock on a Gigabyte RTX 2070 SUPER (TU104, SM75). It includes the source code, benchmark results, and validation history, including failed attempts.

A UEFI application sets the GPU state before the operating system starts. The existing NVIDIA driver then initializes the GPU with that state. Neither the driver nor the VBIOS is modified, and a full power-off restores the original state. Validation currently covers the single card and system listed below.

## Benchmark results

Two benchmarks compare locked and unlocked performance with FP16 inputs and FP32 accumulation. Tensor MMA measures instruction throughput; cuBLAS measures matrix-multiplication throughput.

### Tensor MMA — approximately 2×

These are the medians of seven samples per state, using the same card, driver, and executable. The reported device-clock attribute was 1905 MHz in every record.

| State | TFLOPS | FLOP/SM-cycle |
| --- | ---: | ---: |
| Locked — normal cold boot | 32.4437 | 511.6813 |
| Unlocked — independent power cycle 1 | 65.1408 | 1021.9858 |
| Unlocked — independent power cycle 2 | 64.5049 | 1021.9858 |

The first unlock reached **2.0078×** the locked TFLOPS and **1.9973×** the locked throughput per SM cycle. A second unlock after a full power-off reproduced `1021.9858 FLOP/SM-cycle`. TFLOPS differed by less than 1% between the two unlocked runs.

The benchmark executes SM75 `mma.sync.aligned.m16n8k8` instructions with nonzero inputs. All 21 samples had zero arithmetic error and covered all 40 SMs. No NVIDIA Xid or PCIe AER error was observed after the measured runs.

### cuBLAS GEMM — 1.63–1.65×

Both states used the same 25,928-byte executable and cuBLAS 12.0.2 runtime.

| Square matrix size N | Locked TFLOPS | Unlocked TFLOPS | Speedup |
| ---: | ---: | ---: | ---: |
| 4096 | 39.094819 | 63.773163 | 1.631243× |
| 8192 | 39.832639 | 65.676676 | 1.648816× |

Each size received a two-second warm-up, followed by seven batches of 30 GEMMs timed with CUDA events. The table reports median batch throughput. Four sampled outputs per size had zero error.

These measurements cover the two workloads above. Application performance, correctness across varied inputs, and long-term stability have not been validated. See the [validation record](docs/VALIDATION.md) for measurement conditions and raw data.

## Validated system

| Component | Configuration |
| --- | --- |
| GPU | NVIDIA GeForce RTX 2070 SUPER, TU104, 40 SMs |
| PCI identity | `10de:1e84`, subsystem `1458:3ff5` |
| PCI location | `0000:01:00.0` |
| VBIOS | `90.04.76.00.D5` |
| Motherboard | MSI MPG Z390 GAMING EDGE AC (`MS-7B17`, BIOS `A.70`) |
| OS | Ubuntu 24.04, kernel `7.0.0-38-generic` |
| NVIDIA driver | Unmodified `610.57.04` |
| Active image | `0.8.0-TU104-VOLATILE-UNLOCK-CANDIDATE` |

SHA-256 of the tested EFI image:

```text
053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e
```

The execution profile also covers this system's firmware, memory layout, and Ubuntu GRUB image. Compatibility with other configurations has not been established, including systems with the same GPU model. Read [Safety](docs/SAFETY.md) before building or running the active image.

## How it works

The application establishes volatile GPU firmware/register state before the OS starts, allowing the existing driver to initialize with that state. The validated final selector values are `SS0=0x88888888` and `SS1=0x00000008`. In that state, measured FP16-input/FP32-accumulation Tensor throughput increased from about 512 to 1,022 FLOP per SM cycle.

The host prepares the input memory, and the card's Falcon firmware performs the state transition. The host does not directly write WPR2 or the feature selectors. The SM count remains unchanged, and the full meaning of the internal policies represented by those selectors has not been established.

<details>
<summary>Firmware transition details</summary>

1. The UEFI wrapper validates the exact PCI identity, subsystem, live PROM, option ROM, motherboard/firmware state, memory map, and initial GSP/SEC2 state. It also validates and preloads the exact Ubuntu GRUB image before a GPU write is possible.
2. It captures the card's native, signed FWSEC image from the live PROM and allocates one exact 32 MiB high-physical-memory pool. That pool contains the native FWSEC staging area plus hash-pinned production TU10x Booter, signature, V67 payload, boot container, metadata, radix, and data regions. The layout follows NVIDIA's public TU102 WPR metadata geometry and is checked before execution.
3. After a physical one-shot `U` confirmation, it waits only for the documented transient SEC2 IMEM/DMEM scrub bits to clear, then rechecks the complete stable guard state. This bounded wait is the fix derived from the failed `0.7.0` attempt; unrelated bit changes or timeout still refuse execution.
4. The card-native signed FWSEC runs one FRTS command on GSP and must create the exact temporary WPR2 bounds. The production Booter is then loaded into SEC2 and invoked once with metadata pointing at the prepared V67/container layout. There is no automatic retry.
5. The result is accepted only if the Booter mailbox is zero, the selectors are exactly `SS0=0x88888888` and `SS1=0x00000008`, the expected PLM and restored WPR2 state match, both Falcons are stopped, and the state machine reaches stage 13 in a quiescent state.
6. Only after those checks does the wrapper release the 32 MiB pool and start the already-preloaded GRUB image. This avoids a firmware reset between the transition and Linux, allowing the unmodified NVIDIA driver to inherit the volatile state. A full power cycle clears it.

After the first possible GPU write, any uncertain result retains the backing memory and halts in UEFI. The operator must fully power the machine off; a warm reboot or continued OS boot is forbidden by the design.

</details>

## Documentation and reproduction

| Topic | Document |
| --- | --- |
| Recomputing results and reproduction scope | [Reproducing the report](docs/REPRODUCING.md) |
| Methods, raw measurements, and hashes | [Validation record](docs/VALIDATION.md) |
| Failed attempts and subsequent decisions | [Experiment history](docs/EXPERIMENT-HISTORY.md) |
| Dependencies and build instructions | [Build guide](docs/BUILD.md) |
| Operating conditions and stop criteria | [Safety](docs/SAFETY.md) |
| Evidence required for another system | [Porting](docs/PORTING.md) |
| Attribution and third-party licenses | [NOTICE](NOTICE.md) |

The following command verifies public inputs, host tests, and published measurements without accessing the GPU:

```sh
make reproduce-public
```

Public inputs are pinned to NVIDIA `610.57.04` and an `xrip/cmp50hx-unlock` revision. Full target tests also require a private 512 KiB PROM fixture verified against two matching captures.

There is no `install-unlock-active` target. Verifying published results, rebuilding identical EFI bytes, and measuring physical hardware each have different requirements. An intermediate `Success` message alone does not establish an unlock: the result depends on final firmware state, driver health, correct computation, and an independent locked measurement. The reproduction guide explains these distinctions.

## Repository layout

```text
.
├── README.md / README.ko.md   Project overview and results
├── LICENSE                   MIT license for original contributions
├── NOTICE.md                 Attribution and third-party licenses
├── LICENSES/                 Third-party license texts
├── docs/                     Validation, reproduction, and experiment records
├── include/                  Profiles, memory layouts, and state machines
├── src/                      UEFI applications and staged diagnostics
├── tests/                    Host tests and failure injection
├── scripts/                  Audits and input retrieval
├── tools/                    Analysis tools and benchmark source
└── results/                  Public measurements and hashes
```

Raw PROM captures, full machine logs, external reference repositories, and prebuilt EFI files are excluded from publication.

## License

Original contributions to this project are licensed under the [MIT License](LICENSE). Third-party code, firmware, and other inputs retain their own license terms. See [NOTICE](NOTICE.md) for attribution and scope.
