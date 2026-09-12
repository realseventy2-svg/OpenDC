#include "screen_flashrom.h"
#include "renderer.h"
#include "flashrom_service.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>
#include <stdio.h>
#include <string.h>

static bios_syscfg_t s_syscfg;
static int s_dirty = 0;
static int s_save_status = 0;
static int s_save_timer = 0;

static const char *LANG_NAMES[6] = {
    "Japanese", "English", "German", "French", "Spanish", "Italian"
};

typedef enum {
    FLASH_VIEW_DIAGNOSTICS = 0,
    FLASH_VIEW_HEX_INSPECTOR = 1
} flash_ui_view_t;

static flash_ui_view_t s_current_view = FLASH_VIEW_DIAGNOSTICS;
static int s_hex_partition = 2; /* Default to PT2: User / Sysconfig */
static int s_hex_block_page = 0;

void screen_flashrom_init(void) {
    s_dirty = 0;
    s_save_status = 0;
    s_save_timer = 0;
    s_current_view = FLASH_VIEW_DIAGNOSTICS;
    s_hex_partition = 2;
    s_hex_block_page = 0;

    flashrom_service_get_syscfg(&s_syscfg);
}

static void render_settings_diagnostics(void) {
    int start_x = MARGIN_X;
    int y = 24;

    draw_bfont(start_x, y, COLOR_WHITE, "FLASHROM CONFIGURATION");
    y += 28;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 12;

    uint32_t phys_sc_addr = 0;
    uint16_t phys_crc = 0, calc_phys_crc = 0;
    int phys_has_magic = 0;
    flashrom_service_find_syscfg_slot(&phys_sc_addr, &phys_crc, &calc_phys_crc, &phys_has_magic);

    /* Section 1: User System Settings */
    draw_bfont(start_x, y, COLOR_WHITE, "SYSTEM PREFERENCES:");
    y += 22;

    const char *lang_str = (s_syscfg.language >= 0 && s_syscfg.language <= 5) ?
                            LANG_NAMES[s_syscfg.language] : "English";

    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Language:         %-10s [ (X) Cycle ]", lang_str);
    y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Audio DAC:        %-10s [ (Y) Toggle ]",
        s_syscfg.audio ? "Stereo" : "Mono");
    y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Disc Auto-Start:  %-10s [ (A) Toggle ]",
        s_syscfg.autostart ? "Enabled" : "Disabled");
    y += 20;

    /* Section 2: Non-Volatile Memory Status */
    draw_bfont(start_x, y, COLOR_WHITE, "FLASHROM HARDWARE STATUS:");
    y += 22;

    draw_sysfont(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Flash Memory:     128 KB NOR Flash @ Area 0 (0xA0200000)");
    y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Partition 2:      User Block 0x05 @ 0x%05X [CRC: 0x%04X]",
        (unsigned int)(phys_sc_addr - FLASHROM_BASE_ADDR), phys_crc);
    y += 14;
    draw_sysfont_fmt(start_x + 16, y,
        (phys_has_magic && phys_crc == calc_phys_crc) ? COLOR_GREEN : COLOR_GOLD,
        "- Hardware Link:    %s",
        phys_has_magic ? "[ CONNECTED / VALID ]" : "[ UNINITIALIZED ]");
    y += 20;

    /* Section 3: Commit / Save State */
    draw_bfont(start_x, y, COLOR_WHITE, "NVRAM STATE:");
    y += 22;
    if(s_save_status == 1 && s_save_timer > 0) {
        draw_sysfont(start_x + 16, y, COLOR_GREEN,
            "- Status:           [ SAVED / COMMITTED TO NVRAM ]");
        s_save_timer--;
    } else if(s_save_status == 2 && s_save_timer > 0) {
        draw_sysfont(start_x + 16, y, COLOR_RED,
            "- Status:           [ ERROR / WRITE FAILED ]");
        s_save_timer--;
    } else if(s_dirty) {
        draw_sysfont(start_x + 16, y, COLOR_GOLD,
            "- Status:           [ MODIFIED / Press (START) to Commit ]");
    } else {
        draw_sysfont(start_x + 16, y, COLOR_LIGHT_GRAY,
            "- Status:           [ SYNCHRONIZED / READY ]");
    }
    y += 20;

    /* Section 4: Topology */
    draw_bfont(start_x, y, COLOR_WHITE, "PARTITION TOPOLOGY:");
    y += 22;
    draw_sysfont(start_x + 16, y, COLOR_LIGHT_GRAY,
        "PT0: Factory (8KB)    PT1: Reserved (8KB)   PT2: User/ISP (16KB)");
    y += 14;
    draw_sysfont(start_x + 16, y, COLOR_LIGHT_GRAY,
        "PT3: Game Save (32KB) PT4: Block Alloc 2 (64KB)");

    /* Footer Controls */
    draw_sysfont(start_x, 430, COLOR_LIGHT_GRAY,
        "(X) Language   (Y) Audio Mode   (A) Auto-Start   (START) Save NVRAM");
    draw_sysfont(start_x, 446, COLOR_LIGHT_GRAY,
        "(D-PAD) Hex Inspector   (B) Return to Bootmenu");
}

