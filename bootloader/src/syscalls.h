#ifndef CUSTOM_BOOTLOADER_SYSCALLS_H
#define CUSTOM_BOOTLOADER_SYSCALLS_H

#include <stdint.h>

#define KOS_SYSINFO_VECTOR   0x8C0000B0UL
#define KOS_BIOFONT_VECTOR   0x8C0000B4UL
#define KOS_FLASHROM_VECTOR  0x8C0000B8UL
#define KOS_GDROM_VECTOR     0x8C0000BCUL
#define KOS_GDROM2_VECTOR    0x8C0000C0UL
#define KOS_SYSTEM_VECTOR    0x8C0000E0UL

/* Install the standard Dreamcast BIOS syscall vector table & SYSINFO block */
void gdrom_install_syscall(void);

#endif /* CUSTOM_BOOTLOADER_SYSCALLS_H */
