#ifndef CUSTOM_BOOTLOADER_GDROM_H
#define CUSTOM_BOOTLOADER_GDROM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Low-level GD-ROM transport for the Dreamcast G1 ATA bridge.
 *
 * This is intentionally not a KOS syscall/vector-table implementation.  The
 * bootloader has no KOS runtime, and the retail GD-ROM packet command set is
 * separate from the KOS syscall numbers.
 */

enum {
    GDROM_OK          = 0,
    GDROM_TIMEOUT     = -1,
    GDROM_DEVICE_ERR  = -2,
    GDROM_BAD_ARG     = -3,
    GDROM_NOT_READY   = -4
};

enum {
    GDROM_ST_ERR  = 0x01,
    GDROM_ST_DRQ  = 0x08,
    GDROM_ST_DF   = 0x20,
    GDROM_ST_DRDY = 0x40,
    GDROM_ST_BSY  = 0x80
};

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
} gdrom_service_table_t;

/* Configure the G1 PIO timing and select the GD-ROM device. */
int gdrom_init(void);

/* Read the ATA status register without changing device state. */
uint8_t gdrom_status(void);

/* Returns non-zero when the device is present and not reporting an error. */
int gdrom_drive_ready(void);

/* Start an ATA PACKET transaction and submit its 12-byte command packet. */
int gdrom_packet_begin(uint16_t byte_count);
int gdrom_packet_write(const uint8_t packet[12]);

/* Read a data phase after the drive asserts DRQ. */
int gdrom_pio_read(void *dst, size_t bytes);

/* Wait until the current command completes. */
int gdrom_wait_complete(void);

/* Read the 408-byte Sega TOC and 2048-byte FAD sectors. */
int gdrom_read_toc(void *buffer, uint8_t session);
int gdrom_read_fad(void *buffer, uint32_t fad, uint16_t sectors);

/* Probe the drive and TOC without relying on the KOS syscall layer. */
int gdrom_probe_toc(void);
int gdrom_probe_iso(uint32_t *data_fad, uint8_t pvd_head[8]);

/* Publish the bootloader GD-ROM ABI in RAM for the next stage. */
void gdrom_install_services(void);
void gdrom_install_syscall(void);

#endif
