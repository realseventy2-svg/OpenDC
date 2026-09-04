#include "homebrew_cdi.h"
#include "gdrom.h"
#include "scramble.h"

static int str_contains(const char *src, size_t max_len, const char *sub) {
    if(!src || !sub) return 0;
    size_t sub_len = 0;
    while(sub[sub_len]) sub_len++;

    for(size_t i = 0; i + sub_len <= max_len; i++) {
        int match = 1;
        for(size_t j = 0; j < sub_len; j++) {
            char c1 = src[i + j];
            char c2 = sub[j];
            if(c1 >= 'a' && c1 <= 'z') c1 -= ('a' - 'A');
            if(c2 >= 'a' && c2 <= 'z') c2 -= ('a' - 'A');
            if(c1 != c2) { match = 0; break; }
        }
        if(match) return 1;
    }
    return 0;
}

int homebrew_cdi_detect(const uint8_t *ip_sector) {
    if(!ip_sector) return 0;

    /* Check for KallistiOS maker/title signatures in IP.BIN header */
    if(str_contains((const char *)(ip_sector + 0x10), 16, "KallistiOS") ||
       str_contains((const char *)(ip_sector + 0x70), 16, "KallistiOS") ||
       str_contains((const char *)(ip_sector + 0x80), 32, "KallistiOS") ||
       str_contains((const char *)(ip_sector + 0x80), 32, "Dreamcast App")) {
        return 1;
    }

    /* Check if IP.BIN bootstrap code at 0x8C008300 is empty/zeroed (standard makeip output) */
    const uint32_t *ip_entry = (const uint32_t *)0x8C008300UL;
    if(*ip_entry == 0 || *ip_entry == 0xFFFFFFFFUL) {
        return 1;
    }

    return 0;
}

int homebrew_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest) {
    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint8_t *staging = (uint8_t *)0x8C700000UL;

    /* Ingest first sector into staging buffer to inspect entrypoint */
    if(gdrom_read_fad(staging, file_fad, 1) != GDROM_OK) {
        return GDROM_DEVICE_ERR;
    }

    const uint16_t *op = (const uint16_t *)staging;
    int is_unscrambled = 0;

    /* KallistiOS crt0 entrypoint signature (mov.l @(disp,PC), r0; stc sr, r1; mov.l r1, @r0) */
    if((op[0] & 0xFF00) == 0xD000 && op[1] == 0x0102 && op[2] == 0x2012) {
        is_unscrambled = 1;
    } else if(op[0] == 0x0009 || op[1] == 0x0009) {
        /* NOP sled entrypoint */
        is_unscrambled = 1;
    }

    if(is_unscrambled) {
        /* Raw / Unscrambled KOS binary: stream directly into destination */
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
        /* Scrambled KOS binary (generated via make-cdi.sh scramble step):
           Stream all sectors into staging buffer and descramble into 0x8C010000 */
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

        gdrom_descramble(staging, dest, file_size);
    }

    return GDROM_OK;
}
