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

OBJS = crt0.o payload.o ata.o gdrom.o iso9660.o scramble.o syscalls.o wince.o boot.o

.PHONY: all clean rom

all: $(TARGET).bin dc_boot.bin
rom: dc_boot.bin

crt0.o: src/crt0.s
	$(CC) $(CFLAGS) -c $< -o $@

payload.o: src/payload.c
	$(CC) $(CFLAGS) -c $< -o $@

ata.o: src/ata.c src/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

gdrom.o: src/gdrom.c src/gdrom.h src/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

iso9660.o: src/iso9660.c src/iso9660.h src/gdrom.h src/scramble.h
	$(CC) $(CFLAGS) -c $< -o $@

scramble.o: src/scramble.c src/scramble.h
	$(CC) $(CFLAGS) -c $< -o $@

syscalls.o: src/syscalls.c src/syscalls.h src/gdrom.h src/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

wince.o: src/wince.c src/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: src/boot.c src/boot.h src/gdrom.h src/iso9660.h src/syscalls.h src/wince.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Pad with retail ROM tables to exactly 2 MB (2,097,152 bytes) for full Katana SDK font/system compatibility
dc_boot.bin: $(TARGET).bin
	python3 -c "import os; r_path='/mnt/d/Github/Personal/KallistiOS/bios/jc-bootROM-retail-v1.032.bin'; r=bytearray(open(r_path,'rb').read() if os.path.exists(r_path) else b'\x00'*$(ROM_SIZE)); b=open('$<','rb').read(); r[:len(b)]=b; open('$@','wb').write(r[:$(ROM_SIZE)]); print(f'Built $@: {$(ROM_SIZE):,} bytes')"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin dc_boot.bin
