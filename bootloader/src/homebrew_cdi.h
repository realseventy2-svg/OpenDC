#ifndef HOMEBREW_CDI_H
#define HOMEBREW_CDI_H

#include <stdint.h>
#include <stddef.h>

/**
 * Detects if the current disc is a KallistiOS or community homebrew CDI.
 *
 * @param ip_sector Pointer to sector 0 of track data (IP.BIN header).
 * @return 1 if KallistiOS/homebrew disc, 0 otherwise.
 */
int homebrew_cdi_detect(const uint8_t *ip_sector);

/**
 * Loads a KallistiOS or homebrew CDI binary into 0x8C010000.
 * Handles both make-cdi.sh scrambled binaries and raw unscrambled binaries.
 *
 * @param file_fad  Starting FAD of the executable.
 * @param file_size Total size of the executable in bytes.
 * @param dest      Destination RAM address (0x8C010000).
 * @return GDROM_OK on success, or an error code on failure.
 */
int homebrew_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest);

#endif /* HOMEBREW_CDI_H */
