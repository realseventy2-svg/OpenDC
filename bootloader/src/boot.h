#ifndef CUSTOM_BOOTLOADER_BOOT_H
#define CUSTOM_BOOTLOADER_BOOT_H

#include <stdint.h>

/* Orchestrates loading IP.BIN, 1ST_READ.BIN, installs syscalls, and hands off to the game */
int gdrom_boot_game(uint32_t data_fad);

#endif /* CUSTOM_BOOTLOADER_BOOT_H */
