CC := gcc
LD := ld
OBJCOPY := objcopy
NM := nm

-include config.local.mk

BUILD_DIR := build
EFI_OBJECT := $(BUILD_DIR)/preflight.o
EFI_SHARED := $(BUILD_DIR)/preflight.so
EFI_IMAGE := $(BUILD_DIR)/BOOTX64.EFI
ROM_OBJECT := $(BUILD_DIR)/rom_capture.o
ROM_SHARED := $(BUILD_DIR)/rom_capture.so
ROM_IMAGE := $(BUILD_DIR)/ROMCAP.EFI
MMIO_OBJECT := $(BUILD_DIR)/mmio_probe.o
MMIO_SHARED := $(BUILD_DIR)/mmio_probe.so
MMIO_IMAGE := $(BUILD_DIR)/MMIOPROBE.EFI
PROM_OBJECT := $(BUILD_DIR)/prom_capture.o
PROM_SHARED := $(BUILD_DIR)/prom_capture.so
PROM_IMAGE := $(BUILD_DIR)/PROMCAP.EFI
REGISTER_OBJECT := $(BUILD_DIR)/register_probe.o
REGISTER_SHARED := $(BUILD_DIR)/register_probe.so
REGISTER_IMAGE := $(BUILD_DIR)/REGPROBE.EFI
DMA_MAP_OBJECT := $(BUILD_DIR)/dma_map_probe.o
DMA_MAP_SHARED := $(BUILD_DIR)/dma_map_probe.so
DMA_MAP_IMAGE := $(BUILD_DIR)/DMAMAP.EFI
HIGH_DMA_MAP_OBJECT := $(BUILD_DIR)/high_dma_map_probe.o
HIGH_DMA_MAP_SHARED := $(BUILD_DIR)/high_dma_map_probe.so
HIGH_DMA_MAP_IMAGE := $(BUILD_DIR)/HIGHMAP.EFI
PLATFORM_DMA_OBJECT := $(BUILD_DIR)/platform_dma_probe.o
PLATFORM_DMA_SHARED := $(BUILD_DIR)/platform_dma_probe.so
PLATFORM_DMA_IMAGE := $(BUILD_DIR)/PLATDMA.EFI
FALCON_STATE_OBJECT := $(BUILD_DIR)/falcon_state_probe.o
FALCON_STATE_SHARED := $(BUILD_DIR)/falcon_state_probe.so
FALCON_STATE_IMAGE := $(BUILD_DIR)/FALCONSTATE.EFI
FWSEC_GUARD_OBJECT := $(BUILD_DIR)/fwsec_guard_probe.o
FWSEC_GUARD_SHARED := $(BUILD_DIR)/fwsec_guard_probe.so
FWSEC_GUARD_IMAGE := $(BUILD_DIR)/FWSECGUARD.EFI
FWSEC_FRTS_OBJECT := $(BUILD_DIR)/fwsec_frts_probe.o
FWSEC_FRTS_SHARED := $(BUILD_DIR)/fwsec_frts_probe.so
FWSEC_FRTS_IMAGE := $(BUILD_DIR)/FWSECFRTS.EFI
HANDOFF_OBJECT := $(BUILD_DIR)/ubuntu_handoff_probe.o
HANDOFF_SHARED := $(BUILD_DIR)/ubuntu_handoff_probe.so
HANDOFF_IMAGE := $(BUILD_DIR)/HANDOFF.EFI
UNLOCK_ACTIVE_OBJECT := $(BUILD_DIR)/tu104_unlock_active.o
UNLOCK_ACTIVE_SHARED := $(BUILD_DIR)/tu104_unlock_active.so
UNLOCK_ACTIVE_IMAGE := $(BUILD_DIR)/TU104UNLOCK.EFI
TU10X_ASSET_DIR := $(BUILD_DIR)/tu10x-assets
GENERIC_BL_BINARY := $(TU10X_ASSET_DIR)/bl_gsp_tu102.bin
GENERIC_BL_OBJECT := $(BUILD_DIR)/bl_gsp_tu102.o
BOOTER_BINARY := $(TU10X_ASSET_DIR)/booter_ucode_prod.bin
BOOTER_SIGNATURE_BINARY := $(TU10X_ASSET_DIR)/booter_sig_prod.bin
BOOTER_OBJECT := $(BUILD_DIR)/booter_ucode_prod.o
BOOTER_SIGNATURE_OBJECT := $(BUILD_DIR)/booter_sig_prod.o
V67_BINARY ?= references/xrip__cmp50hx-unlock/efi-unlock/blobs/v67_payload.bin
BOOT_CONTAINER_BINARY ?= references/xrip__cmp50hx-unlock/efi-unlock/blobs/gsp_rm_boot_dbg.bin
V67_OBJECT := $(BUILD_DIR)/v67_payload.o
BOOT_CONTAINER_OBJECT := $(BUILD_DIR)/gsp_rm_boot_dbg.o
NVIDIA_GENERATED ?= references/NVIDIA-open-gpu-kernel-modules-610.57.04/src/nvidia/generated
PROM_FIXTURE ?= artifacts/2070s-efirom/rtx2070s-gigabyte-90.04.76.00.D5-prom512k-run1.bin
HOST_TEST := $(BUILD_DIR)/test_profile
ROM_TEST := $(BUILD_DIR)/test_option_rom
BAR_TEST := $(BUILD_DIR)/test_pci_bar_resource
DMA_POLICY_TEST := $(BUILD_DIR)/test_dma_map_policy
PLATFORM_DMA_POLICY_TEST := $(BUILD_DIR)/test_platform_dma_policy
FWSEC_LOADER_TEST := $(BUILD_DIR)/test_tu104_fwsec_loader
FALCON_PROFILE_TEST := $(BUILD_DIR)/test_tu104_falcon_profile
FWSEC_PROBE_CORE_TEST := $(BUILD_DIR)/test_tu104_fwsec_probe_core
FWSEC_LIFECYCLE_TEST := $(BUILD_DIR)/test_tu104_fwsec_lifecycle
UNLOCK_LAYOUT_TEST := $(BUILD_DIR)/test_tu104_unlock_layout
UNLOCK_CORE_TEST := $(BUILD_DIR)/test_tu104_unlock_core
UNLOCK_LIFECYCLE_TEST := $(BUILD_DIR)/test_tu104_unlock_lifecycle
HANDOFF_PROFILE_TEST := $(BUILD_DIR)/test_ubuntu_handoff_profile
TENSOR_MMA_DRIVER := $(BUILD_DIR)/tensor_mma_driver
CUBLAS_GEMM_DRIVER := $(BUILD_DIR)/cublas_gemm_driver

