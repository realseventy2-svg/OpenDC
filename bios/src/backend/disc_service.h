#ifndef __BIOS_DISC_SERVICE_H
#define __BIOS_DISC_SERVICE_H

#include <stdint.h>

typedef struct {
    int disc_present;
    int is_gdrom;
    char title[128];
    char product_id[16];
    char version[8];
    char region[10];
    uint32_t data_fad;
} disc_info_t;

void disc_service_init(void);
void disc_service_probe(void);
void disc_service_launch(void);
const disc_info_t *disc_service_get_info(void);

#endif /* __BIOS_DISC_SERVICE_H */
