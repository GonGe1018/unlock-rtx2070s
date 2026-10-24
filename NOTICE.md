# Provenance and third-party notices

[Overview](README.md) · [Build inputs](results/input-manifest.json)

This document records the project's research lineage, third-party attribution, and redistribution boundaries. Original contributions are licensed under the [MIT License](LICENSE). Third-party materials retain their existing license terms.

## Research lineage

The pre-OS FWSEC/Booter/V67 sequence was studied from [xrip/cmp50hx-unlock](https://github.com/xrip/cmp50hx-unlock) at commit `df2fe11b370bb52cc314576d0f2f5ae47ce908f8`. That repository's root license is copyleft-next 0.3.1. Its `efi-unlock` work separately retains the MIT notice from the CMP40HX unlock lineage; it is incorrect to describe every file or firmware blob in the root repository as MIT.

The retained upstream attribution is:

> Ported from `unlock40x_v70.c` of the CMP40HX-Unlock project (https://github.com/PZH1gdmu/CMP40HX-Unlock, MIT License, Copyright (c) 2026), which itself descends from the CMP 90HX unlock work and the V67 canary exploit published by Jon Pry, “A Canary in the Crypto Mine,” DOI 10.5281/zenodo.20916112.

The corresponding MIT text is preserved in [LICENSES/CMP40HX-MIT.txt](LICENSES/CMP40HX-MIT.txt).

Closely related board-specific Turing research was reviewed from [haohervchb/Turing-unlock](https://github.com/haohervchb/Turing-unlock) at commit `a188b24a392414f29f931ea886329227cfbc2f98`. It is distributed under copyleft-next 0.3.1 and demonstrates the importance of private firmware boundaries, exact-input guards, raw A/B evidence, and explicit limitations.

Other repositories retained under ignored local `references/` were research inputs only. They are not part of the publication and are not relicensed here.

## NVIDIA source inputs

Three generated source files are retrieved from the official [NVIDIA/open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules) tag `610.57.04`:

- `g_bindata_kgspGetBinArchiveBooterLoadUcode_TU102.c`
- `g_bindata_ksec2GetBinArchiveBlUcode_TU102.c`
- `g_bindata_kgspGetBinArchiveGspRmBoot_TU102.c`

Those files carry NVIDIA's MIT notice, reproduced in [LICENSES/NVIDIA-MIT.txt](LICENSES/NVIDIA-MIT.txt). The retrieval and extractor verify exact source and output hashes. The fetched sources and extracted binary assets remain ignored build inputs and are not redistributed in this project.

GNU-EFI, the system toolchain, GRUB, the Linux kernel, and the NVIDIA driver are external build/runtime components and retain their own licenses.

## Material excluded from publication

This publication excludes:

- the test card's option ROM and paired 512 KiB physical PROM captures;
- native firmware bytes extracted from that capture;
- external V67/boot-container binaries and fetched reference repositories;
- prebuilt UEFI executables and object files;
- full machine logs, USB serial numbers, private mount paths, and boot IDs.

Public `results/` files contain only benchmark output, sanitized validation facts, and cryptographic hashes that identify the privately retained evidence. Provenance identifies bytes; it does not by itself grant redistribution rights.

## Original contributions and MIT license

The TU104 target profiles, staged guard sequence, injected-I/O state machines, fail-closed lifecycle policy, exact Ubuntu handoff, audit scripts, CUDA Driver API validator, cuBLAS GEMM benchmark, tests, and documentation form the adaptation maintained in this repository. The active field binary remains target-specific and is not distributed.

The project maintainer has selected the [MIT License](LICENSE) for original contributions. This grant covers only rights held by the project contributors; it does not relicense third-party code, firmware, or other inputs, or waive applicable upstream obligations. Existing copyright, license, and attribution notices remain in effect.
