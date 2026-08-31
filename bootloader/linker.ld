OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh4)
ENTRY(_start)

MEMORY
{
    ROM (rx)  : ORIGIN = 0xA0000000, LENGTH = 0x00200000   /* 2 MB Flash ROM */
    RAM (rwx) : ORIGIN = 0x8C010000, LENGTH = 0x00FE0000   /* ~16 MB SDRAM */
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

    /* Stage 3: Initialized data (Stored in ROM, Relocated to RAM) */
    __data_lma = .;
    .data : AT (__data_lma)
    {
        . = ALIGN(4);
        __data_vma = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM

    __data_size = __data_end - __data_vma;

    /* Zero-initialized RAM memory */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    __stack_top = 0x8D000000;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