EFI_CFLAGS := -O2 -Wall -Wextra -Werror -fpic -ffreestanding \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fshort-wchar -mno-red-zone -maccumulate-outgoing-args -fno-builtin \
	-DGNU_EFI_USE_MS_ABI -I/usr/include/efi -I/usr/include/efi/x86_64 \
	-Iinclude

HOST_CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror -Iinclude

USB_MOUNT ?=
USB_SERIAL ?=

.PHONY: all audit audit-probe audit-prom-capture audit-register-probe \
	audit-dma-map-probe audit-publication test test-public fetch-build-inputs \
	verify-results reproduce-public reproduce-field-build \
	audit-high-dma-map-probe audit-platform-dma-probe \
	audit-falcon-state-probe audit-fwsec-guard-probe audit-fwsec-probe-core \
	audit-fwsec-frts-probe audit-unlock-core audit-unlock-lifecycle \
	audit-handoff-probe audit-unlock-active audit-unlock-active-binary \
	inspect inspect-rom inspect-mmio inspect-prom-capture inspect-register-probe \
	inspect-dma-map-probe inspect-high-dma-map-probe inspect-platform-dma-probe \
	inspect-falcon-state-probe inspect-fwsec-guard-probe \
	inspect-fwsec-frts-probe \
	inspect-handoff-probe inspect-unlock-active \
	rom-capture mmio-probe prom-capture register-probe verify-usb install-usb \
	dma-map-probe high-dma-map-probe platform-dma-probe falcon-state-probe \
	fwsec-guard-probe \
	fwsec-frts-probe \
	handoff-probe unlock-active tensor-mma-driver tensor-bench \
	cublas-gemm-driver cublas-gemm-bench \
	install-rom-capture install-mmio-probe install-prom-capture \
	install-register-probe install-dma-map-probe install-high-dma-map-probe \
	install-platform-dma-probe install-falcon-state-probe \
	port-profile

all: audit test-public $(EFI_IMAGE) inspect

fetch-build-inputs:
	bash scripts/fetch-build-inputs.sh

audit-publication:
	bash scripts/publication-audit.sh

$(BUILD_DIR):
	mkdir -p -- $@

audit:
	bash scripts/safety-audit.sh

audit-probe:
	bash scripts/probe-safety-audit.sh

audit-prom-capture:
	bash scripts/prom-capture-safety-audit.sh

