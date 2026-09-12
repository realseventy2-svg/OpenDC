#ifndef CUSTOM_BIOS_BOOTLOADER_GDROM_H
#define CUSTOM_BIOS_BOOTLOADER_GDROM_H

#include <stdint.h>

#define GDROM_SERVICE_MAGIC   0x4744524FUL /* "GDRO" */
#define GDROM_SERVICE_VERSION 1U
#define GDROM_SERVICE_ADDR    0x8C00F000UL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    int (*init)(void);
    uint8_t (*status)(void);
    int (*drive_ready)(void);
    int (*read_toc)(void *buffer, uint8_t session);
    int (*read_fad)(void *buffer, uint32_t fad, uint16_t sectors);
    int (*boot_game)(uint32_t data_fad);
    uint32_t disc_present;
    uint32_t data_fad;
} gdrom_service_table_t;

/* Access the table published by the bootloader. */
static inline volatile gdrom_service_table_t *gdrom_services(void) {
    return (volatile gdrom_service_table_t *)GDROM_SERVICE_ADDR;
}

#endif /* CUSTOM_BIOS_BOOTLOADER_GDROM_H */
