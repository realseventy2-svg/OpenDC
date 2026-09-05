TARGET   = boot
ROM_SIZE = 2097152

CC      = /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc
AS      = /opt/toolchains/dc/sh-elf/bin/sh-elf-as
OBJCOPY = /opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy

CFLAGS = -m4-single-only -ml -mno-ieee \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -fomit-frame-pointer \
         -O2 \
         -ffunction-sections \
         -fdata-sections

LDFLAGS = -m4-single-only -ml -nostdlib \
          -Wl,-T,linker.ld \
          -Wl,--gc-sections

OBJS = crt0.o video.o boot_anim.o boot_scene.o screen.o sound.o payload.o ata.o gdrom.o iso9660.o gdi_loader.o homebrew_cdi.o selfboot_cdi.o scramble.o syscalls.o wince.o boot.o boot_scene_blob.o

.PHONY: all clean rom

all: $(TARGET).bin dc_boot.bin
rom: dc_boot.bin

crt0.o: src/crt0.s
	$(CC) $(CFLAGS) -c $< -o $@

video.o: src/video.c src/video.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_anim.o: src/boot_anim.c src/boot_anim.h src/video.h src/math_fx.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_scene.o: src/boot_scene.c src/boot_scene.h src/math_fx.h src/sound.h src/video.h
	$(CC) $(CFLAGS) -c $< -o $@

screen.o: src/screen.c src/screen.h src/video.h src/sound.h src/boot_anim.h src/boot_scene.h
	$(CC) $(CFLAGS) -c $< -o $@

sound.o: src/sound.c src/sound.h src/math_fx.h
	$(CC) $(CFLAGS) -c $< -o $@

payload.o: src/payload.c src/video.h src/screen.h src/sound.h
	$(CC) $(CFLAGS) -c $< -o $@

ata.o: src/ata.c src/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

gdrom.o: src/gdrom.c src/gdrom.h src/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

iso9660.o: src/iso9660.c src/iso9660.h src/gdrom.h src/scramble.h src/gdi_loader.h src/homebrew_cdi.h src/selfboot_cdi.h src/screen.h
	$(CC) $(CFLAGS) -c $< -o $@

gdi_loader.o: src/gdi_loader.c src/gdi_loader.h src/gdrom.h src/screen.h
	$(CC) $(CFLAGS) -c $< -o $@

homebrew_cdi.o: src/homebrew_cdi.c src/homebrew_cdi.h src/gdrom.h src/scramble.h src/screen.h
	$(CC) $(CFLAGS) -c $< -o $@

selfboot_cdi.o: src/selfboot_cdi.c src/selfboot_cdi.h src/gdrom.h src/scramble.h src/screen.h
	$(CC) $(CFLAGS) -c $< -o $@

scramble.o: src/scramble.c src/scramble.h
	$(CC) $(CFLAGS) -c $< -o $@

syscalls.o: src/syscalls.c src/syscalls.h src/gdrom.h src/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

wince.o: src/wince.c src/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: src/boot.c src/boot.h src/gdrom.h src/iso9660.h src/syscalls.h src/wince.h src/video.h
	$(CC) $(CFLAGS) -c $< -o $@

# Wrap boot_scene.bin as a linkable ELF object with a .boot_scene section.
# sh-elf-objcopy --input-target binary renames the section to .data by default;
# --rename-section moves it to .boot_scene so the linker script can place it at
# exactly 0xA0010000 (64 KB above the bootloader entry point).
boot_scene_blob.o: boot_scene.bin
	$(OBJCOPY) \
	  --input-target  binary \
	  --output-target elf32-shl \
	  --binary-architecture sh \
	  --rename-section .data=.boot_scene,alloc,load,readonly,data,contents \
	  $< $@

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Build the final 2 MB ROM image:
#   1. Start from the retail ROM base (for font/system tables) or zero-filled.
#   2. Patch the bootloader binary (0xA0000000) and boot_scene (0xA0080000).
#   3. Inject custom_bios.bin (the KOS Dashboard payload) at offset 64 KB (0x10000).
dc_boot.bin: $(TARGET).bin
	python3 -c "\
import os; \
sz = $(ROM_SIZE); \
r_path = '/mnt/d/Github/Personal/KallistiOS/bios/jc-bootROM-retail-v1.032.bin'; \
r = bytearray(open(r_path, 'rb').read()) if os.path.exists(r_path) else bytearray(sz); \
b = open('$<', 'rb').read(); \
r[:len(b)] = b; \
c_candidates = ['../bios/custom_bios.bin', '/mnt/d/Github/Personal/KallistiOS/projects/OpenDC/bios/custom_bios.bin']; \
[r.__setitem__(slice(65536, 65536 + len(open(cp, 'rb').read())), open(cp, 'rb').read()) for cp in c_candidates if os.path.exists(cp)]; \
open('$@', 'wb').write(r[:sz]); \
print(f'Built $@: {sz:,} bytes')"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin dc_boot.bin boot_scene_blob.o