audit-register-probe:
	bash scripts/register-probe-safety-audit.sh

audit-dma-map-probe:
	bash scripts/dma-map-probe-safety-audit.sh

audit-high-dma-map-probe:
	bash scripts/high-dma-map-probe-safety-audit.sh

audit-platform-dma-probe:
	bash scripts/platform-dma-probe-safety-audit.sh

audit-falcon-state-probe:
	bash scripts/falcon-state-probe-safety-audit.sh

audit-fwsec-guard-probe:
	bash scripts/fwsec-guard-probe-safety-audit.sh

audit-fwsec-probe-core:
	bash scripts/fwsec-probe-core-safety-audit.sh

audit-fwsec-frts-probe:
	bash scripts/fwsec-frts-probe-safety-audit.sh

audit-unlock-core:
	bash scripts/unlock-core-safety-audit.sh

audit-unlock-lifecycle:
	bash scripts/unlock-lifecycle-safety-audit.sh

audit-handoff-probe:
	bash scripts/handoff-probe-safety-audit.sh

audit-unlock-active:
	bash scripts/unlock-active-safety-audit.sh

audit-unlock-active-binary: $(UNLOCK_ACTIVE_IMAGE)
	V67_BINARY='$(V67_BINARY)' \
		BOOT_CONTAINER_BINARY='$(BOOT_CONTAINER_BINARY)' \
		bash scripts/unlock-active-binary-audit.sh

$(HOST_TEST): tests/test_profile.c include/tu104_profile.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(ROM_TEST): tests/test_option_rom.c include/option_rom.h \
		include/tu104_profile.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(BAR_TEST): tests/test_pci_bar_resource.c include/pci_bar_resource.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(DMA_POLICY_TEST): tests/test_dma_map_policy.c include/dma_map_policy.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(PLATFORM_DMA_POLICY_TEST): tests/test_platform_dma_policy.c \
		include/platform_dma_policy.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(FWSEC_LOADER_TEST): tests/test_tu104_fwsec_loader.c \
		include/tu104_fwsec_loader.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(FALCON_PROFILE_TEST): tests/test_tu104_falcon_profile.c \
		include/tu104_falcon_profile.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(FWSEC_PROBE_CORE_TEST): tests/test_tu104_fwsec_probe_core.c \
		include/tu104_fwsec_probe_core.h include/tu104_fwsec_loader.h \
		include/tu104_falcon_profile.h $(GENERIC_BL_BINARY) \
		$(PROM_FIXTURE) | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) \
		-DTU104_TEST_PROM_FIXTURE='"$(abspath $(PROM_FIXTURE))"' $< -o $@

