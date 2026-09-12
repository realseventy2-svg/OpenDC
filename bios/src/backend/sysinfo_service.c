#include "sysinfo_service.h"
#include <kos.h>
#include <dc/video.h>

static const sysinfo_specs_t s_specs = {
    .cpu_model  = "Hitachi SH7091 (SH-4) 32-bit RISC",
    .cpu_speed  = "200.0 MHz",
    .cpu_cache  = "16-entry UTLB, 4-entry ITLB, 16KB O-Cache + 8KB I-Cache",
    .ram_size   = "16 MB 100MHz SDRAM",
    .ram_bus    = "Area 3 Bus Width: 32-bit Burst",
    .gpu_model  = "NEC PowerVR2 CLX2 3D Rasterizer",
    .gpu_speed  = "100.0 MHz",
    .vram_size  = "8 MB 128-bit Synchronous VRAM @ 0xA5000000",
    .audio_chip = "Yamaha AICA Sound Core (64 Channels PCM/ADPCM)",
    .audio_cpu  = "ARM7DI Sound CPU @ 45.0 MHz + 2MB Audio RAM"
};

const char *sysinfo_get_cable_name(void) {
    int cable = vid_check_cable();
    switch(cable) {
        case CT_VGA: return "VGA (480p 60Hz)";
        case CT_RGB: return "RGB (480i 60Hz)";
        case CT_COMPOSITE: return "Composite (480i)";
        default: return "Auto-Detect";
    }
}

const char *sysinfo_get_region_name(void) {
    const char *cc = (const char *)0x8C008030UL;
    int has_j = 0, has_u = 0, has_e = 0;
    for(int i = 0; i < 8; i++) {
        if(cc[i] == 'J') has_j = 1;
        if(cc[i] == 'U') has_u = 1;
        if(cc[i] == 'E') has_e = 1;
    }
    if(has_j && !has_u && !has_e) return "NTSC-J";
    if(has_e && !has_u && !has_j) return "PAL";
    if(has_u) return "NTSC-U";
    return "Universal";
}

const sysinfo_specs_t *sysinfo_get_specs(void) {
    return &s_specs;
}
