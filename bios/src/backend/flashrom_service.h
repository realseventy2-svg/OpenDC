#ifndef __BIOS_FLASHROM_SERVICE_H
#define __BIOS_FLASHROM_SERVICE_H

#include <stdint.h>

#define FLASHROM_BASE_ADDR   0xA0200000UL
#define FLASHROM_P2_OFFSET   0x1C000UL
#define FLASHROM_P2_ADDR     (FLASHROM_BASE_ADDR + FLASHROM_P2_OFFSET)
#define FLASHROM_P2_SIZE     0x4000UL

typedef struct {
    int language;   /* 0=Japanese, 1=English, 2=German, 3=French, 4=Spanish, 5=Italian */
    int audio;      /* 1=Stereo, 0=Mono */
    int autostart;  /* 1=Enabled, 0=Disabled */
} bios_syscfg_t;

typedef struct {
    const char *name;
    uint32_t offset;
    uint32_t size;
} flash_partition_entry_t;

void flashrom_service_init(void);
int flashrom_service_get_syscfg(bios_syscfg_t *cfg);
int flashrom_service_set_syscfg(const bios_syscfg_t *cfg);
uint16_t flashrom_service_calc_crc(const uint8_t *data);

int flashrom_service_get_partition_count(void);
const flash_partition_entry_t *flashrom_service_get_partition(int index);

int flashrom_service_find_syscfg_slot(uint32_t *out_phys_addr, uint16_t *out_crc, uint16_t *out_calc_crc, int *out_has_magic);

#endif /* __BIOS_FLASHROM_SERVICE_H */
