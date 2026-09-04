#include "iso9660.h"
#include "gdrom.h"
#include "scramble.h"
#include "gdi_loader.h"
#include "homebrew_cdi.h"
#include "selfboot_cdi.h"
#include "screen.h"

uint32_t read_le32_unaligned(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t lba_to_fad(uint32_t lba, uint32_t data_fad) {
    if(lba >= 10000U) {
        return lba + 150U;
    }
    /* Relative sector within Track */
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

static uint32_t cached_ip_fad = 0;

uint32_t iso_get_ip_fad(void) {
    return cached_ip_fad;
}

int iso_load_1st_read(uint32_t data_fad) {
    uint8_t *sector = (uint8_t *)0x8C004000UL;
    uint8_t *ip_sector = (uint8_t *)0x8C006000UL;
    uint32_t file_fad  = 0;
    uint32_t file_size = 0;
    uint32_t pvd_fad   = 0;
    char boot_file[17] = { 0 };
    char wince_os = '0';
    cached_ip_fad = 0;

    /* 1. Read IP.BIN (sector 0 at data_fad) into dedicated buffer to determine target boot executable and OS type */
    if(gdrom_read_fad(ip_sector, data_fad, 1) == GDROM_OK) {
        wince_os = (char)ip_sector[0x3E];
        int len = 0;
        for(int i = 0; i < 16; i++) {
            char c = (char)ip_sector[0x60 + i];
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
                if(filename_match(name, name_len, "IP.BIN")) {
                    uint32_t ip_lba = read_le32_unaligned(sector + pos + 2);
                    cached_ip_fad = lba_to_fad(ip_lba, data_fad);
                }
                if(file_fad == 0 &&
                   (filename_match(name, name_len, boot_file) ||
                    filename_match(name, name_len, "1ST_READ.BIN") ||
                    filename_match(name, name_len, "0WINCEOS.BIN"))) {
                    uint32_t lba = read_le32_unaligned(sector + pos + 2);
                    uint32_t sz  = read_le32_unaligned(sector + pos + 10);
                    if(sz > 0 && sz <= 0x00D00000UL) {
                        file_fad  = lba_to_fad(lba, data_fad);
                        file_size = sz;
                        if(filename_match(name, name_len, "0WINCEOS.BIN")) {
                            wince_os = '1';
                        }
                    }
                }
            }
            pos += rec_len;
        }
        if(file_fad != 0 && cached_ip_fad != 0) break;
    }

    if(file_fad == 0 || file_size == 0)
        return GDROM_DEVICE_ERR;
    if(file_size > 0x00D00000UL)
        return GDROM_BAD_ARG;

    uint8_t *dest = (uint8_t *)0x8C010000UL;
    int is_wince = (wince_os == '1' ||
                    filename_match(boot_file, 12, "0WINCEOS.BIN") ||
                    filename_match(boot_file, 8, "0WINCEOS"));
    int is_gdi   = (gdrom_get_cached_disc_type() == 0x80);

    int load_res = GDROM_DEVICE_ERR;
    if(is_gdi) {
        /* Official GD-ROM / GDI High-Density format (Area 1) */
        load_res = gdi_load_binary(file_fad, file_size, dest, is_wince);
    } else if(homebrew_cdi_detect(ip_sector)) {
        /* KallistiOS / Homebrew CDI format */
        load_res = homebrew_cdi_load(file_fad, file_size, dest);
    } else {
        /* Commercial Self-Boot (MIL-CD / Retail Rip) CDI format */
        load_res = selfboot_cdi_load(file_fad, file_size, dest);
    }

    if(load_res != GDROM_OK) {
        return load_res;
    }

    /* Complete progress bar */
    screen_finish_progress();

    return GDROM_OK;
}