$(FWSEC_LIFECYCLE_TEST): tests/test_tu104_fwsec_lifecycle.c \
		include/tu104_fwsec_lifecycle.h include/tu104_fwsec_loader.h | \
		$(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(UNLOCK_LAYOUT_TEST): tests/test_tu104_unlock_layout.c \
		include/tu104_unlock_layout.h $(TU10X_ASSET_DIR)/booter_ucode_prod.bin \
		$(TU10X_ASSET_DIR)/booter_sig_prod.bin $(V67_BINARY) \
		$(BOOT_CONTAINER_BINARY) | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(UNLOCK_CORE_TEST): tests/test_tu104_unlock_core.c \
		include/tu104_unlock_core.h include/tu104_unlock_layout.h \
		include/tu104_fwsec_probe_core.h include/tu104_fwsec_loader.h \
		include/tu104_falcon_profile.h $(TU10X_ASSET_DIR)/booter_ucode_prod.bin \
		$(TU10X_ASSET_DIR)/booter_sig_prod.bin $(GENERIC_BL_BINARY) \
		$(V67_BINARY) $(BOOT_CONTAINER_BINARY) $(PROM_FIXTURE) | \
		$(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) \
		-DTU104_TEST_PROM_FIXTURE='"$(abspath $(PROM_FIXTURE))"' $< -o $@

$(UNLOCK_LIFECYCLE_TEST): tests/test_tu104_unlock_lifecycle.c \
		include/tu104_unlock_lifecycle.h include/tu104_unlock_core.h \
		include/tu104_unlock_layout.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(HANDOFF_PROFILE_TEST): tests/test_ubuntu_handoff_profile.c \
		include/ubuntu_handoff_profile.h include/option_rom.h \
		include/tu104_profile.h | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -o $@

$(TENSOR_MMA_DRIVER): tools/tensor_mma_driver.c | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -lcuda -lm -o $@

$(CUBLAS_GEMM_DRIVER): tools/cublas_gemm_driver.c | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $< -lcuda -ldl -lm -o $@

tensor-mma-driver: $(TENSOR_MMA_DRIVER)

tensor-bench: $(TENSOR_MMA_DRIVER)
	./$(TENSOR_MMA_DRIVER)

cublas-gemm-driver: $(CUBLAS_GEMM_DRIVER)

cublas-gemm-bench: $(CUBLAS_GEMM_DRIVER)
	@test '$(STATE)' = locked -o '$(STATE)' = unlocked || \
		{ echo 'usage: make cublas-gemm-bench STATE=locked|unlocked' >&2; exit 2; }
	./$(CUBLAS_GEMM_DRIVER) --label '$(STATE)'

verify-results:
	python3 tools/verify_public_results.py

reproduce-public: fetch-build-inputs
	$(MAKE) test-public
	$(MAKE) verify-results

reproduce-field-build: unlock-active
	@test "$$(stat -c '%s' $(UNLOCK_ACTIVE_IMAGE))" = 389999
	@test "$$(sha256sum $(UNLOCK_ACTIVE_IMAGE) | awk '{print $$1}')" = \
		053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e
	@echo 'Field-build reproduction: exact 389999-byte active EFI hash matched'

test-public: audit-fwsec-probe-core audit-unlock-core \
		audit-unlock-lifecycle $(HOST_TEST) $(ROM_TEST) $(BAR_TEST) \
		$(DMA_POLICY_TEST) $(PLATFORM_DMA_POLICY_TEST) \
		$(FWSEC_LOADER_TEST) $(FALCON_PROFILE_TEST) \
		$(FWSEC_LIFECYCLE_TEST) $(UNLOCK_LAYOUT_TEST) \
		$(UNLOCK_LIFECYCLE_TEST) $(HANDOFF_PROFILE_TEST)
	./$(HOST_TEST)
	./$(ROM_TEST)
	./$(BAR_TEST)
	./$(DMA_POLICY_TEST)
	./$(PLATFORM_DMA_POLICY_TEST)
	./$(FWSEC_LOADER_TEST)
	./$(FALCON_PROFILE_TEST)
	./$(FWSEC_LIFECYCLE_TEST)
	./$(UNLOCK_LAYOUT_TEST)
	./$(UNLOCK_LIFECYCLE_TEST)
	./$(HANDOFF_PROFILE_TEST)
	python3 -m unittest tests/test_analyze_option_rom.py \
		tests/test_analyze_tu104_prom.py tests/test_extract_tu10x_bindata.py \
		tests/test_tu104_port_profile.py tests/test_verify_public_results.py

test: audit-fwsec-probe-core audit-unlock-core audit-unlock-lifecycle $(HOST_TEST) $(ROM_TEST) $(BAR_TEST) $(DMA_POLICY_TEST) \
		$(PLATFORM_DMA_POLICY_TEST) $(FWSEC_LOADER_TEST) \
		$(FALCON_PROFILE_TEST) $(FWSEC_PROBE_CORE_TEST) \
		$(FWSEC_LIFECYCLE_TEST) $(UNLOCK_LAYOUT_TEST) $(UNLOCK_CORE_TEST) \
		$(UNLOCK_LIFECYCLE_TEST) \
		$(HANDOFF_PROFILE_TEST)
	./$(HOST_TEST)
	./$(ROM_TEST)
	./$(BAR_TEST)
	./$(DMA_POLICY_TEST)
	./$(PLATFORM_DMA_POLICY_TEST)
	./$(FWSEC_LOADER_TEST)
	./$(FALCON_PROFILE_TEST)
	./$(FWSEC_PROBE_CORE_TEST)
	./$(FWSEC_LIFECYCLE_TEST)
	./$(UNLOCK_LAYOUT_TEST)
	./$(UNLOCK_CORE_TEST)
	./$(UNLOCK_LIFECYCLE_TEST)
	./$(HANDOFF_PROFILE_TEST)
	python3 -m unittest tests/test_analyze_option_rom.py \
		tests/test_analyze_tu104_prom.py tests/test_extract_tu10x_bindata.py \
		tests/test_tu104_port_profile.py tests/test_verify_public_results.py

port-profile: test
	python3 -m tools.tu104_port_profile \
		artifacts/2070s-efirom/rtx2070s-gigabyte-90.04.76.00.D5-prom512k-run1.bin \
		--second-capture artifacts/2070s-efirom/rtx2070s-gigabyte-90.04.76.00.D5-prom512k-run2.bin \
		--register-log artifacts/2070s-efirom/register-probe-0.5.0-run1.log \
		--register-log artifacts/2070s-efirom/register-probe-0.5.0-run2.log

$(EFI_OBJECT): src/preflight.c include/tu104_profile.h \
		include/option_rom.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) $< -o $@

