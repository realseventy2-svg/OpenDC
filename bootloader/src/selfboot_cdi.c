#include "selfboot_cdi.h"
#include "gdrom.h"
#include "scramble.h"

int selfboot_cdi_detect(const uint8_t *ip_sector) {
    if(!ip_sector) return 0;

    /* Check for official Katana hardware ID */
    if(ip_sector[0] == 'S' && ip_sector[1] == 'E' &&
       ip_sector[2] == 'G' && ip_sector[3] == 'A') {
        return 1;
    }

    /* Check for commercial bootstrap code at ip_sector + 0x0300 */
    const uint32_t *ip_entry = (const uint32_t *)(ip_sector + 0x0300);
    if(*ip_entry != 0 && *ip_entry != 0xFFFFFFFFUL) {
        return 1;
    }

    return 0;
}

static int is_raw_sh4_binary(const uint8_t *data) {
    const uint16_t *insn = (const uint16_t *)data;

    /* Typical SH-4 entry patterns:
       - 0xDxxx: mov.l @(disp, PC), Rn
       - 0x4xxx: ldc / lds / sts
       - 0xAFxx: bra <disp>
       - 0x2Fxx: mov.l Rm, @-Rn
       - 0x0009: nop */
    uint16_t first = insn[0];
    if((first & 0xF000) == 0xD000 || 
       (first & 0xFF00) == 0xAF00 || 
       (first & 0xF000) == 0x4000 ||
       (first & 0xF000) == 0x2000 ||
        first == 0x0009) {
        return 1;
    }
    return 0;
}

int selfboot_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest) {
    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint32_t padded_size = total_sectors * 2048U;

    /* Use uncached P2 mirror for staging to prevent dirty cache retention */
    uint8_t *staging = (uint8_t *)0xAC700000UL;
    uint8_t *uncached_dest = (uint8_t *)((uint32_t)dest | 0x20000000UL);

    /* Stream all sectors into staging buffer at 0xAC700000 */
    uint32_t read_count = 0;
    while(read_count < total_sectors) {
        uint32_t batch = total_sectors - read_count;
        if(batch > 16U) batch = 16U;

        if(gdrom_read_fad(staging + (read_count * 2048U),
                          file_fad + read_count,
                          (uint16_t)batch) != GDROM_OK) {
            return GDROM_DEVICE_ERR;
        }
        read_count += batch;

        /* Visual Progress bar */
        uint32_t progress_w = (read_count >> 3);
        if(progress_w > 400U) progress_w = 400U;
        for(int y = 468; y < 474; y++) {
            for(uint32_t x = 0; x < progress_w; x++) {
                fb[y * 640 + (120 + x)] = 0x07E0; /* GREEN */
            }
        }
    }

    /* Check if the binary is already clean SH-4 machine code */
    if(is_raw_sh4_binary(staging)) {
        const uint32_t *src32 = (const uint32_t *)staging;
        uint32_t *dst32 = (uint32_t *)uncached_dest;
        for(uint32_t i = 0; i < (padded_size >> 2); i++) {
            dst32[i] = src32[i];
        }
    } else {
        /* Descramble full sector-padded length directly to uncached destination */
        gdrom_descramble(staging, uncached_dest, padded_size);
    }

    return GDROM_OK;
}