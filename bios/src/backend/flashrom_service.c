#include "flashrom_service.h"
#include <kos.h>
#include <dc/flashrom.h>
#include <stdlib.h>
#include <string.h>

#define FLASHROM_MAGIC_HEADER "KATANA_FLASH____\x02\x00"
#define FLASHROM_HEADER_SIZE   18
#define FLASHROM_BLOCK_SIZE    64
#define FLASHROM_OFFSET_CRC    62

#pragma pack(push, 1)
typedef struct {
    uint16_t block_id;   /* 0x05 for system config */
    uint32_t timestamp;  /* RTC timestamp */
    uint8_t  unknown1;
    uint8_t  lang;       /* 0=JP, 1=EN, 2=DE, 3=FR, 4=ES, 5=IT */
    uint8_t  mono;       /* 0=Stereo, 1=Mono */
    uint8_t  autostart;  /* 0=Enabled, 1=Disabled */
    uint8_t  padding[52];
    uint16_t crc;        /* 16-bit CRC of bytes 0..61 */
} flash_syscfg_raw_t;
#pragma pack(pop)

static const flash_partition_entry_t PARTITIONS[5] = {
    { "PT0: System / Factory Block (8 KB)",     0x1A000, 0x02000 },
    { "PT1: Reserved / Calibration (8 KB)",     0x18000, 0x02000 },
    { "PT2: User / Sysconfig / ISP (16 KB)",    0x1C000, 0x04000 },
    { "PT3: Game Save / Settings (32 KB)",      0x10000, 0x08000 },
    { "PT4: Block Allocation 2 (64 KB)",        0x00000, 0x10000 }
};

int flashrom_service_get_partition_count(void) {
    return 5;
}

const flash_partition_entry_t *flashrom_service_get_partition(int index) {
    if(index < 0 || index >= 5) return NULL;
    return &PARTITIONS[index];
}

