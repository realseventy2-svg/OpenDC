#include "cdi.h"
#include "gdrom.h"
#include "scramble.h"

int cdi_is_valid_sh4_entry(const uint8_t *code, size_t len) {
    if(!code || len < 4) return 0;

    const uint16_t *op = (const uint16_t *)code;
    uint16_t op0 = op[0];
    uint16_t op1 = op[1];

    /* Reject known invalid/empty header markers */
    if(op0 == 0x0000 || op0 == 0xFFFF) return 0;

    /* 1. NOP sled (0x0009): standard for KallistiOS SDK and Katana tools */
    if(op0 == 0x0009 || op1 == 0x0009) {
        return 1;
    }

    /* 2. PC-relative load: mov.l @(disp, PC), Rn (0xDxxx) */
    if((op0 & 0xF000) == 0xD000) {
        return 1;
    }

    /* 3. Branch: bra label (0xAxxx) or bsr label (0xBxxx) with valid delay slot */
    if((op0 & 0xE000) == 0xA000) {
        if(op1 != 0x0000 && op1 != 0xFFFF) {
            return 1;
        }
    }

    /* 4. Control register or stack operations: ldc/lds/sts/jsr/jmp (0x4xxx) */
    if((op0 & 0xF000) == 0x4000) {
        return 1;
    }

    /* 5. Immediate load: mov #imm, Rn (0xExxx) */
    if((op0 & 0xF000) == 0xE000) {
        return 1;
    }

    /* 6. PC-relative word load: mov.w @(disp, PC), Rn (0x9xxx) */
    if((op0 & 0xF000) == 0x9000) {
        return 1;
    }

    /* 7. Memory / Register operations: mov.b/w/l (0x6xxx, 0x2xxx) */
    if((op0 & 0xF000) == 0x6000 || (op0 & 0xF000) == 0x2000) {
        return 1;
    }

    /* 8. Arithmetic & Logic: add, tst, and, xor, or (0x7xxx, 0xCxxx) */
    if((op0 & 0xF000) == 0x7000 || (op0 & 0xF000) == 0xC000) {
        return 1;
    }

    /* 9. Conditional branches: bt, bf, bt/s, bf/s (0x8xxx) */
    if((op0 & 0xF000) == 0x8000) {
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
