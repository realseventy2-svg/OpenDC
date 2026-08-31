# Unified custom Dreamcast firmware build.
#
# The two source projects remain separate internally because they use
# different link environments:
#   bootloader: freestanding SH-4 ROM code
#   bios:       KOS payload injected at ROM offset 0x10000

ROOT_DIR = $(abspath $(CURDIR))
BOOT_DIR = $(ROOT_DIR)/bootloader
BIOS_DIR = $(ROOT_DIR)/bios
OUTPUT   = $(CURDIR)/boot_loader_custom.bios

.PHONY: all bootloader bios check clean

all: $(OUTPUT)

bootloader:
	$(MAKE) -C "$(BOOT_DIR)"

bios: bootloader
	cp -f "$(BOOT_DIR)/dc_boot.bin" "$(BIOS_DIR)/res/boot_loader_custom.bios"
	$(MAKE) -C "$(BIOS_DIR)" clean
	$(MAKE) -C "$(BIOS_DIR)" rom

$(OUTPUT): bios
	cp -f "$(BIOS_DIR)/custom_dc_bios.bin" "$@"
	@test "$$(wc -c < "$@")" -eq 2097152
	@echo "Built $@: 2097152 bytes"

check: $(OUTPUT)
	@test "$$(wc -c < "$(BOOT_DIR)/dc_boot.bin")" -eq 2097152
	@test "$$(wc -c < "$(BIOS_DIR)/custom_dc_bios.bin")" -eq 2097152
	@echo "Firmware image checks passed"

clean:
	$(MAKE) -C "$(BOOT_DIR)" clean
	$(MAKE) -C "$(BIOS_DIR)" clean
	rm -f "$(OUTPUT)"