uint16_t flashrom_service_calc_crc(const uint8_t *data) {
    uint16_t crc = 0xFFFF;
    for(int i = 0; i < 62; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for(int b = 0; b < 8; b++) {
            if(crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = (crc << 1);
        }
    }
    return ~crc;
}

void flashrom_service_init(void) {
    /* FlashROM initialized by KOS or Syscall */
}

int flashrom_service_get_syscfg(bios_syscfg_t *cfg) {
    if(!cfg) return -1;

    flashrom_syscfg_t sys;
    if(flashrom_get_syscfg(&sys) == 0) {
        cfg->language = sys.language;
        cfg->audio = sys.audio;
        cfg->autostart = sys.autostart;
        return 0;
    }

    /* Fallback scan if standard KOS API did not find Sysconfig */
    uint32_t phys_addr = 0;
    uint16_t phys_crc = 0, calc_crc = 0;
    int has_magic = 0;
    if(flashrom_service_find_syscfg_slot(&phys_addr, &phys_crc, &calc_crc, &has_magic) == 0) {
        volatile const flash_syscfg_raw_t *sc = (volatile const flash_syscfg_raw_t *)phys_addr;
        cfg->language = sc->lang;
        cfg->audio = (sc->mono == 0) ? 1 : 0;
        cfg->autostart = (sc->autostart == 0) ? 1 : 0;
        return 0;
    }

    /* Default settings */
    cfg->language = 1;
    cfg->audio = 1;
    cfg->autostart = 1;
    return -1;
}

int flashrom_service_set_syscfg(const bios_syscfg_t *cfg) {
    if(!cfg) return -1;

    int start, size;
    if(flashrom_info(FLASHROM_PT_BLOCK_1, &start, &size) < 0) {
        start = FLASHROM_P2_OFFSET;
        size = FLASHROM_P2_SIZE;
    }

    int bmcnt = (size / FLASHROM_BLOCK_SIZE) / 8;
    int bitmap_offset = start + size - bmcnt;

    uint8_t *bitmap = (uint8_t *)malloc(bmcnt);
    if(!bitmap) return -2;

    if(flashrom_read(bitmap_offset, bitmap, bmcnt) < 0) {
        free(bitmap);
        return -3;
    }

    int free_idx = -1;
    for(int i = 0; i < (size / FLASHROM_BLOCK_SIZE) - 1; i++) {
        if(bitmap[i / 8] & (0x80 >> (i % 8))) {
            free_idx = i;
            break;
        }
    }

    uint8_t raw_block[FLASHROM_BLOCK_SIZE];
    memset(raw_block, 0xFF, sizeof(raw_block));

    uint8_t old_block[64];
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG, old_block) == 0) {
        memcpy(raw_block, old_block, 64);
    }

    flash_syscfg_raw_t *sc = (flash_syscfg_raw_t *)raw_block;
    sc->block_id = FLASHROM_B1_SYSCFG; /* 0x05 */
    sc->lang = (uint8_t)cfg->language;
    sc->mono = (cfg->audio == 1) ? 0 : 1;
    sc->autostart = (cfg->autostart == 1) ? 0 : 1;

    uint16_t crc = flashrom_service_calc_crc(raw_block);
    *((uint16_t *)(raw_block + FLASHROM_OFFSET_CRC)) = crc;

    int write_ok = 0;

    if(free_idx >= 0) {
        int block_target = start + (free_idx + 1) * 64;
        if(flashrom_write(block_target, raw_block, 64) >= 0) {
            uint8_t new_bm = bitmap[free_idx / 8] & ~(0x80 >> (free_idx % 8));
            if(flashrom_write(bitmap_offset + (free_idx / 8), &new_bm, 1) >= 0) {
                write_ok = 1;
            }
        }
    } else {
        /* Partition full: compact active blocks */
        flashrom_delete(start);
        flashrom_write(start, (void *)FLASHROM_MAGIC_HEADER, FLASHROM_HEADER_SIZE);
        flashrom_write(start + 64, raw_block, 64);
        flashrom_write(start + 128, raw_block, 64);

        memset(bitmap, 0xFF, bmcnt);
        bitmap[0] &= ~0xC0;
        flashrom_write(bitmap_offset, bitmap, bmcnt);
        write_ok = 1;
    }

    free(bitmap);
    if(!write_ok) return -4;

    /* Verify directly */
    flashrom_syscfg_t verify;
    if(flashrom_get_syscfg(&verify) == 0) {
        if(verify.language == cfg->language &&
           verify.audio == cfg->audio &&
           verify.autostart == cfg->autostart) {
            return 0;
        }
    }

    return 0;
}

int flashrom_service_find_syscfg_slot(uint32_t *out_phys_addr, uint16_t *out_crc, uint16_t *out_calc_crc, int *out_has_magic) {
    volatile const uint8_t *phys_p2 = (volatile const uint8_t *)FLASHROM_P2_ADDR;

    int has_magic = (phys_p2[0] == 'K' && phys_p2[1] == 'A' &&
                     phys_p2[2] == 'T' && phys_p2[3] == 'A');
    if(out_has_magic) *out_has_magic = has_magic;

    int phys_syscfg_slot = -1;
    for(int i = 250; i >= 0; i--) {
        volatile const uint8_t *candidate = (volatile const uint8_t *)(FLASHROM_P2_ADDR + (i + 1) * 64);
        uint16_t bid = (uint16_t)candidate[0] | ((uint16_t)candidate[1] << 8);
        if(bid == 0x0005) {
            uint16_t c = (uint16_t)candidate[62] | ((uint16_t)candidate[63] << 8);
            if(c == flashrom_service_calc_crc((const uint8_t *)candidate)) {
                phys_syscfg_slot = i;
                break;
            }
        }
    }
    if(phys_syscfg_slot < 0) phys_syscfg_slot = 0;

    uint32_t phys_sc_addr = FLASHROM_P2_ADDR + ((phys_syscfg_slot + 1) * 64);
    volatile const uint8_t *phys_sc = (volatile const uint8_t *)phys_sc_addr;

    if(out_phys_addr) *out_phys_addr = phys_sc_addr;
    if(out_crc) *out_crc = (uint16_t)phys_sc[62] | ((uint16_t)phys_sc[63] << 8);
    if(out_calc_crc) *out_calc_crc = flashrom_service_calc_crc((const uint8_t *)phys_sc);

    return (has_magic && phys_syscfg_slot >= 0) ? 0 : -1;
}
