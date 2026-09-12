#include "disc.h"
#include "font.h"
#include "audio.h"
#include "bootloader_gdrom.h"
#include <kos.h>
#include <dc/video.h>
#include <string.h>

static disc_info_t s_disc;

const char *get_cable_name(void) {
    int cable = vid_check_cable();
    switch(cable) {
        case CT_VGA: return "VGA (480p 60Hz)";
        case CT_RGB: return "RGB (480i 60Hz)";
        case CT_COMPOSITE: return "Composite (480i)";
        default: return "Auto-Detect";
    }
}

const char *get_region_name(void) {
    const char *cc = (const char *)0x8C008030UL;
    int has_j = 0, has_u = 0, has_e = 0;
    for(int i = 0; i < 8; i++) {
        if(cc[i] == 'J') has_j = 1;
        if(cc[i] == 'U') has_u = 1;
        if(cc[i] == 'E') has_e = 1;
    }
    if(has_j && !has_u && !has_e) return "NTSC-J";
    if(has_e && !has_u && !has_j) return "PAL";
    if(has_u) return "NTSC-U";
    return "Universal";
}

void disc_init(void) {
    memset(&s_disc, 0, sizeof(s_disc));
    disc_probe();
}

void disc_probe(void) {
    memset(&s_disc, 0, sizeof(s_disc));
    volatile gdrom_service_table_t *gd = gdrom_services();

    if(gd && gd->magic == GDROM_SERVICE_MAGIC) {
        s_disc.disc_present = (gd->disc_present != 0);
        s_disc.data_fad = gd->data_fad;
    }

    uint8_t ip_sector[2048];
    if(gd && gd->magic == GDROM_SERVICE_MAGIC && s_disc.disc_present) {
        uint32_t fad = s_disc.data_fad ? s_disc.data_fad : 45150U;
        if(gd->read_fad(ip_sector, fad, 1) == 0) {
            if(ip_sector[0] == 'S' && ip_sector[1] == 'E' &&
               ip_sector[2] == 'G' && ip_sector[3] == 'A') {
                char *t = (char *)(ip_sector + 0x80);
                int len = 0;
                for(int i = 0; i < 63 && i < 128; i++) {
                    if(t[i] >= 32) s_disc.title[len++] = t[i];
                }
                while(len > 0 && s_disc.title[len - 1] == ' ') len--;
                s_disc.title[len] = '\0';

                memcpy(s_disc.product_id, ip_sector + 0x40, 10);
                s_disc.product_id[10] = '\0';

                memcpy(s_disc.version, ip_sector + 0x50, 6);
                s_disc.version[6] = '\0';

                memcpy(s_disc.region, ip_sector + 0x30, 8);
                s_disc.region[8] = '\0';

                s_disc.is_gdrom = (fad >= 45000U);
            }
        }
    }

    if(s_disc.title[0] == '\0') {
        if(s_disc.disc_present) {
            strcpy(s_disc.title, "Unidentified Disc / CD-ROM");
        } else {
            strcpy(s_disc.title, "NO DISC DETECTED");
        }
    }
}

void disc_launch(void) {
    volatile gdrom_service_table_t *gd = gdrom_services();
    if(gd && gd->magic == GDROM_SERVICE_MAGIC && s_disc.disc_present) {
        audio_play_confirm();
        thd_sleep(250);
        uint32_t fad = s_disc.data_fad ? s_disc.data_fad : 45150U;
        irq_disable();
        gd->boot_game(fad);
    }
}

const disc_info_t *disc_get_info(void) {
    return &s_disc;
}
