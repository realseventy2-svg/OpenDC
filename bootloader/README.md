# Dreamcast bare-metal BootROM experiment

Target: SH-4 Dreamcast BootROM address space 0xA0000000, 2 MiB ROM.

The supplied `dc_boot.bin` is 0x200000 bytes and contains the string
`SEGA SEGAKATANA KABUTO Ver.1.01d` at ROM offset 0x7B0.

This project is a minimal ROM-resident bring-up. It intentionally does NOT
claim to reproduce Sega's SDRAM/BSC initialization. The next safe stage is to
lift the exact BSC/SDRAM initialization from the supplied ROM and insert it
before switching to a RAM stack.

The Dreamcast starts execution from 0xA0000000. System RAM is 16 MiB at the
0x8C000000 cached P1 alias after initialization.

Do not overwrite a physical Dreamcast BootROM without a recoverable external
replacement/mod and a verified emulator/test path.

## GD-ROM service layer

`src/gdrom.c` provides the verified G1 ATA register transport used by a
standalone stage: PIO timing setup, drive status, ATA PACKET setup, packet
submission, PIO data phases, and bounded waits. It does not guess Sega GD-ROM
packet opcodes and it does not overwrite the KOS syscall vectors. A caller must
provide the correct 12-byte Sega packet sequence for the operation it needs.

The bootloader publishes a versioned service table at `0x8C00F000` before
chainloading. It currently exposes drive status, TOC reads, and FAD sector
reads. These services are opt-in; startup does not touch the GD-ROM, so an
empty or unsupported drive cannot block the bootloader splash.
