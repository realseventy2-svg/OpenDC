TARGET = custom_bios

# Backend services objects
BACKEND_OBJS = src/backend/audio_driver.o \
               src/backend/disc_service.o \
               src/backend/flashrom_service.o \
               src/backend/maple_service.o \
               src/backend/memory_service.o \
               src/backend/sysinfo_service.o

# GUI presentation & render objects
GUI_OBJS = src/gui/render/renderer.o \
           src/gui/render/bfont_data.o \
           src/gui/screens/screen_main_menu.o \
           src/gui/screens/screen_sysinfo.o \
           src/gui/screens/screen_maple.o \
           src/gui/screens/screen_flashrom.o \
           src/gui/screens/screen_memory.o \
           src/gui/screens/screen_test.o

# Main payload objects
OBJS = src/main.o $(BACKEND_OBJS) $(GUI_OBJS) romdisk.o

# The unified build generates this base from bootloader/dc_boot.bin. Override
# this on the command line only when intentionally testing another loader.
BIOS_BASE ?= res/boot_loader_devkit.bios

# The BIOS loader has a limited payload window after the 64 KiB ROM offset.
# Use size optimization and omit optional audio assets from the romdisk.
CFLAGS += -Os -flto=auto -ffunction-sections -fdata-sections -Isrc -Isrc/backend -Isrc/gui/render -Isrc/gui/screens
LDFLAGS += -Wl,--gc-sections -s $(if $(wildcard /opt/toolchains/dc/sh-elf/lib),-L/opt/toolchains/dc/sh-elf/lib,)
LIBS += -lz

BIN2O ?= $(shell which bin2o 2>/dev/null || echo $(KOS_BASE)/utils/bin2o/bin2o)

all: $(TARGET).elf $(TARGET).bin rom

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(OBJS) crt0.o src/entry.o romdisk.img romdisk.o $(TARGET).elf $(TARGET).bin $(TARGET).cdi custom_dc_bios.bin src/*.o src/backend/*.o src/gui/render/*.o src/gui/screens/*.o

# rungd.bin.gz is pre-built and checked in — no DreamDash dependency needed.
romdisk/rungd.bin.gz:
	@test -f $@ || (echo "ERROR: romdisk/rungd.bin.gz is missing. Re-check the repository." && exit 1)

romdisk.img: romdisk/rungd.bin.gz
	$(KOS_GENROMFS) -f romdisk.img -d romdisk -v -x .gitignore -x .DS_Store -x Thumbs.db -x bios_theme.wav -x boot_chime.wav

romdisk.o: romdisk.img
	$(BIN2O) romdisk.img romdisk romdisk.o

$(TARGET).elf: $(OBJS)
	kos-cc $(LDFLAGS) -o $(TARGET).elf $(OBJS) $(LIBS)

$(TARGET).bin: $(TARGET).elf
	$(KOS_OBJCOPY) -R .stack -O binary $(TARGET).elf $(TARGET).bin

# 100% Custom Bare-Metal Bootloader + 60 FPS Custom KOS Payload
rom: $(TARGET).bin $(BIOS_BASE)
	cp -f $(BIOS_BASE) custom_dc_bios.bin
	dd if=$(TARGET).bin of=custom_dc_bios.bin bs=1024 seek=64 conv=notrunc
	@echo "=== Custom BIOS with retail GD-ROM services assembled ==="

cdi: $(TARGET).elf
	@if [ -x "../../../scripts/make-cdi.sh" ]; then \
		../../../scripts/make-cdi.sh $(TARGET).elf $(TARGET).cdi "Custom KOS BIOS"; \
	elif which makedisc >/dev/null 2>&1; then \
		makedisc -cdi -output $(TARGET).cdi $(TARGET).bin; \
	else \
		echo "Notice: CDI creation tool (make-cdi.sh / makedisc) not found; skipping CDI target."; \
	fi
