#ifndef CUSTOM_BOOTLOADER_SCRAMBLE_H
#define CUSTOM_BOOTLOADER_SCRAMBLE_H

#include <stdint.h>

void scramble_srand(unsigned int n);
unsigned int scramble_rand(void);

/* In-memory bit-slice descrambler matching kos/utils/scramble/scramble.c */
void gdrom_descramble(const uint8_t *src, uint8_t *dst, uint32_t filesz);

#endif /* CUSTOM_BOOTLOADER_SCRAMBLE_H */
