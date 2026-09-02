#ifndef CUSTOM_BOOTLOADER_ISO9660_H
#define CUSTOM_BOOTLOADER_ISO9660_H

#include <stdint.h>

/* Safely read unaligned 32-bit little-endian integer on SH-4 */
uint32_t read_le32_unaligned(const uint8_t *p);

/* Map ISO9660 directory record LBA to physical disc FAD */
uint32_t lba_to_fad(uint32_t lba, uint32_t data_fad);

/* Locate and load 1ST_READ.BIN (descrambling if needed) into 0x8C010000 */
int iso_load_1st_read(uint32_t data_fad);

#endif /* CUSTOM_BOOTLOADER_ISO9660_H */
