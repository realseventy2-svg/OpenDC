OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh4)
ENTRY(_start)

MEMORY
{
    ROM (rx)    : ORIGIN = 0xA0000000, LENGTH = 0x00010000   /* 64 KB Bootloader Flash ROM */
    /* Resident BIOS RAM state: 0x8C000800 to 0x8C000F00 (safe private BIOS workspace).
       Leaves 0x8C001000-0x8C007FFF 100% free for Katana SDK game buffers and CRT0. */
    STATE (rwx) : ORIGIN = 0x8C000800, LENGTH = 0x00000700
}

SECTIONS
{
    /* Stage 1: Cold-boot reset entry point at physical 0xA0000000 */
    .text.entry 0xA0000000 :
    {
        . = ALIGN(4);
        KEEP(*(.text.entry))
    } > ROM

    /* Stage 2: Code and read-only data */
    .text :
    {
        . = ALIGN(4);
        *(.text*)
    } > ROM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata*)
    } > ROM

    /* Stage 3: Initialized data (Stored in ROM, Relocated to Low SDRAM) */
    __data_lma = .;
    .data : AT (__data_lma)
    {
        . = ALIGN(4);
        __data_vma = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > STATE

    __data_size = __data_end - __data_vma;

    /* Zero-initialized RAM memory in Low SDRAM */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > STATE

    __stack_top = 0x8D000000;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
