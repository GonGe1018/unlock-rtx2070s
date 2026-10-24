# Porting scope and required evidence

[Overview](../README.md) · [Safety](SAFETY.md) · [Experiment history](EXPERIMENT-HISTORY.md)

The current active profile has been validated on one complete system. Cards sold under the same name can differ in subsystem identity, VBIOS, PROM layout, Falcon state, PCI topology, BAR placement, framebuffer size, and motherboard DMA behavior. Those differences require independent evidence.

## Review rule

Never port by deleting a failed guard, accepting a wildcard identity, copying the original fixed physical address, or retrying after a partial mutation. Each changed observation needs an explanation, a new pinned profile, offline tests, and independent review.

## Required staged evidence

1. **Read-only identity:** PCI BDF, vendor/device/subsystem IDs, class, VBIOS, BAR descriptors, parent bridge, CPU, board, BIOS, Secure Boot, and relevant firmware setup state.
2. **Reproducible firmware capture:** two independent bounded PROM captures with identical size and SHA-256. Parse the expansion-ROM chain, BIT tokens, Falcon table, native FWSEC descriptor, and image bounds offline.
3. **Read-only engine inventory:** stable GSP/SEC2 geometry, CPU state, DMA aperture, WPR bounds, selectors, scrub status, and firmware scratch values.
4. **Platform/DMA proof:** establish how the GPU reaches the selected physical allocation on that exact firmware. An EFI `Map()` success, an allocation above 4 GiB, or another machine's direct-DMA result is not interchangeable evidence.
5. **Native FRTS-only stage:** before the full path, prove the target's native signed FWSEC can consume the exact buffer and create only the expected volatile WPR2 bounds. This is already GPU-mutating and needs its own one-shot/fail-closed review.
6. **Same-boot loader proof:** preload and hash the exact OS loader before any active write. A return to firmware that causes another GPU POST can erase or alter the state and is not equivalent.
7. **Host tests:** generate a target-specific PROM/profile fixture and inject failures at every successful-path I/O and state-event boundary. Lifecycle tests must prove that no uncertain post-write state frees backing memory or enters the OS.
8. **One-shot field review:** inspect the final source, linked blobs, PE image, hash, USB target, and recovery instructions before one physical attempt.

## Target-dependent profile data

- `include/tu104_profile.h`: exact PCI topology and identity;
- PCI/BAR/PROM constants in the staged wrappers;
- native FWSEC descriptor, image offset, size, and hashes;
- `include/tu104_falcon_profile.h`: Falcon and DMA geometry;
- `include/tu104_unlock_layout.h`: framebuffer-dependent metadata/layout;
- fixed high allocation and UEFI memory-map ownership checks;
- platform setup, root-bridge, MCHBAR/VT-d observations;
- `include/ubuntu_handoff_profile.h`: exact loader and configuration bytes;
- USB label/serial guard in the private local configuration.

Changing one value can invalidate assumptions elsewhere. Review the profile as a whole so that its identity, geometry, and platform assumptions agree.

## Offline analysis tools

- `tools/analyze_option_rom.py`: expansion-ROM structure and identity.
- `tools/analyze_tu104_prom.py`: paired PROM comparison and native FWSEC location/geometry.
- `tools/tu104_port_profile.py`: combines paired captures and repeated register logs into a candidate profile report.
- `make test-public`: hardware-free policy and parser tests.
- `make test PROM_FIXTURE=/path/to/capture.bin`: full private target suite.

The helpers analyze evidence; they do not certify that an active operation is safe. A new target should publish its own input manifest, hashes, locked A/B control, raw measurements, failure history, and limitations.
