#ifndef __BIOS_DISC_H
#define __BIOS_DISC_H

#include <stdint.h>

typedef struct {
    int disc_present;
    int is_gdrom;
    char title[64];
    char product_id[16];
    char version[16];
    char region[16];
    uint32_t data_fad;
} disc_info_t;

void disc_init(void);
void disc_probe(void);
void disc_launch(void);
const disc_info_t *disc_get_info(void);
const char *get_cable_name(void);
const char *get_region_name(void);

#endif /* __BIOS_DISC_H */
