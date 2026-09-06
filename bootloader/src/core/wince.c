#include "wince.h"

#define SB_BASE             0xA05F6800UL
#define SB_GDSTARD          (*(volatile uint32_t*)(0xA05F7404UL))

static int s_wince_active = 0;

int wince_detect(const uint8_t *sec0_header) {
    if(sec0_header) {
        const uint32_t *hdr = (const uint32_t *)sec0_header;
        if(hdr[0] == WINCE_ROMHDR_SIG) {
            s_wince_active = 1;
            return 1;
        }
    }

    const uint32_t *hdr_mirror = (const uint32_t *)WINCE_HDR_BASE;
    if(hdr_mirror[0] == WINCE_ROMHDR_SIG) {
        s_wince_active = 1;
        return 1;
    }

    s_wince_active = 0;
    return 0;
}

int wince_is_active(void) {
    return s_wince_active;
}

uint32_t wince_get_gdstard(void) {
    const uint32_t *wince_hdr = (const uint32_t *)WINCE_HDR_BASE;
    if(wince_hdr[0] == WINCE_ROMHDR_SIG) {
        /* WinCE IP.BIN bootstrap requires SB_GDSTARD to equal wince_hdr[3] + wince_hdr[4] */
        return wince_hdr[3] + wince_hdr[4];
    }
    /* Default Katana / Standard GD-ROM DMA base */
    return 0x0C110000UL;
}

void wince_apply_security_checksum(void) {
    /* Midway Games (San Francisco Rush, Midway Arcade Hits, Gauntlet Legends)
       BIOS entrypoint checksum calculation:
       Calculates checksum across 0x8C0010F0 and balances the sum to 0 using 98 words at 0x8C003174.
    */
    int16_t *p = (int16_t *)WINCE_GD_ENTRYPOINT;
    int32_t chksum = (int32_t)0xFFF937D1UL;

    for(int i = 0; i < 10; i++)
        chksum -= *p++;

    p += 0xEE - 1;
    for(int i = 0; i < 3; i++)
        chksum += *p++;

    p += 0x347 - 1;
    for(int i = 0; i < 11; i++)
        chksum -= *p++;

    p += 0xBF8 - 1;
    for(int i = 0; i < 98; i++) {
        int16_t v = chksum < 0 ? ((-chksum > 32767) ? 32767 : (int16_t)-chksum)
                               : ((chksum > 32768) ? -32768 : (int16_t)-chksum);
        *p = v;
        chksum += *p++;
    }
}

void wince_setup_env(void) {
    /* 1. Configure SB_GDSTARD register for WinCE memory map */
    SB_GDSTARD = wince_get_gdstard();

    /* 2. Apply Midway / San Francisco Rush BIOS checksum */
    wince_apply_security_checksum();
}
