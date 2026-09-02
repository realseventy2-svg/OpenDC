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

    /* 4. Flush & enable SH-4 caches (CCR = 0x092B) */
    *(volatile uint32_t *)0xFF00001CUL = 0x0000092BUL;
    __asm__ volatile("nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
                     "nop\n\t" "nop\n\t" "nop\n\t" "nop" ::: "memory");

    /* 5. Set up clean Katana environment registers and jump to 0x8C010000 */
    __asm__ volatile(
        "mov.l  1f, r15\n\t"
        "mov.l  2f, r0\n\t"
        "lds    r0, pr\n\t"
        "mov.l  3f, r0\n\t"
        "ldc    r0, sr\n\t"
        "mov.l  4f, r0\n\t"
        "lds    r0, fpscr\n\t"
        "mov.l  5f, r0\n\t"
        "ldc    r0, vbr\n\t"
        "sub    r0, r0\n\t"
        "mov    r0, r1\n\t"
        "mov    r0, r2\n\t"
        "mov    r0, r3\n\t"
        "mov    r0, r4\n\t"
        "mov    r0, r5\n\t"
        "mov    r0, r6\n\t"
        "mov    r0, r7\n\t"
        "mov    r0, r8\n\t"
        "mov    r0, r9\n\t"
        "mov    r0, r10\n\t"
        "mov    r0, r11\n\t"
        "mov    r0, r12\n\t"
        "mov    r0, r13\n\t"
        "mov    r0, r14\n\t"
        "mov.l  6f, r0\n\t"
        "jmp    @r0\n\t"
        "sub    r0, r0\n\t"
        ".align 4\n\t"
        "1: .long 0x8D000000\n\t"
        "2: .long 0xAC00B700\n\t"
        "3: .long 0x40000000\n\t"
        "4: .long 0x00040001\n\t"
        "5: .long 0x8C00F400\n\t"
        "6: .long 0x8C010000\n\t"
        :
        :
        : "memory"
    );

    while(1) { __asm__ volatile("nop"); }
    return GDROM_OK;
}
