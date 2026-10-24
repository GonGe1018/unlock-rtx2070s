# Safety and operating boundary

[Overview](../README.md) · [Validation](VALIDATION.md) · [Porting scope](PORTING.md)

This project executes GPU firmware before the operating system. An incorrect DMA address, firmware image, register sequence, or boot handoff can hang the machine, corrupt memory, cause data loss, or leave the GPU unusable until a full power cycle. Volatility describes how long the state persists; the safeguards below address known risks on the validated system.

## Validated configuration

The active `0.8.0` image was validated only on the exact target listed in the root README. It is additionally tied to observed PCI resources, PROM bytes, motherboard/CPU state, UEFI memory descriptors, a fixed high physical allocation, and the installed Ubuntu GRUB image.

Do not run the active image on:

- a different GPU model, PCI/subsystem identity, or VBIOS;
- another motherboard or firmware revision;
- a system with a changed PCI topology, IOMMU/VT-d policy, Above-4G setup, CSM state, or memory map;
- a different GRUB binary/configuration;
- a multi-GPU layout that has not received its own profile and review.

The correct response to a guard refusal is to stop and inspect the durable log. Do not remove, widen, or mask a guard merely to reach the active stage.

## Actions excluded from the active image

- no VBIOS, motherboard firmware, or flash write;
- no EFI `BootOrder`/`BootNext` or other boot-variable write;
- no automatic retry and no persistent every-boot installation;
- no PCI command-register change in the active wrapper;
- no direct host write to SS0/SS1, WPR2, or feature selectors;
- no OS driver patch or kernel-module replacement.

The state is expected to disappear after complete power removal. Hardware and firmware failures remain possible within that scope.

## Operator protocol for the validated machine

1. Stop important workloads and make sure valuable data is backed up.
2. Use the firmware's one-time boot menu. Never make the research USB the permanent/default boot target.
3. The unsigned EFI requires a firmware policy that permits it. Do not erase Secure Boot keys or change a production trust database merely for this test.
4. At `READY`, read the screen. Press `U` exactly once only when the exact target and pre-mutation checks report success. Escape or the 30-second timeout is the safe cancellation path.
5. After `U`, do not press reset, Ctrl-Alt-Delete, or another key to force boot. A successful run starts Ubuntu automatically.
6. If Ubuntu does not start, the screen freezes, output stops unexpectedly, or any post-write failure appears, hold the physical power button until off, switch off/unplug the PSU, and allow power to drain. Do **not** warm reboot.
7. After a successful boot, check the saved EFI log, driver initialization, NVIDIA Xid messages, and PCIe AER messages before running a bounded compute validator.

Before a new independent active attempt, start from a complete power-off. The program itself never retries a possibly partial GPU mutation.

## Memory release and boot policy

The UEFI wrapper marks `write_attempted` immediately before its only MMIO write callback. From that point, backing memory may be released and GRUB may start only if all of these are true:

- the exact GRUB image was already loaded and validated;
- the core returned success;
- the final state is `DONE`/stage 13;
- the unlock result matches the exact SS0, SS1, and mailbox values;
- the GPU reports quiescent cleanup.

Every other post-write combination retains the full 32 MiB pool and halts. The lifecycle policy is exhaustively host-tested over all 256 Boolean state combinations.

## Publication and installation scope

Prebuilt active EFI files, captured card firmware, machine-private logs, and automatic installation are intentionally excluded from the public tree. The Makefile has no `install-unlock-active` target. A source build that succeeds is not evidence that a machine is compatible or authorization to boot the image.

Any new target needs the process in [PORTING.md](PORTING.md), independent review, and its own field evidence. Hardware damage, warranty impact, regulatory rules, and local law remain the operator's responsibility.
