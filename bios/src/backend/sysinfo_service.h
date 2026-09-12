#ifndef __BIOS_SYSINFO_SERVICE_H
#define __BIOS_SYSINFO_SERVICE_H

#include <stdint.h>

typedef struct {
    const char *cpu_model;
    const char *cpu_speed;
    const char *cpu_cache;
    const char *ram_size;
    const char *ram_bus;
    const char *gpu_model;
    const char *gpu_speed;
    const char *vram_size;
    const char *audio_chip;
    const char *audio_cpu;
} sysinfo_specs_t;

const char *sysinfo_get_cable_name(void);
const char *sysinfo_get_region_name(void);
const sysinfo_specs_t *sysinfo_get_specs(void);

#endif /* __BIOS_SYSINFO_SERVICE_H */
