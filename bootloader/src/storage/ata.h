#ifndef CUSTOM_BOOTLOADER_ATA_H
#define CUSTOM_BOOTLOADER_ATA_H

#include <stddef.h>
#include <stdint.h>

/* G1 ATA Status Bitflags */
enum {
    ATA_ST_ERR  = 0x01,
    ATA_ST_DRQ  = 0x08,
    ATA_ST_DF   = 0x20,
    ATA_ST_DRDY = 0x40,
    ATA_ST_BSY  = 0x80
};

/* Result Codes */
enum {
    ATA_OK         =  0,
    ATA_TIMEOUT    = -1,
    ATA_DEVICE_ERR = -2,
    ATA_BAD_ARG    = -3
};

void ata_delay(unsigned count);
uint8_t ata_status(void);
uint8_t ata_altstatus(void);
int ata_wait_status(uint8_t must_set, uint8_t must_clear);
int ata_wait_complete(void);

int ata_init(void);
int ata_packet_begin(uint16_t byte_count);
int ata_packet_write(const uint8_t packet[12]);
int ata_pio_read(void *dst, size_t bytes, int swap_words);

#endif /* CUSTOM_BOOTLOADER_ATA_H */
