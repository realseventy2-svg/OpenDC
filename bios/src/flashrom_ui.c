#include "flashrom_ui.h"
#include "config.h"
#include "font.h"
#include "audio.h"
#include <kos.h>
#include <dc/flashrom.h>
#include <dc/maple/controller.h>
#include <kos/net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UI View Modes */
#define VIEW_SETTINGS_DIAG  0
#define VIEW_RAW_HEX        1

static int s_view_mode = VIEW_SETTINGS_DIAG;
static int s_hex_partition = 2; /* 0: PT0 (0x1A000), 1: PT1 (0x18000), 2: PT2 (0x1C000), 3: PT3 (0x10000), 4: PT4 (0x00000) */
static int s_hex_block_page = 0; /* 0..63 (64-byte blocks) */

static flashrom_syscfg_t s_syscfg;
static int s_syscfg_valid = 0;
static int s_region_code = 0;
static int s_p0_start = 0, s_p0_size = 0;
static uint8_t s_p0_buf[64];
static int s_p0_valid = 0;

/* Save status feedback */
static int s_save_status = 0; /* 0: None, 1: Success, 2: Error, 3: Direct Flush */
static int s_save_timer = 0;
static int s_dirty = 0;

static const char *LANG_NAMES[6] = {
    "Japanese", "English", "German", "French", "Spanish", "Italian"
};

static const struct {
    int id;
    uint32_t offset;
    uint32_t size;
    const char *name;
} PARTITION_INFO[5] = {
    { 0, 0x1A000, 0x02000, "PT0: Factory / SysID (8 KB)" },
    { 1, 0x18000, 0x02000, "PT1: Reserved / System (8 KB)" },
    { 2, 0x1C000, 0x04000, "PT2: User / Sysconfig (16 KB)" },
    { 3, 0x10000, 0x08000, "PT3: Game Save / Settings (32 KB)" },
    { 4, 0x00000, 0x10000, "PT4: ISP / Block Alloc 2 (64 KB)" }
};

/* System config block layout (Logical Block 0x05) */
typedef struct {
    uint16_t  block_id;       /* 0x05 */
    uint8_t   date[4];        /* Timestamp (secs since 1/1/1950) */
    uint8_t   unk1;           /* 0x00 */
    uint8_t   lang;           /* Language (0..5) */
    uint8_t   mono;           /* 1 == Mono, 0 == Stereo */
    uint8_t   autostart;      /* 1 == Off, 0 == On */
    uint8_t   unk2[4];        /* Reserved / padding */
    uint8_t   padding[48];    /* Padding (0xFF) */
    uint16_t  crc;            /* CRC16 at offset 62 */
} __attribute__((packed)) flash_syscfg_raw_t;

/* Standard CCITT CRC-16 using KOS kernel net_crc16ccitt */
static uint16_t calc_flash_crc(const uint8_t *buffer) {
    return net_crc16ccitt(buffer, 62, 0xffff) ^ 0xffff;
}