static void render_raw_hex_inspector(void) {
    int start_x = MARGIN_X;
    int y = 24;

    draw_bfont(start_x, y, COLOR_WHITE, "FLASHROM MEMORY INSPECTOR");
    y += 28;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 12;

    const flash_partition_entry_t *pt = flashrom_service_get_partition(s_hex_partition);
    uint32_t pt_offset = pt ? pt->offset : 0;
    uint32_t current_addr = FLASHROM_BASE_ADDR + pt_offset + (s_hex_block_page * 64);

    draw_sysfont_fmt(start_x, y, COLOR_WHITE,
        "PARTITION [%d/5]: %s", s_hex_partition + 1, pt ? pt->name : "Unknown");
    y += 14;
    draw_sysfont_fmt(start_x, y, COLOR_LIGHT_GRAY,
        "Address: 0x%08X  |  Offset: 0x%05X  |  Block: %d/64",
        (unsigned int)current_addr, (unsigned int)(pt_offset + (s_hex_block_page * 64)), s_hex_block_page);
    y += 16;

    /* Hex Dump Box */
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 140, 0x0842);
    y += 6;

    volatile const uint8_t *mem_ptr = (volatile const uint8_t *)current_addr;

    draw_sysfont(start_x + 8, y, COLOR_WHITE,
        "OFFSET   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII");
    y += 14;
    draw_rect(start_x + 8, y, SCREEN_W - (MARGIN_X * 2) - 16, 1, COLOR_DARK_GRAY);
    y += 6;

    for(int row = 0; row < 4; row++) {
        uint32_t row_off = row * 16;
        char hex1[32] = {0};
        char hex2[32] = {0};
        char ascii[17] = {0};

        for(int b = 0; b < 8; b++) {
            sprintf(hex1 + (b * 3), "%02X ", mem_ptr[row_off + b]);
            uint8_t byte = mem_ptr[row_off + b];
            ascii[b] = (byte >= 32 && byte <= 126) ? (char)byte : '.';
        }
        for(int b = 0; b < 8; b++) {
            sprintf(hex2 + (b * 3), "%02X ", mem_ptr[row_off + 8 + b]);
            uint8_t byte = mem_ptr[row_off + 8 + b];
            ascii[8 + b] = (byte >= 32 && byte <= 126) ? (char)byte : '.';
        }
        ascii[16] = '\0';

        draw_sysfont_fmt(start_x + 8, y, COLOR_LIGHT_GRAY,
            "+0x%02X    %s %s  %s", (unsigned int)row_off, hex1, hex2, ascii);
        y += 14;
    }

    y += 16;
    draw_bfont(start_x, y, COLOR_WHITE, "BLOCK METRICS & CRC:");
    y += 24;

    uint16_t block_id = (uint16_t)mem_ptr[0] | ((uint16_t)mem_ptr[1] << 8);
    uint16_t block_crc = (uint16_t)mem_ptr[62] | ((uint16_t)mem_ptr[63] << 8);
    uint16_t calc_crc = flashrom_service_calc_crc((const uint8_t *)mem_ptr);

    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
        "- Header ID: 0x%04X %s", block_id,
        (block_id == 0x0005) ? "(Sysconfig)" : ((block_id == 0xFFFF) ? "(Empty/Erased)" : "(User Data)"));
    y += 14;
    draw_sysfont_fmt(start_x + 16, y, (block_crc == calc_crc) ? COLOR_GREEN : COLOR_GOLD,
        "- CRC-16:    0x%04X (Computed: 0x%04X) %s",
        block_crc, calc_crc, (block_crc == calc_crc) ? "[ VALID ]" : "[ MISMATCH / UNCOMMITTED ]");

    /* Controls footer */
    draw_bfont_centered(SCREEN_W / 2, 440, COLOR_LIGHT_GRAY,
        "(L/R) Partition   (UP/DN) Block   (B) Back");
}

void screen_flashrom_render(void) {
    if(s_current_view == FLASH_VIEW_DIAGNOSTICS) {
        render_settings_diagnostics();
    } else {
        render_raw_hex_inspector();
    }
}

bios_screen_t screen_flashrom_handle_input(uint32_t pressed) {
    if(s_current_view == FLASH_VIEW_DIAGNOSTICS) {
        if(pressed & CONT_X) {
            s_syscfg.language = (s_syscfg.language + 1) % 6;
            s_dirty = 1;
            audio_play_click();
        }
        if(pressed & CONT_Y) {
            s_syscfg.audio = !s_syscfg.audio;
            s_dirty = 1;
            audio_play_click();
        }
        if(pressed & CONT_A) {
            s_syscfg.autostart = !s_syscfg.autostart;
            s_dirty = 1;
            audio_play_click();
        }
        if(pressed & CONT_START) {
            int res = flashrom_service_set_syscfg(&s_syscfg);
            s_save_status = (res == 0) ? 1 : 2;
            s_save_timer = 90;
            if(res == 0) s_dirty = 0;
            audio_play_confirm();
        }
        if(pressed & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) {
            s_current_view = FLASH_VIEW_HEX_INSPECTOR;
            audio_play_click();
        }
        if(pressed & CONT_B) {
            audio_play_click();
            return SCREEN_MAIN_MENU;
        }
    } else {
        if(pressed & CONT_DPAD_LEFT) {
            s_hex_partition = (s_hex_partition - 1 + 5) % 5;
            s_hex_block_page = 0;
            audio_play_click();
        }
        if(pressed & CONT_DPAD_RIGHT) {
            s_hex_partition = (s_hex_partition + 1) % 5;
            s_hex_block_page = 0;
            audio_play_click();
        }
        if(pressed & CONT_DPAD_UP) {
            if(s_hex_block_page > 0) s_hex_block_page--;
            audio_play_click();
        }
        if(pressed & CONT_DPAD_DOWN) {
            const flash_partition_entry_t *pt = flashrom_service_get_partition(s_hex_partition);
            int max_pages = pt ? (pt->size / 64) - 1 : 0;
            if(s_hex_block_page < max_pages) s_hex_block_page++;
            audio_play_click();
        }
        if(pressed & (CONT_B | CONT_START)) {
            s_current_view = FLASH_VIEW_DIAGNOSTICS;
            audio_play_click();
        }
    }

    return SCREEN_FLASHROM;
}
