TARGET = custom_bios
# This is a normal KOS payload injected into the BIOS loader.  Deliberately
# do not add crt0.o or src/entry.o: KOS supplies the startup/runtime entry.
OBJS = src/main.o romdisk.o
# The unified build generates this base from bootloader/dc_boot.bin. Override
# this on the command line only when intentionally testing another loader.
BIOS_BASE ?= res/boot_loader_devkit.bios

# The BIOS loader has a limited payload window after the 64 KiB ROM offset.
# Use size optimization and omit optional audio assets from the romdisk.
CFLAGS += -Os -flto=auto -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections -s
LIBS += -lz

all: $(TARGET).elf $(TARGET).bin rom cdi

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(OBJS) crt0.o src/entry.o romdisk.img romdisk.o $(TARGET).elf $(TARGET).bin $(TARGET).cdi custom_dc_bios.bin

romdisk.img: romdisk/rungd.bin.gz
	$(KOS_GENROMFS) -f romdisk.img -d romdisk -v -x .gitignore -x .DS_Store -x Thumbs.db -x bios_theme.wav -x boot_chime.wav

romdisk/rungd.bin.gz: ../../DreamDash/res/rungd.bin
	gzip -9 -c $< > $@

romdisk.o: romdisk.img
	/opt/toolchains/dc/kos/utils/bin2o/bin2o romdisk.img romdisk romdisk.o

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
	../../../scripts/make-cdi.sh $(TARGET).elf $(TARGET).cdi "Custom KOS BIOS"
