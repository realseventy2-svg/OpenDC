#ifndef CUSTOM_BOOTLOADER_GDROM_H
#define CUSTOM_BOOTLOADER_GDROM_H

#include <stddef.h>
#include <stdint.h>
#include "ata.h"

enum {
    GDROM_OK          = 0,
    GDROM_TIMEOUT     = -1,
    GDROM_DEVICE_ERR  = -2,
    GDROM_BAD_ARG     = -3,
    GDROM_NOT_READY   = -4
};

enum {
    GDROM_ST_ERR  = ATA_ST_ERR,
    GDROM_ST_DRQ  = ATA_ST_DRQ,
    GDROM_ST_DF   = ATA_ST_DF,
    GDROM_ST_DRDY = ATA_ST_DRDY,
    GDROM_ST_BSY  = ATA_ST_BSY
};

typedef struct {
    uint32_t entry[99];
    uint32_t first;
    uint32_t last;
    uint32_t leadout_sector;
} kos_toc_t;

#define GDROM_SERVICE_MAGIC   0x4744524FUL /* "GDRO" */
#define GDROM_SERVICE_VERSION 1
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

/* Initialize G1 ATA interface and select master device */
int gdrom_init(void);

/* Check drive status */
uint8_t gdrom_status(void);
int gdrom_drive_ready(void);

/* Send drive wakeup/ready packet (REQ_STAT 0x70) */
int gdrom_prepare_disk(void);

/* Read raw or public TOC format */
int gdrom_read_raw_toc(uint8_t *buffer, uint8_t session);
int gdrom_read_toc(void *buffer, uint8_t session);

/* Read sectors by FAD */
int gdrom_read_fad(void *buffer, uint32_t fad, uint16_t sectors);

/* Disc and ISO probing */
int gdrom_probe_toc(void);
int gdrom_probe_iso(uint32_t *data_fad, uint8_t pvd_head[8]);

/* Query cached data track FAD and disc type */
uint32_t gdrom_get_cached_data_fad(void);
int32_t gdrom_get_cached_disc_type(void);

/* Publish services to RAM */
void gdrom_install_services(void);

/* Include modular subsystem headers */
#include "iso9660.h"
#include "scramble.h"
#include "syscalls.h"
#include "boot.h"

#endif /* CUSTOM_BOOTLOADER_GDROM_H */