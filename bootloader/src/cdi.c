#include "cdi.h"
#include "gdrom.h"
#include "scramble.h"

int cdi_is_valid_sh4_entry(const uint8_t *code, size_t len) {
    if(!code || len < 8) return 0;

    const uint16_t *op = (const uint16_t *)code;

    /* 1. NOP sled (Katana SDK crt0 & generic Dreamcast tools):
          0x0009 at op[0] or op[1] */
    if(op[0] == 0x0009 || op[1] == 0x0009) {
        return 1;
    }

    /* 2. KallistiOS crt0 entrypoint signature:
          mov.l @(disp, PC), r0  (0xD0xx)
          stc   sr, r1           (0x0102)
          mov.l r1, @r0          (0x2012) */
    if((op[0] & 0xFF00) == 0xD000 && op[1] == 0x0102 && op[2] == 0x2012) {
        return 1;
    }

    /* 3. Standard SH-4 startup sequence (mov.l literal load + ldc/sts to control register):
          op[0] is mov.l @(disp, PC), Rn (0xDxxx) and op[1] is ldc/lds/sts (0x4xxx) */
    if((op[0] & 0xF000) == 0xD000 && (op[1] & 0xF000) == 0x4000) {
        return 1;
    }

    /* 4. Branch to entrypoint:
          bra label (0xAxxx) followed by NOP delay slot (0x0009) */
    if((op[0] & 0xF000) == 0xA000 && op[1] == 0x0009) {
        return 1;
    }

    return 0;
}

int cdi_load_binary(uint32_t file_fad, uint32_t file_size, uint8_t *dest) {
    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint8_t *staging = (uint8_t *)0x8C700000UL;

    /* Ingest first sector into staging buffer to inspect entrypoint */
    if(gdrom_read_fad(staging, file_fad, 1) != GDROM_OK) {
        return GDROM_DEVICE_ERR;
    }

    int is_unscrambled = cdi_is_valid_sh4_entry(staging, 2048);

    if(is_unscrambled) {
        /* Homebrew / Modern CDI: Binary is raw SH-4 machine code.
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
        /* Commercial Self-Boot CDI (Retail Rip): Binary is PRNG scrambled.
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