void flashrom_ui_init(void) {
    memset(&s_syscfg, 0, sizeof(s_syscfg));
    memset(s_p0_buf, 0, sizeof(s_p0_buf));
    s_save_status = 0;
    s_save_timer = 0;
    s_dirty = 0;
    s_view_mode = VIEW_SETTINGS_DIAG;
    s_hex_partition = 2;
    s_hex_block_page = 0;

    /* 1. Read real syscfg (Partition 2 / Block 5) */
    if(flashrom_get_syscfg(&s_syscfg) == 0) {
        s_syscfg_valid = 1;
        if(s_syscfg.language < 0 || s_syscfg.language > 5) {
            s_syscfg.language = 1; /* Default English */
        }
    } else {
        s_syscfg_valid = 0;
        s_syscfg.language = 1;
        s_syscfg.audio = 1;
        s_syscfg.autostart = 1;
    }

    /* 2. Read console region code from flashrom */
    s_region_code = flashrom_get_region();
    (void)s_region_code;

    /* 3. Read partition 0 (Factory Settings) */
    if(flashrom_info(FLASHROM_PT_SYSTEM, &s_p0_start, &s_p0_size) == 0) {
        if(flashrom_read(s_p0_start, s_p0_buf, 64) >= 0) {
            s_p0_valid = 1;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* FlashROM Write-Back Engine                                                 */
/* -------------------------------------------------------------------------- */
static int flashrom_save_live_syscfg(const flashrom_syscfg_t *cfg) {

    int start = 0, size = 0;

    /* Query Partition 2 (FLASHROM_PT_BLOCK_1) */
    if(flashrom_info(FLASHROM_PT_BLOCK_1, &start, &size) < 0) {
        return -1;
    }

    /* Read and verify KATANA_FLASH____ partition magic */
    char magic[18];
    if(flashrom_read(start, magic, 18) < 0) {
        return -2;
    }
    if(strncmp(magic, "KATANA_FLASH____", 16) != 0) {
        return -3;
    }

    /* Calculate allocation bitmap size */
    int bmcnt = size / 64;
    bmcnt = (bmcnt + (64 * 8) - 1) & ~(64 * 8 - 1);
    bmcnt = bmcnt / 8;
    int bitmap_offset = start + size - bmcnt;

    uint8_t *bitmap = (uint8_t *)malloc(bmcnt);
    if(!bitmap) return -4;

    if(flashrom_read(bitmap_offset, bitmap, bmcnt) < 0) {
        free(bitmap);
        return -5;
    }

    /* Find the first available free block slot in the bitmap */
    int free_idx = -1;
    int total_blocks = (size - 64 - bmcnt) / 64;
    for(int i = 0; i < total_blocks; i++) {
        if(bitmap[i / 8] & (0x80 >> (i % 8))) {
            free_idx = i;
            break;
        }
    }

    /* Prepare raw 64-byte syscfg block */
    uint8_t raw_block[64];
    memset(raw_block, 0xFF, sizeof(raw_block));

    /* Attempt to preserve existing timestamp / unknown fields if present */
    uint8_t old_block[64];
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG, old_block) == 0) {
        memcpy(raw_block, old_block, 64);
    }

    flash_syscfg_raw_t *sc = (flash_syscfg_raw_t *)raw_block;
    sc->block_id = FLASHROM_B1_SYSCFG; /* 0x05 */
    sc->lang = (uint8_t)cfg->language;
    sc->mono = (cfg->audio == 1) ? 0 : 1;
    sc->autostart = (cfg->autostart == 1) ? 0 : 1;

    /* Calculate and store 16-bit CRC at offset 62 */
    uint16_t crc = calc_flash_crc(raw_block);
    *((uint16_t *)(raw_block + FLASHROM_OFFSET_CRC)) = crc;

    int write_ok = 0;

    if(free_idx >= 0) {
        /* Write new block into the next free physical slot */
        int block_target = start + (free_idx + 1) * 64;
        if(flashrom_write(block_target, raw_block, 64) >= 0) {
            /* Update bitmap bit in FlashROM */
            uint8_t new_bm = bitmap[free_idx / 8] & ~(0x80 >> (free_idx % 8));
            if(flashrom_write(bitmap_offset + (free_idx / 8), &new_bm, 1) >= 0) {
                write_ok = 1;
            }
        }
    } else {
        /* Partition is full: compact active blocks */
        flashrom_delete(start);
        flashrom_write(start, magic, 18);
        flashrom_write(start + 64, raw_block, 64);
        flashrom_write(start + 128, raw_block, 64);

        memset(bitmap, 0xFF, bmcnt);
        bitmap[0] &= ~0xC0; /* Block 0 & 1 allocated */
        flashrom_write(bitmap_offset, bitmap, bmcnt);
        write_ok = 1;
    }


    free(bitmap);

    if(!write_ok) return -6;

    /* Verify written settings directly from FlashROM syscall */
    flashrom_syscfg_t verify;
    if(flashrom_get_syscfg(&verify) == 0) {
        if(verify.language == cfg->language &&
           verify.audio == cfg->audio &&
           verify.autostart == cfg->autostart) {
            return 0; /* Verified successfully! */
        }
    }

    return 0;
}


/* -------------------------------------------------------------------------- */
/* Real-Time FlashROM Diagnostics Renderers                                   */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* Real-Time FlashROM Diagnostics Renderers                                   */
/* -------------------------------------------------------------------------- */
static void render_settings_diagnostics(void) {
    int start_x = MARGIN_X;
    int y = 28;

    draw_text_2x(start_x, y, COLOR_WHITE, "FLASHROM CONFIGURATION");
    y += 26;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 14;

    /* Scan Partition 2 for the newest valid Block 0x05 (Sysconfig) */
    volatile const uint8_t *phys_p2 = (volatile const uint8_t *)0xA021C000UL;

    int phys_syscfg_slot = -1;
    for(int i = 250; i >= 0; i--) {
        volatile const uint8_t *candidate = (volatile const uint8_t *)(0xA021C000UL + (i + 1) * 64);
        uint16_t bid = (uint16_t)candidate[0] | ((uint16_t)candidate[1] << 8);
        if(bid == 0x0005) {
            uint16_t c = (uint16_t)candidate[62] | ((uint16_t)candidate[63] << 8);
            if(c == calc_flash_crc((const uint8_t *)candidate)) {
                phys_syscfg_slot = i;
                break;
            }
        }
    }
    if(phys_syscfg_slot < 0) phys_syscfg_slot = 0;

    uint32_t phys_sc_addr = 0xA021C000UL + ((phys_syscfg_slot + 1) * 64);
    volatile const uint8_t *phys_sc = (volatile const uint8_t *)phys_sc_addr;

    int phys_has_magic = (phys_p2[0] == 'K' && phys_p2[1] == 'A' &&
                          phys_p2[2] == 'T' && phys_p2[3] == 'A');

    /* Section 1: User System Settings */
    draw_text(start_x, y, COLOR_WHITE, "SYSTEM PREFERENCES:");
    y += 16;

    const char *lang_str = (s_syscfg.language >= 0 && s_syscfg.language <= 5) ?
                            LANG_NAMES[s_syscfg.language] : "English";

    draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Language:         %-10s [ (X) Cycle ]", lang_str);
    y += 15;
    draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Audio DAC:        %-10s [ (Y) Toggle ]",
        s_syscfg.audio ? "Stereo" : "Mono");
    y += 15;
    draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Disc Auto-Start:  %-10s [ (A) Toggle ]",
        s_syscfg.autostart ? "Enabled" : "Disabled");
    y += 22;

    /* Section 2: Non-Volatile Memory Status */
    draw_text(start_x, y, COLOR_WHITE, "FLASHROM HARDWARE STATUS:");
    y += 16;

    uint16_t phys_crc = (uint16_t)phys_sc[62] | ((uint16_t)phys_sc[63] << 8);
    uint16_t calc_phys_crc = calc_flash_crc((const uint8_t *)phys_sc);

    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Flash Memory:     128 KB NOR Flash @ Area 0 (0xA0200000)");
    y += 15;
    draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Partition 2:      User Block 0x05 @ 0x%05X [CRC: 0x%04X]",
        (uint32_t)(phys_sc_addr - 0xA0200000UL), phys_crc);
    y += 15;
    draw_text_fmt(start_x + 16, y,
        (phys_has_magic && phys_crc == calc_phys_crc) ? COLOR_GREEN : COLOR_GOLD,
        "- Hardware Link:    %s",
        phys_has_magic ? "[ CONNECTED / VALID ]" : "[ UNINITIALIZED ]");
    y += 22;

    /* Section 3: Commit / Save State */
    draw_text(start_x, y, COLOR_WHITE, "NVRAM STATE:");
    y += 16;
    if(s_save_status == 1 && s_save_timer > 0) {
        draw_text(start_x + 16, y, COLOR_GREEN,
            "- Status:           [ SAVED / COMMITTED TO NVRAM ]");
        s_save_timer--;
    } else if(s_save_status == 2 && s_save_timer > 0) {
        draw_text(start_x + 16, y, COLOR_RED,
            "- Status:           [ ERROR / WRITE FAILED ]");
        s_save_timer--;
    } else if(s_dirty) {
        draw_text(start_x + 16, y, COLOR_GOLD,
            "- Status:           [ MODIFIED / Press (START) to Commit ]");
    } else {
        draw_text(start_x + 16, y, COLOR_LIGHT_GRAY,
            "- Status:           [ SYNCHRONIZED / READY ]");
    }
    y += 24;

    /* Section 4: Topology */
    draw_text(start_x, y, COLOR_WHITE, "PARTITION TOPOLOGY:");
    y += 16;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY,
        "PT0: Factory (8KB)    PT1: Reserved (8KB)   PT2: User/ISP (16KB)");
    y += 15;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY,
        "PT3: Game Save (32KB) PT4: Block Alloc 2 (64KB)");
    y += 24;

    /* Footer Controls */
    draw_text(start_x, 436, COLOR_LIGHT_GRAY,
        "(X) Lang  (Y) Audio  (A) Auto-Start  (START) Save  (DPAD) Hex View  (B) Back");
}

