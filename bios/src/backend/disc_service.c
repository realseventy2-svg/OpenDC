#include "disc_service.h"
#include "audio_driver.h"
#include "bootloader_gdrom.h"
#include <kos.h>
#include <string.h>

static disc_info_t s_disc;

void disc_service_init(void) {
    memset(&s_disc, 0, sizeof(s_disc));
    disc_service_probe();
}

void disc_service_probe(void) {
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

void disc_service_launch(void) {
    volatile gdrom_service_table_t *gd = gdrom_services();
    if(gd && gd->magic == GDROM_SERVICE_MAGIC && s_disc.disc_present) {
        audio_play_confirm();
        thd_sleep(250);
        uint32_t fad = s_disc.data_fad ? s_disc.data_fad : 45150U;
        irq_disable();
        gd->boot_game(fad);
    }
}

const disc_info_t *disc_service_get_info(void) {
    return &s_disc;
}
