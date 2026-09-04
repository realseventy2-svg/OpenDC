#include "gdi_loader.h"
#include "gdrom.h"

int gdi_load_binary(uint32_t file_fad, uint32_t file_size, uint8_t *dest, int is_wince) {
    volatile uint16_t *fb = (volatile uint16_t *)0xA5000000UL;

    if(is_wince) {
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
                    fb[y * 640 + (120 + x)] = 0x07E0; /* GREEN */
                }
            }
        }
    } else {
        /* Katana SDK / GDI high-density loading directly into 0x8C010000 */
        uint32_t total_sectors = (file_size + 2047U) / 2048U;
        uint32_t read_count = 0;
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
    }

    return GDROM_OK;
}
