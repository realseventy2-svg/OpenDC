#include "iso9660.h"
#include "gdrom.h"
#include "scramble.h"

uint32_t read_le32_unaligned(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t lba_to_fad(uint32_t lba, uint32_t data_fad) {
    if(lba >= 10000U && lba < 45000U) {
        /* Multi-session CDI: ISO LBAs were generated with -C 0,11702 */
        if(data_fad >= 11800U) {
            return data_fad + (lba - 11702U);
        } else {
            return lba + 150U;
        }
    }
    if(lba >= 45000U) {
        return lba;
    }
    return data_fad + lba;
}

int iso_load_1st_read(uint32_t data_fad) {
    static uint8_t sector[2048] __attribute__((aligned(4)));
    uint32_t file_fad  = 0;
    uint32_t file_size = 0;
    uint32_t pvd_fad   = 0;

    /* Check candidate FADs for ISO9660 Primary Volume Descriptor */
    uint32_t candidates[6] = { data_fad + 16U, data_fad, 11868, 11718, 45016, 166 };
    for(int c = 0; c < 6; c++) {
        if(candidates[c] == 0) continue;
        if(gdrom_read_fad(sector, candidates[c], 1) == GDROM_OK) {
            if(sector[1] == 'C' && sector[2] == 'D' &&
               sector[3] == '0' && sector[4] == '0' && sector[5] == '1') {
                pvd_fad = candidates[c];
                break;
            }
        }
    }

    if(pvd_fad == 0)
        return GDROM_DEVICE_ERR;

    uint8_t root_record_len = sector[156];
    if(root_record_len < 34)
        return GDROM_DEVICE_ERR;

    uint32_t root_lba  = read_le32_unaligned(sector + 156 + 2);
    uint32_t root_size = read_le32_unaligned(sector + 156 + 10);
    uint32_t root_fad  = lba_to_fad(root_lba, data_fad);

    /* Traverse the root directory to locate 1ST_READ.BIN */
    uint32_t root_sectors = (root_size + 2047U) / 2048U;
    if(root_sectors > 32U) root_sectors = 32U;

    for(uint32_t s = 0; s < root_sectors; s++) {
        if(gdrom_read_fad(sector, root_fad + s, 1) != GDROM_OK) break;

        for(uint32_t i = 0; i <= 2048 - 12; i++) {
            if(sector[i] == '1' && sector[i+1] == 'S' && sector[i+2] == 'T' &&
               sector[i+3] == '_' && sector[i+4] == 'R' && sector[i+5] == 'E' &&
               sector[i+6] == 'A' && sector[i+7] == 'D' && sector[i+8] == '.' &&
               sector[i+9] == 'B' && sector[i+10] == 'I' && sector[i+11] == 'N') {
                for(int back = 30; back <= 36; back++) {
                    if((int)i >= back) {
                        uint32_t rec_off = i - (uint32_t)back;
                        uint8_t rec_len = sector[rec_off];
                        if(rec_len >= 33) {
                            uint32_t lba = read_le32_unaligned(sector + rec_off + 2);
                            uint32_t sz  = read_le32_unaligned(sector + rec_off + 10);
                            if(sz > 0 && sz <= 0x00D00000UL) {
                                file_fad  = lba_to_fad(lba, data_fad);
                                file_size = sz;
                                break;
                            }
                        }
                    }
                }
            }
            if(file_fad != 0) break;
        }
        if(file_fad != 0) break;
    }

    if(file_fad == 0 || file_size == 0)
        return GDROM_DEVICE_ERR;
    if(file_size > 0x00D00000UL)
        return GDROM_BAD_ARG;

    uint8_t *dest = (uint8_t *)0x8C010000UL;
    uint32_t total_sectors = (file_size + 2047U) / 2048U;

    /* 1. Read first sector to check if scrambled or already plain machine code */
    if(gdrom_read_fad(dest, file_fad, 1) != GDROM_OK)
        return GDROM_DEVICE_ERR;

    int is_unscrambled = ((dest[0] == 0x09 && dest[1] == 0x00) ||
                          (dest[2] == 0x09 && dest[3] == 0x00) ||
                          (data_fad >= 45000U));

    if(is_unscrambled) {
        /* Read remaining sectors directly into dest without staging */
        uint32_t read_count = 1;
        while(read_count < total_sectors) {
            uint32_t batch = total_sectors - read_count;
            if(batch > 16U) batch = 16U;
            if(gdrom_read_fad(dest + (read_count * 2048U),
                              file_fad + read_count,
                              (uint16_t)batch) != GDROM_OK)
                return GDROM_DEVICE_ERR;
            read_count += batch;
        }
    } else {
        /* Scrambled CD-R binary: read to staging buffer and descramble into dest */
        uint8_t *staging = (uint8_t *)0x8C700000UL;
        for(int b = 0; b < 2048; b++) staging[b] = dest[b];

        uint32_t read_count = 1;
        while(read_count < total_sectors) {
            uint32_t batch = total_sectors - read_count;
            if(batch > 16U) batch = 16U;
            if(gdrom_read_fad(staging + (read_count * 2048U),
                              file_fad + read_count,
                              (uint16_t)batch) != GDROM_OK)
                return GDROM_DEVICE_ERR;
            read_count += batch;
        }
        gdrom_descramble(staging, dest, file_size);
    }

    return GDROM_OK;
}
