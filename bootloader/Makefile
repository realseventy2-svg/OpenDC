TARGET   = boot
ROM_SIZE = 2097152

CC      = /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc
AS      = /opt/toolchains/dc/sh-elf/bin/sh-elf-as
OBJCOPY = /opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy

INCLUDES = -Isrc/core \
           -Isrc/storage \
           -Isrc/video \
           -Isrc/audio \
           -Isrc/scene \
           -Isrc/fallback_anim \
           -Isrc

CFLAGS = -m4-single-only -ml -mno-ieee \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -fomit-frame-pointer \
         -O2 \
         -ffunction-sections \
         -fdata-sections \
         $(INCLUDES)

LDFLAGS = -m4-single-only -ml -nostdlib \
          -Wl,-T,linker.ld \
          -Wl,--gc-sections

# Subsystem Object Groups
CORE_OBJS     = crt0.o syscalls.o wince.o payload.o
STORAGE_OBJS  = ata.o gdrom.o iso9660.o gdi_loader.o selfboot_cdi.o homebrew_cdi.o scramble.o boot.o
VIDEO_OBJS    = video.o screen.o
AUDIO_OBJS    = sound.o
SCENE_OBJS    = scene_loader.o rasterizer.o postprocess.o sprite.o scene_render.o boot_scene_blob.o
FALLBACK_OBJS = boot_anim.o

OBJS = $(CORE_OBJS) $(STORAGE_OBJS) $(VIDEO_OBJS) $(AUDIO_OBJS) $(SCENE_OBJS) $(FALLBACK_OBJS)

.PHONY: all clean rom

all: $(TARGET).bin dc_boot.bin
rom: dc_boot.bin

# 1. Core Subsystem
crt0.o: src/core/crt0.s
	$(CC) $(CFLAGS) -c $< -o $@

syscalls.o: src/core/syscalls.c src/core/syscalls.h
	$(CC) $(CFLAGS) -c $< -o $@

wince.o: src/core/wince.c src/core/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

payload.o: src/core/payload.c
	$(CC) $(CFLAGS) -c $< -o $@

# 2. Storage & Boot Subsystem
ata.o: src/storage/ata.c src/storage/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

gdrom.o: src/storage/gdrom.c src/storage/gdrom.h
	$(CC) $(CFLAGS) -c $< -o $@

iso9660.o: src/storage/iso9660.c src/storage/iso9660.h
	$(CC) $(CFLAGS) -c $< -o $@

gdi_loader.o: src/storage/gdi_loader.c src/storage/gdi_loader.h
	$(CC) $(CFLAGS) -c $< -o $@

selfboot_cdi.o: src/storage/selfboot_cdi.c src/storage/selfboot_cdi.h
	$(CC) $(CFLAGS) -c $< -o $@

homebrew_cdi.o: src/storage/homebrew_cdi.c src/storage/homebrew_cdi.h
	$(CC) $(CFLAGS) -c $< -o $@

scramble.o: src/storage/scramble.c src/storage/scramble.h
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: src/storage/boot.c src/storage/boot.h
	$(CC) $(CFLAGS) -c $< -o $@

# 3. Video Subsystem
video.o: src/video/video.c src/video/video.h
	$(CC) $(CFLAGS) -c $< -o $@

screen.o: src/video/screen.c src/video/screen.h
	$(CC) $(CFLAGS) -c $< -o $@

# 4. Audio Subsystem
sound.o: src/audio/sound.c src/audio/sound.h
	$(CC) $(CFLAGS) -c $< -o $@

# 5. Modular 3D Boot Scene Engine
scene_loader.o: src/scene/scene_loader.c src/scene/scene_loader.h src/scene/scene_types.h
	$(CC) $(CFLAGS) -c $< -o $@

rasterizer.o: src/scene/rasterizer.c src/scene/rasterizer.h
	$(CC) $(CFLAGS) -c $< -o $@

postprocess.o: src/scene/postprocess.c src/scene/postprocess.h
	$(CC) $(CFLAGS) -c $< -o $@

sprite.o: src/scene/sprite.c src/scene/sprite.h
	$(CC) $(CFLAGS) -c $< -o $@

scene_render.o: src/scene/scene_render.c src/scene/boot_scene.h src/scene/scene_types.h src/scene/scene_math.h
	$(CC) $(CFLAGS) -c $< -o $@

# 6. Fallback 2D Splash Engine
boot_anim.o: src/fallback_anim/boot_anim.c src/fallback_anim/boot_anim.h
	$(CC) $(CFLAGS) -c $< -o $@

# Wrap boot_scene.bin as a linkable ELF object with a .boot_scene section.
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
