#ifndef CDI_H
#define CDI_H

#include <stdint.h>
#include <stddef.h>

/**
 * Validates whether binary data begins with valid SH-4 entrypoint machine instructions.
 * Used to distinguish raw/unscrambled binaries from scrambled commercial rips.
 *
 * @param code Pointer to executable data buffer (at least 4-8 bytes).
 * @param len  Length of data buffer in bytes.
 * @return 1 if valid SH-4 entrypoint instructions detected, 0 if scrambled.
 */
int cdi_is_valid_sh4_entry(const uint8_t *code, size_t len);

/**
 * Loads a Self-Boot CDI executable (commercial scrambled or homebrew unscrambled).
 * Automatically streams sectors, checks scramble status, and descrambles only when needed.
 *
 * @param file_fad  Starting FAD of the executable.
 * @param file_size Total size of the executable in bytes.
 * @param dest      Destination memory address (typically 0x8C010000).
 * @return GDROM_OK on success, or an error code on failure.
 */
int cdi_load_binary(uint32_t file_fad, uint32_t file_size, uint8_t *dest);

#endif /* CDI_H */
