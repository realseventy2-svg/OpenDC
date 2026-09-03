#include "boot.h"
#include "gdrom.h"
#include "iso9660.h"
#include "syscalls.h"

int gdrom_boot_game(uint32_t data_fad) {
    if(data_fad == 0)
        data_fad = gdrom_get_cached_data_fad();
    if(data_fad == 0)
        return GDROM_NOT_READY;

    /* 1. Load and descramble 1ST_READ.BIN into 0x8C010000 */
    int load_res = iso_load_1st_read(data_fad);
    if(load_res != GDROM_OK) {
        return load_res;
    }

    /* 2. Load IP.BIN (16 sectors) cleanly into 0x8C008000UL */
    uint8_t *ip_dest = (uint8_t *)0x8C008000UL;
    gdrom_read_fad(ip_dest, data_fad, 16);

    /* 3. Populate BIOS syscall vectors and sysinfo table */
    gdrom_install_syscall();

    /* 4. Set up Katana required hardware states */
    /* DMAC DMAOR enable (0x8201) */
    *(volatile uint16_t *)0xFFA00040UL = 0x8201;

    /* Bus Control registers (PCTRA & PDTRA for region / PAL-NTSC sensing) */
    *(volatile uint32_t *)0xFF80002CUL = 0x000A03F0; /* BSC_PCTRA */
    *(volatile uint32_t *)0xFF800030UL = 0x00000004; /* BSC_PDTRA */

    /* AICA interrupt levels */
    *(volatile uint16_t *)0xA0702800UL = 0x0048; /* SCIEB */
    *(volatile uint8_t  *)0xA0702804UL = 0x18;   /* SCILV0 */
    *(volatile uint8_t  *)0xA0702808UL = 0x50;   /* SCILV1 */
    *(volatile uint8_t  *)0xA070280CUL = 0x08;   /* SCILV2 */

    /* ARM sound CPU loop */
    /* GD-ROM and G1 Bus hardware unlock / access registers */
    *(volatile uint32_t *)0xA05F7490UL = 0x00000222; /* G1_ATA_PIO_RACCESS */
    *(volatile uint32_t *)0xA05F7494UL = 0x00000222; /* G1_ATA_PIO_WACCESS */
    *(volatile uint32_t *)0xA05F74A0UL = 0x00000222; /* G1_ATA_DMA_RACCESS */
    *(volatile uint32_t *)0xA05F74A4UL = 0x00000222; /* G1_ATA_DMA_WACCESS */
    *(volatile uint32_t *)0xA05F74E4UL = 0x00000018; /* SB_G1RRC */
    *(volatile uint32_t *)0xA05F74B4UL = 0x00000000; /* SB_GDST */
    *(volatile uint32_t *)0xA05F74F4UL = 0x00000001; /* SB_GDEN (Unlock GD-ROM DMA/PIO) */
    *(volatile uint32_t *)0xA05F7404UL = 0x0C010000; /* SB_GDSTARD */
    *(volatile uint32_t *)0xA05F688CUL = 0x00000000; /* SB_SDSTAW */
    *(volatile uint32_t *)0xA05F8040UL = 0x00000000; /* VO_BORDER_COL */

    /* 5. Copy BIOS runtime helper routines (4096 bytes) from Flash ROM 0xA0004000 to 0x8CE00000 */
    const uint32_t *src_rom = (const uint32_t *)0xA0004000UL;
    uint32_t *dst_ram = (uint32_t *)0x8CE00000UL;
    uint32_t *dst_uncached = (uint32_t *)0xACE00000UL;
    for(size_t i = 0; i < 0x400; i++) {
        dst_ram[i] = src_rom[i];
        dst_uncached[i] = src_rom[i];
    }

    /* 6. Apply retail BIOS security DRM patches to IP.BIN (bypasses security fail reset) */
    *(volatile uint16_t *)(0x8C008300UL + 0x0DD8) = 0x5113;
    *(volatile uint16_t *)(0x8C008300UL + 0x14BC) = 0x0009; /* nop */
    *(volatile uint16_t *)(0x8C008300UL + 0x1578) = 0xE030; /* mov #48, r0 */
    *(volatile uint16_t *)(0xAC008300UL + 0x0DD8) = 0x5113;
    *(volatile uint16_t *)(0xAC008300UL + 0x14BC) = 0x0009;
    *(volatile uint16_t *)(0xAC008300UL + 0x1578) = 0xE030;

    /* 7. Flush & enable SH-4 caches (CCR = 0x092B enables OCRAM at 0x7E001000 for IP.BIN) */
    *(volatile uint32_t *)0xFF00001CUL = 0x0000092BUL;
    __asm__ volatile("nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                     "nop\n\t" "nop\n\t" "nop\n\t" "nop" ::: "memory");

    /* 8. Determine boot entrypoint:
          Commercial discs contain Sega license display code at 0x8C008300.
          Homebrew discs (CDI) have 0s at 0x8C008300 and boot directly from 0x8C010000. */
    uint32_t boot_entry = 0x8C010000UL;
    uint32_t *ip_entry = (uint32_t *)0x8C008300UL;
    if(*ip_entry != 0 && *ip_entry != 0xFFFFFFFFUL) {
        boot_entry = 0xAC008300UL;
    }

    /* 7. Set up exact Dreamcast retail BIOS environment registers and jump to entrypoint */
    __asm__ volatile(
        "mov.l  1f, r15\n\t"
        "mov.l  2f, r1\n\t"
        "lds    r1, pr\n\t"
        "mov.l  3f, r1\n\t"
        "ldc    r1, sr\n\t"
        "mov.l  4f, r1\n\t"
        "lds    r1, fpscr\n\t"
        "mov.l  5f, r1\n\t"
        "ldc    r1, vbr\n\t"
        "mov.l  6f, r1\n\t"
        "ldc    r1, gbr\n\t"
        "mov.l  7f, r1\n\t"
        "ldc    r1, dbr\n\t"
        "mov.l  9f, r2\n\t"
        "sub    r3, r3\n\t"
        "mov.l  10f, r4\n\t"
        "mov.l  11f, r5\n\t"
        "mov.l  12f, r6\n\t"
        "mov    #112, r7\n\t"   /* 0x70 */
        "sub    r8, r8\n\t"
        "sub    r9, r9\n\t"
        "sub    r10, r10\n\t"
        "sub    r11, r11\n\t"
        "sub    r12, r12\n\t"
        "sub    r13, r13\n\t"
        "sub    r14, r14\n\t"
        "mov    #9, r1\n\t"
        "jmp    @%0\n\t"
        "mov.l  8f, r0\n\t"     /* Executed in delay slot: r0 = 0xAC0005D8 */
        ".align 4\n\t"
        "1:  .long 0x8D000000\n\t"
        "2:  .long 0xAC00043C\n\t"
        "3:  .long 0x400000F0\n\t"
        "4:  .long 0x00040001\n\t"
        "5:  .long 0x8C000000\n\t"
        "6:  .long 0x8C000000\n\t"
        "7:  .long 0x8C000010\n\t"
        "8:  .long 0xAC0005D8\n\t"
        "9:  .long 0xAC00940C\n\t"
        "10: .long 0xAC008300\n\t"
        "11: .long 0xF4000000\n\t"
        "12: .long 0xF4002000\n\t"
        :
        : "r"(boot_entry)
        : "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "memory"
    );

    while(1) { __asm__ volatile("nop"); }
    return GDROM_OK;
}
