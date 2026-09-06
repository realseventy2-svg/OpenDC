#ifndef SELFBOOT_CDI_H
#define SELFBOOT_CDI_H

#include <stdint.h>
#include <stddef.h>

/**
 * Detects if the current disc is a Commercial Self-Boot (MIL-CD / Retail Rip) CDI.
 *
 * @param ip_sector Pointer to sector 0 of track data (IP.BIN header).
 * @return 1 if commercial self-boot CDI, 0 otherwise.
 */
int selfboot_cdi_detect(const uint8_t *ip_sector);

/**
 * Loads a Commercial Self-Boot (MIL-CD / Retail Rip) CDI binary into 0x8C010000.
 * Automatically handles scrambled retail rips and raw unscrambled rips.
 *
 * @param file_fad  Starting FAD of the executable.
 * @param file_size Total size of the executable in bytes.
 * @param dest      Destination RAM address (0x8C010000).
 * @return GDROM_OK on success, or an error code on failure.
 */
int selfboot_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest);

#endif /* SELFBOOT_CDI_H */
