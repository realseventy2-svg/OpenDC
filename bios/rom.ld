/* ============================================================================
 * rom.ld -- Custom GNU LD Linker Script for ROM-to-RAM Relocation
 *
 * Maps:
 *   LMA (Load Memory Address, physical flash location) = 0xA0000000 (P2, ROM)
 *   VMA (Virtual/execution address for .data) in RAM    = 0x8C010000
 *
 * .text / .rodata execute in place (XIP) directly from ROM.
 * .data is stored in ROM but relocated to RAM by crt0.s at boot.
 * .bss lives in RAM only (zero-filled at boot, occupies no ROM space).
 * ========================================================================== */

OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
    rom (rx)  : ORIGIN = 0xA0000000, LENGTH = 2M
    ram (rwx) : ORIGIN = 0x8C010000, LENGTH = 8M
}

SECTIONS
{
    /* Stage 1 reset vector must land at the very base of ROM */
    .text.start 0xA0000000 :
    {
        KEEP(*(.text.start))
    } > rom

    /* Stage 2/3 code + read-only data, executed in place from ROM */
    .text :
    {
        *(.text)
        *(.text.*)
        *(.init)
        *(.fini)
        *(.eh_frame*)
    } > rom

    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    } > rom

    /* Initialized data: stored in ROM (LMA), relocated to RAM (VMA) */
    __data_lma = .;
    .data : AT (__data_lma)
    {
        __data_vma = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        __data_end = .;
    } > ram

    __data_size = __data_end - __data_vma;

    /* Zero-initialized data: RAM only, not stored in the ROM image */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    .tdata : {
        . = ALIGN(4);
        __tdata_start = .;
        *(.tdata .tdata.*)
        __tdata_end = .;
    } > ram
    __tdata_align = ALIGNOF(.tdata);

    .tbss : {
        . = ALIGN(4);
        __tbss_start = .;
        *(.tbss .tbss.*)
        __tbss_end = .;
    } > ram
    __tbss_align = ALIGNOF(.tbss);

    _end = .;
    PROVIDE(end = .);

    /* Reserve the rest of RAM for stack/heap; stack grows down from top */
    __stack_top = ORIGIN(ram) + LENGTH(ram);

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
