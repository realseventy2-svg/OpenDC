#include "iso9660.h"
#include "gdrom.h"
#include "scramble.h"

uint32_t read_le32_unaligned(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t lba_to_fad(uint32_t lba, uint32_t data_fad) {
    if(data_fad >= 45000U) {
        /* High-Density Track 3 (GDI): Extents >= 44000 are absolute disc LBAs.
           Since Track 3 physical data starts at FAD 45150 (45000 + 150),
           every disc LBA requires adding the 150 sector lead-in. */
        if(lba >= 44000U) {
            return lba + 150U;
        }
        /* Relative sector within Track 3 */
        return data_fad + lba;
    }

    if(lba >= 10000U && lba < 44000U) {
        /* Multi-session CDI: ISO LBAs were generated with -C 0,11702 */
        if(data_fad >= 11800U) {
            return data_fad + (lba - 11702U);
        } else {
            return lba + 150U;
        }
    }
    return data_fad + lba;
}

static int filename_match(const char *name, size_t name_len, const char *target) {
    size_t target_len = 0;
    while(target[target_len]) target_len++;

    size_t clean_len = name_len;
    for(size_t i = 0; i < name_len; i++) {
        if(name[i] == ';') {
            clean_len = i;
            break;
        }
    }
    if(clean_len != target_len) return 0;
    for(size_t i = 0; i < clean_len; i++) {
        char c1 = name[i];
        char c2 = target[i];
        if(c1 >= 'a' && c1 <= 'z') c1 -= ('a' - 'A');
        if(c2 >= 'a' && c2 <= 'z') c2 -= ('a' - 'A');
        if(c1 != c2) return 0;
    }
    return 1;
}

int iso_load_1st_read(uint32_t data_fad) {
    uint8_t *sector = (uint8_t *)0x8C004000UL;
    uint32_t file_fad  = 0;
    uint32_t file_size = 0;
    uint32_t pvd_fad   = 0;
    char boot_file[17] = { 0 };
    char wince_os = '0';

    /* 1. Read IP.BIN (sector 0 at data_fad) to determine target boot executable and OS type */
    if(gdrom_read_fad(sector, data_fad, 1) == GDROM_OK) {
        wince_os = (char)sector[0x3E];
        int len = 0;
        for(int i = 0; i < 16; i++) {
            char c = (char)sector[0x60 + i];
            if(c > ' ') {
                boot_file[len++] = c;
            } else if(len > 0) {
                break;
            }
        }
        boot_file[len] = '\0';
    }
    if(boot_file[0] == '\0') {
        const char def[] = "1ST_READ.BIN";
        for(int i = 0; def[i]; i++) boot_file[i] = def[i];
        boot_file[12] = '\0';
    }

    /* 2. Check candidate FADs for ISO9660 Primary Volume Descriptor */
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

    /* 3. Traverse root directory records sequentially */
    uint32_t root_sectors = (root_size + 2047U) / 2048U;
    if(root_sectors > 32U) root_sectors = 32U;

    for(uint32_t s = 0; s < root_sectors; s++) {
        if(gdrom_read_fad(sector, root_fad + s, 1) != GDROM_OK) break;

        uint32_t pos = 0;
        while(pos < 2048U) {
            uint8_t rec_len = sector[pos];
            if(rec_len < 33U || pos + rec_len > 2048U) {
                break;
            }
            uint8_t name_len = sector[pos + 32];
            const char *name = (const char *)(sector + pos + 33);
            uint8_t flags = sector[pos + 25];

            if(!(flags & 0x02) && name_len > 0) {
                if(filename_match(name, name_len, boot_file) ||
                   filename_match(name, name_len, "1ST_READ.BIN") ||
                   filename_match(name, name_len, "0WINCEOS.BIN")) {
                    uint32_t lba = read_le32_unaligned(sector + pos + 2);
                    uint32_t sz  = read_le32_unaligned(sector + pos + 10);
                    if(sz > 0 && sz <= 0x00D00000UL) {
                        file_fad  = lba_to_fad(lba, data_fad);
                        file_size = sz;
                        if(filename_match(name, name_len, "0WINCEOS.BIN")) {
                            wince_os = '1';
                        }
                        break;
                    }
                }
            }
            pos += rec_len;
        }
        if(file_fad != 0) break;
    }

    if(file_fad == 0 || file_size == 0)
        return GDROM_DEVICE_ERR;
    if(file_size > 0x00D00000UL)
        return GDROM_BAD_ARG;

    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;
    uint8_t *dest = (uint8_t *)0x8C010000UL;
    int is_wince = (wince_os == '1' ||
                    filename_match(boot_file, 12, "0WINCEOS.BIN") ||
                    filename_match(boot_file, 8, "0WINCEOS"));
    int is_gdi   = (data_fad >= 45000U);

    if(is_wince && is_gdi) {
        /* Windows CE retail loading:
           1. First 2048 bytes placed at 0x8CE01000 (WinCE boot header).
           2. Remaining binary placed at 0x8C010000. */
        if(gdrom_read_fad((void *)0x8CE01000UL, file_fad, 1) != GDROM_OK)
            return GDROM_DEVICE_ERR;

        /* Also mirror to uncached 0xACE01000 */
        const uint32_t *hdr_c = (const uint32_t *)0x8CE01000UL;
        uint32_t *hdr_u = (uint32_t *)0xACE01000UL;
        for(size_t i = 0; i < 512; i++) {
            hdr_u[i] = hdr_c[i];
        }

        uint32_t rem_sectors = (file_size > 2048U) ? ((file_size - 2048U + 2047U) / 2048U) : 0;
        uint32_t read_count = 0;
        while(read_count < rem_sectors) {
            uint32_t batch = rem_sectors - read_count;
            if(batch > 16U) batch = 16U;
            if(gdrom_read_fad(dest + (read_count * 2048U),
                              file_fad + 1U + read_count,
                              (uint16_t)batch) != GDROM_OK) {
                return GDROM_DEVICE_ERR;
            }
            read_count += batch;

            uint32_t progress_w = (read_count >> 3);
            if(progress_w > 400U) progress_w = 400U;
            for(int y = 468; y < 474; y++) {
                for(uint32_t x = 0; x < progress_w; x++) {
                    fb[y * 640 + (120 + x)] = 0x07E0;
                }
            }
        }
    } else {
        /* Katana SDK / KallistiOS homebrew loading */
        if(gdrom_read_fad(dest, file_fad, 1) != GDROM_OK)
            return GDROM_DEVICE_ERR;

        int is_unscrambled = ((dest[0] == 0x09 && dest[1] == 0x00) ||
                              (dest[2] == 0x09 && dest[3] == 0x00) ||
                              is_gdi);

        if(is_unscrambled) {
            uint32_t total_sectors = (file_size + 2047U) / 2048U;
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
                        fb[y * 640 + (120 + x)] = 0x07E0;
                    }
                }
            }
        } else {
            /* Scrambled CD-R binary */
            uint32_t total_sectors = (file_size + 2047U) / 2048U;
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

                uint32_t progress_w = (read_count >> 3);
                if(progress_w > 400U) progress_w = 400U;
                for(int y = 468; y < 474; y++) {
                    for(uint32_t x = 0; x < progress_w; x++) {
                        fb[y * 640 + (120 + x)] = 0x07E0;
                    }
                }
            }
            gdrom_descramble(staging, dest, file_size);
        }
    }

    /* Turn progress bar CYAN upon completion */
    for(int y = 468; y < 474; y++) {
        for(int x = 120; x < 520; x++) {
            fb[y * 640 + x] = 0x07FF; /* CYAN */
        }
    }

    return GDROM_OK;
}
