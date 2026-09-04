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

int selfboot_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest) {
    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint8_t *staging = (uint8_t *)0x8C700000UL;

    /* Ingest first sector into staging buffer to inspect entrypoint */
    if(gdrom_read_fad(staging, file_fad, 1) != GDROM_OK) {
        return GDROM_DEVICE_ERR;
    }

    const uint16_t *op = (const uint16_t *)staging;
    int is_unscrambled = 0;

    /* Entrypoint detection:
       - KallistiOS crt0 (mov.l @(disp,PC), r0; stc sr, r1; mov.l r1, @r0)
       - Katana SDK crt0 NOP sled (0x0009 at op[0] or op[1]) */
    if((op[0] & 0xFF00) == 0xD000 && op[1] == 0x0102 && op[2] == 0x2012) {
        is_unscrambled = 1;
    } else if(op[0] == 0x0009 || op[1] == 0x0009) {
        is_unscrambled = 1;
    }

    if(is_unscrambled) {
        /* Raw / Unscrambled commercial CDI rip (e.g. sonic-selfboot.cdi):
           Copy sector 0 into 0x8C010000 and stream remaining sectors directly. */
        for(int b = 0; b < 2048; b++) {
            dest[b] = staging[b];
        }

        uint32_t read_count = 1;
        while(read_count < total_sectors) {
            uint32_t batch = total_sectors - read_count;
            if(batch > 16U) batch = 16U;
            if(gdrom_read_fad(dest + (read_count * 2048U),
                              file_fad + read_count,
                              (uint16_t)batch) != GDROM_OK) {
                return GDROM_DEVICE_ERR;
            }
            read_count += batch;

            uint32_t progress_w = (read_count >> 3);
            if(progress_w > 400U) progress_w = 400U;
            for(int y = 468; y < 474; y++) {
                for(uint32_t x = 0; x < progress_w; x++) {
                    fb[y * 640 + (120 + x)] = 0x07E0; /* GREEN */
                }
            }
        }
    } else {
        /* Scrambled Commercial Self-Boot CDI (Retail MIL-CD Rip):
           Stream all sectors into staging buffer and run gdrom_descramble(). */
        uint32_t read_count = 1;
        while(read_count < total_sectors) {
            uint32_t batch = total_sectors - read_count;
            if(batch > 16U) batch = 16U;
            if(gdrom_read_fad(staging + (read_count * 2048U),
                              file_fad + read_count,
                              (uint16_t)batch) != GDROM_OK) {
                return GDROM_DEVICE_ERR;
            }
            read_count += batch;

            uint32_t progress_w = (read_count >> 3);
            if(progress_w > 400U) progress_w = 400U;
            for(int y = 468; y < 474; y++) {
                for(uint32_t x = 0; x < progress_w; x++) {
                    fb[y * 640 + (120 + x)] = 0x07E0; /* GREEN */
                }
            }
        }

        /* Descramble staging buffer into 0x8C010000 */
        gdrom_descramble(staging, dest, file_size);
    }

    return GDROM_OK;
}
