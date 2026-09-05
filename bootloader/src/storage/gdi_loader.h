#ifndef GDI_LOADER_H
#define GDI_LOADER_H

#include <stdint.h>
#include <stddef.h>

/**
 * Loads a GD-ROM (GDI) High-Density binary directly into RAM.
 *
 * @param file_fad  Starting FAD of the executable.
 * @param file_size Total size of the executable in bytes.
 * @param dest      Destination RAM address (0x8C010000).
 * @param is_wince  1 if Windows CE executable (0WINCEOS.BIN), 0 otherwise.
 * @return GDROM_OK on success, or an error code on failure.
 */
int gdi_load_binary(uint32_t file_fad, uint32_t file_size, uint8_t *dest, int is_wince);

#endif /* GDI_LOADER_H */