$(ROM_OBJECT): src/preflight.c include/tu104_profile.h \
		include/option_rom.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_CAPTURE_ROM $< -o $@

$(MMIO_OBJECT): src/preflight.c src/mmio_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/pci_bar_resource.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_MMIO_PROBE $< -o $@

$(PROM_OBJECT): src/preflight.c src/prom_capture.inc \
		include/tu104_profile.h include/option_rom.h \
		include/pci_bar_resource.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_PROM_CAPTURE $< -o $@

$(REGISTER_OBJECT): src/preflight.c src/register_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/pci_bar_resource.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_REGISTER_PROBE $< -o $@

$(DMA_MAP_OBJECT): src/preflight.c src/dma_map_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/dma_map_policy.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_DMA_MAP_PROBE $< -o $@

$(HIGH_DMA_MAP_OBJECT): src/preflight.c src/high_dma_map_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/dma_map_policy.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_HIGH_DMA_MAP_PROBE $< -o $@

$(PLATFORM_DMA_OBJECT): src/preflight.c src/platform_dma_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/platform_dma_policy.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_PLATFORM_DMA_PROBE $< -o $@

$(FALCON_STATE_OBJECT): src/preflight.c src/falcon_state_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/pci_bar_resource.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_FALCON_STATE_PROBE $< -o $@

$(FWSEC_GUARD_OBJECT): src/preflight.c src/platform_dma_probe.inc \
		src/fwsec_frts_probe.inc src/fwsec_guard_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/platform_dma_policy.h include/tu104_fwsec_probe_core.h \
		include/tu104_fwsec_loader.h include/tu104_falcon_profile.h \
		include/tu104_fwsec_lifecycle.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_FWSEC_GUARD_PROBE $< -o $@

$(GENERIC_BL_BINARY): tools/extract_tu10x_bindata.py \
		$(NVIDIA_GENERATED)/g_bindata_ksec2GetBinArchiveBlUcode_TU102.c | \
		$(BUILD_DIR)
	mkdir -p -- $(TU10X_ASSET_DIR)
	python3 -m tools.extract_tu10x_bindata $(NVIDIA_GENERATED) \
		--output $(TU10X_ASSET_DIR)

