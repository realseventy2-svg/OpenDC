#include "selfboot_cdi.h"
#include "gdrom.h"
#include "scramble.h"
#include "screen.h"

int selfboot_cdi_detect(const uint8_t *ip_sector) {
    if(!ip_sector) return 0;

    /* Check for official Katana hardware ID */
    if(ip_sector[0] == 'S' && ip_sector[1] == 'E' &&
       ip_sector[2] == 'G' && ip_sector[3] == 'A') {
        return 1;
    }

    /* Check for commercial bootstrap code at ip_sector + 0x0300 */
    const uint32_t *ip_entry = (const uint32_t *)(ip_sector + 0x0300);
    if(*ip_entry != 0 && *ip_entry != 0xFFFFFFFFUL) {
        return 1;
    }

    return 0;
}

static int is_valid_sh4_prologue(const uint8_t *code) {
    if(!code) return 0;
    const uint16_t *insn = (const uint16_t *)code;

    int valid_count = 0;
    int invalid_count = 0;

    for(int i = 0; i < 32; i++) {
        uint16_t op = insn[i];

        /* Check known illegal / invalid opcodes on SH-4 */
        if(op == 0x0000 || (op >= 0x0001 && op <= 0x0008) ||
           op == 0x000A || (op >= 0x000C && op <= 0x001A) ||
           (op >= 0x001C && op <= 0x002A) || (op >= 0x002C && op <= 0x003A)) {
            invalid_count++;
            continue;
        }

        /* Undefined sub-opcodes in 0x4000 group */
        uint16_t sub4 = op & 0xF0FF;
        if(sub4 == 0x4008 || sub4 == 0x4009 || sub4 == 0x400A ||
           sub4 == 0x4018 || sub4 == 0x4019 || sub4 == 0x401A ||
           sub4 == 0x4028 || sub4 == 0x4029 || sub4 == 0x402A ||
           sub4 == 0x4038 || sub4 == 0x4039 || sub4 == 0x403A) {
            invalid_count++;
            continue;
        }

        /* Check common valid SH-4 instructions */
        uint16_t top4 = op & 0xF000;
        if(top4 == 0xD000 || top4 == 0xE000 || top4 == 0x6000 ||
           top4 == 0x2000 || top4 == 0x3000 || top4 == 0x4000 ||
           top4 == 0x7000 || top4 == 0x8000 || top4 == 0x9000 ||
           top4 == 0xA000 || top4 == 0xB000 || top4 == 0xC000 ||
           top4 == 0xF000 || op == 0x0009 || op == 0x000B) {
            valid_count++;
        }
    }

    return (invalid_count == 0 && valid_count >= 20);
}

int selfboot_cdi_load(uint32_t file_fad, uint32_t file_size, uint8_t *dest) {
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint32_t padded_size = total_sectors * 2048U;

    /* Use uncached P2 mirror for staging to prevent dirty cache retention */
    uint8_t *staging = (uint8_t *)0xAC700000UL;
    uint8_t *uncached_dest = (uint8_t *)((uint32_t)dest | 0x20000000UL);

    /* Stream all sectors into staging buffer at 0xAC700000 */
    uint32_t read_count = 0;
    while(read_count < total_sectors) {
        uint32_t batch = total_sectors - read_count;
        if(batch > 16U) batch = 16U;

        if(gdrom_read_fad(staging + (read_count * 2048U),
                          file_fad + read_count,
                          (uint16_t)batch) != GDROM_OK) {
            return GDROM_DEVICE_ERR;
        }
        read_count += batch;
        screen_update_progress(read_count, total_sectors);
    }

    /* 1. Descramble using the exact file size into destination buffer */
    gdrom_descramble(staging, uncached_dest, file_size);

    /* 2. If the raw staging buffer was already valid SH-4 machine code and descrambling broke it, keep raw */
    if(is_valid_sh4_prologue(staging) && !is_valid_sh4_prologue(uncached_dest)) {
        const uint32_t *src32 = (const uint32_t *)staging;
        uint32_t *dst32 = (uint32_t *)uncached_dest;
        for(uint32_t i = 0; i < (padded_size >> 2); i++) {
            dst32[i] = src32[i];
        }
    }

    return GDROM_OK;
}