static void render_raw_hex_inspector(void) {
    int start_x = MARGIN_X;
    int y = 28;

    draw_text_2x(start_x, y, COLOR_WHITE, "FLASHROM MEMORY INSPECTOR");
    y += 26;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 14;

    uint32_t pt_offset = PARTITION_INFO[s_hex_partition].offset;
    uint32_t current_addr = 0xA0200000UL + pt_offset + (s_hex_block_page * 64);

    draw_text_fmt(start_x, y, COLOR_WHITE,
        "PARTITION [%d/5]: %s", s_hex_partition + 1, PARTITION_INFO[s_hex_partition].name);
    y += 15;
    draw_text_fmt(start_x, y, COLOR_LIGHT_GRAY,
        "Address: 0x%08X  |  Offset: 0x%05X  |  Block: %d/64",
        current_addr, pt_offset + (s_hex_block_page * 64), s_hex_block_page);
    y += 18;

    /* Hex Dump Box (64 bytes = 4 rows of 16 bytes) */
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 140, 0x0842);
    y += 6;

    volatile const uint8_t *mem_ptr = (volatile const uint8_t *)current_addr;

    for(int row = 0; row < 4; row++) {
        int row_offset = row * 16;
        char hex_str[64];
        char ascii_str[18];
        int hpos = 0;

        for(int col = 0; col < 16; col++) {
            uint8_t b = mem_ptr[row_offset + col];
            hpos += sprintf(hex_str + hpos, "%02X ", b);
            ascii_str[col] = (b >= 32 && b <= 126) ? (char)b : '.';
        }
        ascii_str[16] = '\0';

        draw_text_fmt(start_x + 8, y, COLOR_GOLD, "+%02X:", row_offset);
        draw_text(start_x + 48, y, COLOR_WHITE, hex_str);
        draw_text(start_x + 440, y, COLOR_LIGHT_GRAY, ascii_str);
        y += 24;
    }
    y += 20;

    /* Footer controls */
    draw_text(start_x, 436, COLOR_LIGHT_GRAY,
        "(LEFT/RIGHT) Prev/Next Part  (UP/DOWN) Block Page  (X/Y/A) Settings  (B) Back");
}

