#include "boot.h"
#include "gdrom.h"
#include "iso9660.h"
#include "syscalls.h"

int gdrom_boot_game(uint32_t data_fad) {
    if(data_fad == 0)
        data_fad = gdrom_get_cached_data_fad();
    if(data_fad == 0)
        return GDROM_NOT_READY;

    /* 1. Pre-load IP.BIN (16 sectors) into 0x8C008000UL */
    uint8_t *ip_dest = (uint8_t *)0x8C008000UL;
    gdrom_read_fad(ip_dest, data_fad, 16);

    /* 2. Load and descramble 1ST_READ.BIN into 0x8C010000 */
    int load_res = iso_load_1st_read(data_fad);
    if(load_res != GDROM_OK) {
        return load_res;
    }

    /* 3. Populate BIOS syscall vectors and sysinfo table */
    gdrom_install_syscall();

    /* 4. Set up Katana required hardware states */
    /* DMAC DMAOR enable (0x8201) */
    *(volatile uint16_t *)0xFFA00040UL = 0x8201;

    /* AICA interrupt levels */
    *(volatile uint16_t *)0xA0702800UL = 0x0048; /* SCIEB */
    *(volatile uint8_t  *)0xA0702804UL = 0x18;   /* SCILV0 */
    *(volatile uint8_t  *)0xA0702808UL = 0x50;   /* SCILV1 */
    *(volatile uint8_t  *)0xA070280CUL = 0x08;   /* SCILV2 */

    /* ARM sound CPU loop */
    *(volatile uint32_t *)0xA0800000UL = 0xEAFFFFFE;

    /* GD-ROM DMA start register */
    *(volatile uint32_t *)0xA05F7404UL = 0x0C010000;

    /* 5. Flush & enable SH-4 caches (CCR = 0x092B) */
    *(volatile uint32_t *)0xFF00001CUL = 0x0000092BUL;
    __asm__ volatile("nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                     "nop\n\t" "nop\n\t" "nop\n\t" "nop" ::: "memory");

    /* 6. Determine boot entrypoint:
          Katana games boot through IP.BIN bootstrap at 0x8C008300 (or 0xAC008300).
          If IP.BIN has code at 0x8C008300, jump to 0x8C008300; otherwise fallback to 0x8C010000. */
    uint32_t boot_entry = 0x8C010000UL;
    uint32_t *ip_entry = (uint32_t *)0x8C008300UL;
    if(*ip_entry != 0 && *ip_entry != 0xFFFFFFFFUL) {
        boot_entry = 0x8C008300UL;
    }

    /* 7. Set up clean Katana environment registers and jump to entrypoint */
    register uint32_t target_pc __asm__("r0") = boot_entry;
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
        "sub    r1, r1\n\t"
        "mov    r1, r2\n\t"
        "mov    r1, r3\n\t"
        "mov    r1, r4\n\t"
        "mov    r1, r5\n\t"
        "mov    r1, r6\n\t"
        "mov    r1, r7\n\t"
        "mov    r1, r8\n\t"
        "mov    r1, r9\n\t"
        "mov    r1, r10\n\t"
        "mov    r1, r11\n\t"
        "mov    r1, r12\n\t"
        "mov    r1, r13\n\t"
        "mov    r1, r14\n\t"
        "jmp    @%0\n\t"
        "sub    r1, r1\n\t"
        ".align 4\n\t"
        "1: .long 0x8D000000\n\t"
        "2: .long 0xAC00043C\n\t"
        "3: .long 0x400000F0\n\t"
        "4: .long 0x00040001\n\t"
        "5: .long 0x8C000000\n\t"
        "6: .long 0x8C000000\n\t"
        :
        : "r"(target_pc)
        : "r1", "memory"
    );

    while(1) { __asm__ volatile("nop"); }
    return GDROM_OK;
}
