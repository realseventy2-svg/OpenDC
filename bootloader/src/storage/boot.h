#ifndef CUSTOM_BOOTLOADER_BOOT_H
#define CUSTOM_BOOTLOADER_BOOT_H

#include <stdint.h>

/* Configure whether the Sega License Screen (0xAC008300 in IP.BIN) is shown (1) or bypassed (0) */
void boot_set_sega_license_enabled(int enabled);
int boot_get_sega_license_enabled(void);

/* Orchestrates loading IP.BIN, 1ST_READ.BIN, installs syscalls, and hands off to the game */
int gdrom_boot_game(uint32_t data_fad);

#endif /* CUSTOM_BOOTLOADER_BOOT_H */