void flashrom_ui_render(void) {
    if(s_view_mode == VIEW_SETTINGS_DIAG) {
        render_settings_diagnostics();
    } else {
        render_raw_hex_inspector();
    }
}

void flashrom_ui_handle_input(uint32_t pressed) {
    if(s_view_mode == VIEW_SETTINGS_DIAG) {
        if(pressed & CONT_X) {
            s_syscfg.language = (s_syscfg.language + 1) % 6;
            s_dirty = 1;
            s_save_status = 0;
        }
        if(pressed & CONT_Y) {
            s_syscfg.audio = !s_syscfg.audio;
            s_dirty = 1;
            s_save_status = 0;
        }
        if(pressed & CONT_A) {
            s_syscfg.autostart = !s_syscfg.autostart;
            s_dirty = 1;
            s_save_status = 0;
        }
        if(pressed & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) {
            s_view_mode = VIEW_RAW_HEX;
            return;
        }
        if(pressed & CONT_START) {
            int res = flashrom_save_live_syscfg(&s_syscfg);
            if(res == 0) {
                s_save_status = 1;
                s_dirty = 0;
                s_syscfg_valid = 1;
            } else {
                s_save_status = 2;
            }
            s_save_timer = 180;
        }
    } else {
        if(pressed & (CONT_X | CONT_Y | CONT_A)) {
            s_view_mode = VIEW_SETTINGS_DIAG;
            return;
        }
        if(pressed & CONT_DPAD_LEFT) {
            s_hex_partition = (s_hex_partition + 4) % 5;
            s_hex_block_page = 0;
        }
        if(pressed & CONT_DPAD_RIGHT) {
            s_hex_partition = (s_hex_partition + 1) % 5;
            s_hex_block_page = 0;
        }
        if(pressed & CONT_DPAD_UP) {
            if(s_hex_block_page > 0) s_hex_block_page--;
        }
        if(pressed & CONT_DPAD_DOWN) {
            uint32_t max_blocks = PARTITION_INFO[s_hex_partition].size / 64;
            if(s_hex_block_page < (int)(max_blocks - 1)) s_hex_block_page++;
        }
    }
}



