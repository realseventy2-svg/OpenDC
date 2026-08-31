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

OBJS = crt0.o payload.o gdrom.o

.PHONY: all clean rom

all: $(TARGET).bin dc_boot.bin
rom: dc_boot.bin

crt0.o: src/crt0.s
	$(CC) $(CFLAGS) -c $< -o $@

payload.o: src/payload.c
	$(CC) $(CFLAGS) -c $< -o $@

gdrom.o: src/gdrom.c src/gdrom.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Pad to exactly 2 MB (2,097,152 bytes) for BootROM
dc_boot.bin: $(TARGET).bin
	python3 -c "d=open('$<','rb').read(); open('$@','wb').write((d + b'\x00'*($(ROM_SIZE)-len(d)))[:$(ROM_SIZE)]); print(f'Built $@: {$(ROM_SIZE):,} bytes')"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin dc_boot.bin
