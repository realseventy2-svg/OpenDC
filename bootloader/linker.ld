OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh4)
ENTRY(_start)

MEMORY
{
    ROM (rx)    : ORIGIN = 0xA0000000, LENGTH = 0x00200000   /* 2 MB Flash ROM */
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

    /*
     * DCBS plug-and-play container — placed at 0xA0080000 (ROM + 512 KB).
     * Placed after custom_bios.bin (0xA0010000..0xA007FFFF, 448 KB) to prevent
     * payload collision and ensure clean chainloading into the KOS dashboard.
     * The linker symbol _boot_scene_bin_start exposes the direct ROM pointer.
     */
    .boot_scene 0xA0080000 :
    {
        . = ALIGN(64);
        _boot_scene_bin_start = .;
        __boot_scene_bin_start = .;
        boot_scene_bin_start = .;
        KEEP(*(.boot_scene*))
        _boot_scene_bin_end = .;
        __boot_scene_bin_end = .;
        boot_scene_bin_end = .;
    } > ROM

    __stack_top = 0x8D000000;


    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