$(GENERIC_BL_OBJECT): $(GENERIC_BL_BINARY) | $(BUILD_DIR)
	cd $(TU10X_ASSET_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 \
		-B i386:x86-64 bl_gsp_tu102.bin $(abspath $@)
	$(OBJCOPY) --rename-section \
		.data=.rodata,alloc,load,readonly,data,contents $@

$(BOOTER_OBJECT): $(BOOTER_BINARY) | $(BUILD_DIR)
	cd $(TU10X_ASSET_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 \
		-B i386:x86-64 booter_ucode_prod.bin $(abspath $@)
	$(OBJCOPY) --rename-section \
		.data=.rodata,alloc,load,readonly,data,contents $@

$(BOOTER_SIGNATURE_OBJECT): $(BOOTER_SIGNATURE_BINARY) | $(BUILD_DIR)
	cd $(TU10X_ASSET_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 \
		-B i386:x86-64 booter_sig_prod.bin $(abspath $@)
	$(OBJCOPY) --rename-section \
		.data=.rodata,alloc,load,readonly,data,contents $@

$(V67_OBJECT): $(V67_BINARY) | $(BUILD_DIR)
	cd $(dir $(V67_BINARY)) && $(OBJCOPY) -I binary -O elf64-x86-64 \
		-B i386:x86-64 $(notdir $(V67_BINARY)) $(abspath $@)
	$(OBJCOPY) --rename-section \
		.data=.rodata,alloc,load,readonly,data,contents $@

$(BOOT_CONTAINER_OBJECT): $(BOOT_CONTAINER_BINARY) | $(BUILD_DIR)
	cd $(dir $(BOOT_CONTAINER_BINARY)) && $(OBJCOPY) \
		-I binary -O elf64-x86-64 -B i386:x86-64 \
		$(notdir $(BOOT_CONTAINER_BINARY)) $(abspath $@)
	$(OBJCOPY) --rename-section \
		.data=.rodata,alloc,load,readonly,data,contents $@

$(FWSEC_FRTS_OBJECT): src/preflight.c src/platform_dma_probe.inc \
		src/fwsec_frts_probe.inc include/tu104_profile.h include/option_rom.h \
		include/platform_dma_policy.h include/tu104_fwsec_probe_core.h \
		include/tu104_fwsec_loader.h include/tu104_falcon_profile.h \
		include/tu104_fwsec_lifecycle.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_FWSEC_FRTS_PROBE $< -o $@

$(HANDOFF_OBJECT): src/preflight.c src/ubuntu_handoff_probe.inc \
		include/tu104_profile.h include/option_rom.h \
		include/ubuntu_handoff_profile.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_HANDOFF_PROBE $< -o $@

$(UNLOCK_ACTIVE_OBJECT): src/preflight.c src/platform_dma_probe.inc \
		src/fwsec_frts_probe.inc src/ubuntu_handoff_probe.inc \
		src/tu104_unlock_active.inc include/tu104_profile.h \
		include/option_rom.h include/platform_dma_policy.h \
		include/tu104_fwsec_probe_core.h include/tu104_fwsec_loader.h \
		include/tu104_falcon_profile.h include/tu104_unlock_layout.h \
		include/tu104_unlock_core.h include/tu104_unlock_lifecycle.h \
		include/ubuntu_handoff_profile.h | $(BUILD_DIR)
	$(CC) -c $(EFI_CFLAGS) -DTU104_UNLOCK_ACTIVE $< -o $@

$(EFI_SHARED): $(EFI_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(EFI_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(ROM_SHARED): $(ROM_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(ROM_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(MMIO_SHARED): $(MMIO_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(MMIO_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(PROM_SHARED): $(PROM_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(PROM_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(REGISTER_SHARED): $(REGISTER_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(REGISTER_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(DMA_MAP_SHARED): $(DMA_MAP_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(DMA_MAP_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(HIGH_DMA_MAP_SHARED): $(HIGH_DMA_MAP_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(HIGH_DMA_MAP_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(PLATFORM_DMA_SHARED): $(PLATFORM_DMA_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(PLATFORM_DMA_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(FALCON_STATE_SHARED): $(FALCON_STATE_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(FALCON_STATE_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(FWSEC_GUARD_SHARED): $(FWSEC_GUARD_OBJECT) $(GENERIC_BL_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(FWSEC_GUARD_OBJECT) \
		$(GENERIC_BL_OBJECT) -L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(FWSEC_FRTS_SHARED): $(FWSEC_FRTS_OBJECT) $(GENERIC_BL_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(FWSEC_FRTS_OBJECT) \
		$(GENERIC_BL_OBJECT) -L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(HANDOFF_SHARED): $(HANDOFF_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(HANDOFF_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(UNLOCK_ACTIVE_SHARED): $(UNLOCK_ACTIVE_OBJECT) $(GENERIC_BL_OBJECT) \
		$(BOOTER_OBJECT) $(BOOTER_SIGNATURE_OBJECT) $(V67_OBJECT) \
		$(BOOT_CONTAINER_OBJECT)
	$(LD) -nostdlib -znocombreloc -z noexecstack \
		-T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o $(UNLOCK_ACTIVE_OBJECT) \
		$(GENERIC_BL_OBJECT) $(BOOTER_OBJECT) $(BOOTER_SIGNATURE_OBJECT) \
		$(V67_OBJECT) $(BOOT_CONTAINER_OBJECT) \
		-L/usr/lib -lgnuefi -lefi -o $@
	@test -z "$$($(NM) -u $@ 2>/dev/null)"

$(EFI_IMAGE): $(EFI_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(ROM_IMAGE): $(ROM_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(MMIO_IMAGE): $(MMIO_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(PROM_IMAGE): $(PROM_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(REGISTER_IMAGE): $(REGISTER_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(DMA_MAP_IMAGE): $(DMA_MAP_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(HIGH_DMA_MAP_IMAGE): $(HIGH_DMA_MAP_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(PLATFORM_DMA_IMAGE): $(PLATFORM_DMA_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(FALCON_STATE_IMAGE): $(FALCON_STATE_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(FWSEC_GUARD_IMAGE): $(FWSEC_GUARD_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(FWSEC_FRTS_IMAGE): $(FWSEC_FRTS_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(HANDOFF_IMAGE): $(HANDOFF_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

$(UNLOCK_ACTIVE_IMAGE): $(UNLOCK_ACTIVE_SHARED)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata \
		-j .dynamic -j .dynsym -j .reloc -j .rel -j .rela \
		-j '.rel.*' -j '.rela.*' --target=efi-app-x86_64 $< $@

inspect: $(EFI_IMAGE)
	file $(EFI_IMAGE)
	sha256sum $(EFI_IMAGE)

inspect-rom: $(ROM_IMAGE)
	file $(ROM_IMAGE)
	sha256sum $(ROM_IMAGE)

rom-capture: audit test-public $(ROM_IMAGE) inspect-rom

inspect-mmio: $(MMIO_IMAGE)
	file $(MMIO_IMAGE)
	sha256sum $(MMIO_IMAGE)

mmio-probe: audit audit-probe test-public $(MMIO_IMAGE) inspect-mmio

inspect-prom-capture: $(PROM_IMAGE)
	file $(PROM_IMAGE)
	sha256sum $(PROM_IMAGE)

prom-capture: audit audit-prom-capture test-public $(PROM_IMAGE) inspect-prom-capture

inspect-register-probe: $(REGISTER_IMAGE)
	file $(REGISTER_IMAGE)
	sha256sum $(REGISTER_IMAGE)

register-probe: audit audit-register-probe test-public $(REGISTER_IMAGE) inspect-register-probe

inspect-dma-map-probe: $(DMA_MAP_IMAGE)
	file $(DMA_MAP_IMAGE)
	sha256sum $(DMA_MAP_IMAGE)

dma-map-probe: audit audit-dma-map-probe test-public $(DMA_MAP_IMAGE) inspect-dma-map-probe

inspect-high-dma-map-probe: $(HIGH_DMA_MAP_IMAGE)
	file $(HIGH_DMA_MAP_IMAGE)
	sha256sum $(HIGH_DMA_MAP_IMAGE)

high-dma-map-probe: audit audit-high-dma-map-probe test-public \
		$(HIGH_DMA_MAP_IMAGE) inspect-high-dma-map-probe

inspect-platform-dma-probe: $(PLATFORM_DMA_IMAGE)
	file $(PLATFORM_DMA_IMAGE)
	sha256sum $(PLATFORM_DMA_IMAGE)

platform-dma-probe: audit audit-platform-dma-probe test-public \
		$(PLATFORM_DMA_IMAGE) inspect-platform-dma-probe

inspect-falcon-state-probe: $(FALCON_STATE_IMAGE)
	file $(FALCON_STATE_IMAGE)
	sha256sum $(FALCON_STATE_IMAGE)

falcon-state-probe: audit audit-falcon-state-probe test-public \
		$(FALCON_STATE_IMAGE) inspect-falcon-state-probe

inspect-fwsec-guard-probe: $(FWSEC_GUARD_IMAGE)
	file $(FWSEC_GUARD_IMAGE)
	sha256sum $(FWSEC_GUARD_IMAGE)

fwsec-guard-probe: audit audit-platform-dma-probe \
		audit-fwsec-guard-probe test $(FWSEC_GUARD_IMAGE) \
		inspect-fwsec-guard-probe

inspect-fwsec-frts-probe: $(FWSEC_FRTS_IMAGE)
	file $(FWSEC_FRTS_IMAGE)
	sha256sum $(FWSEC_FRTS_IMAGE)

fwsec-frts-probe: audit audit-platform-dma-probe audit-fwsec-probe-core \
		audit-fwsec-frts-probe test \
		$(FWSEC_FRTS_IMAGE) inspect-fwsec-frts-probe

inspect-handoff-probe: $(HANDOFF_IMAGE)
	file $(HANDOFF_IMAGE)
	sha256sum $(HANDOFF_IMAGE)

handoff-probe: audit audit-handoff-probe test-public \
		$(HANDOFF_IMAGE) inspect-handoff-probe

inspect-unlock-active: $(UNLOCK_ACTIVE_IMAGE)
	file $(UNLOCK_ACTIVE_IMAGE)
	sha256sum $(UNLOCK_ACTIVE_IMAGE)

unlock-active: audit audit-unlock-core audit-unlock-lifecycle \
		audit-unlock-active test $(UNLOCK_ACTIVE_IMAGE) \
		audit-unlock-active-binary inspect-unlock-active

verify-usb:
	@test -n '$(USB_MOUNT)' || { \
		echo 'USB_MOUNT is unset; define the exact mounted FAT volume in config.local.mk' >&2; \
		exit 2; \
	}
	@test -n '$(USB_SERIAL)' || { \
		echo 'USB_SERIAL is unset; put the exact removable-disk serial in config.local.mk' >&2; \
		exit 2; \
	}
	@source="$$(findmnt -n -o SOURCE --target '$(USB_MOUNT)')"; \
		test -n "$$source" && test -b "$$source"; \
		parent="$$(lsblk -ndo PKNAME "$$source" | sed 's/[[:space:]]*$$//')"; \
		test -n "$$parent" && disk="/dev/$$parent" && test -b "$$disk"; \
		test "$$(findmnt -n -o FSTYPE --target '$(USB_MOUNT)')" = vfat; \
		test "$$(lsblk -dn -o SERIAL "$$disk" | sed 's/[[:space:]]*$$//')" = '$(USB_SERIAL)'; \
		test "$$(lsblk -dn -o MODEL "$$disk" | sed 's/[[:space:]]*$$//')" = 'SanDisk 3.2Gen1'; \
		test "$$(lsblk -dn -o LABEL "$$source" | sed 's/[[:space:]]*$$//')" = RTX2070S; \
		test -w '$(USB_MOUNT)'

install-usb: all verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/TU104-UNLOCK/LOGS'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-rom-capture: all rom-capture verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		$(ROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-mmio-probe: all rom-capture mmio-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	install -m 0644 $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	cmp -- $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		$(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		$(MMIO_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-prom-capture: all rom-capture mmio-probe prom-capture verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	install -m 0644 $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	install -m 0644 $(PROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	cmp -- $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	cmp -- $(PROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		$(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		$(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		$(PROM_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-register-probe: all rom-capture mmio-probe prom-capture \
		register-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	install -m 0644 $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	install -m 0644 $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	install -m 0644 $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	cmp -- $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	cmp -- $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	cmp -- $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		$(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		$(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		$(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI' \
		$(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-dma-map-probe: all rom-capture mmio-probe prom-capture \
		register-probe dma-map-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	install -m 0644 $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	install -m 0644 $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	install -m 0644 $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI'
	install -m 0644 $(DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	cmp -- $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	cmp -- $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	cmp -- $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI'
	cmp -- $(DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		$(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		$(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		$(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI' \
		$(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI' \
		$(DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-high-dma-map-probe: all rom-capture mmio-probe prom-capture \
		register-probe dma-map-probe high-dma-map-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS' \
		'$(USB_MOUNT)/TU104-UNLOCK/CAPTURES'
	install -m 0644 $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	install -m 0644 $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	install -m 0644 $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	install -m 0644 $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	install -m 0644 $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI'
	install -m 0644 $(DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/DMAMAP.EFI'
	install -m 0644 $(HIGH_DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI' \
		'$(USB_MOUNT)/EFI/TOOLS/DMAMAP.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(EFI_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PREFLIGHT.EFI'
	cmp -- $(ROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/ROMCAP.EFI'
	cmp -- $(MMIO_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/MMIOPROBE.EFI'
	cmp -- $(PROM_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PROMCAP.EFI'
	cmp -- $(REGISTER_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/REGPROBE.EFI'
	cmp -- $(DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/DMAMAP.EFI'
	cmp -- $(HIGH_DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(HIGH_DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-platform-dma-probe: platform-dma-probe high-dma-map-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS'
	install -m 0644 $(HIGH_DMA_MAP_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/HIGHMAP.EFI'
	install -m 0644 $(PLATFORM_DMA_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/HIGHMAP.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(PLATFORM_DMA_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(PLATFORM_DMA_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'

install-falcon-state-probe: falcon-state-probe platform-dma-probe verify-usb
	mkdir -p -- '$(USB_MOUNT)/EFI/BOOT' '$(USB_MOUNT)/EFI/TOOLS' \
		'$(USB_MOUNT)/TU104-UNLOCK/LOGS'
	install -m 0644 $(PLATFORM_DMA_IMAGE) \
		'$(USB_MOUNT)/EFI/TOOLS/PLATDMA.EFI'
	install -m 0644 $(FALCON_STATE_IMAGE) \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sync '$(USB_MOUNT)/EFI/TOOLS/PLATDMA.EFI' \
		'$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	cmp -- $(PLATFORM_DMA_IMAGE) '$(USB_MOUNT)/EFI/TOOLS/PLATDMA.EFI'
	cmp -- $(FALCON_STATE_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
	sha256sum $(PLATFORM_DMA_IMAGE) \
		'$(USB_MOUNT)/EFI/TOOLS/PLATDMA.EFI' \
		$(FALCON_STATE_IMAGE) '$(USB_MOUNT)/EFI/BOOT/BOOTX64.EFI'
