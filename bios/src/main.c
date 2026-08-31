#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/vmu.h>
#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <dc/video.h>
#include <arch/exec.h>
#include <arch/rtc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib/zlib.h>

extern const uint8_t romdisk[];

/* Match DreamDash's known-good KOS startup requirements explicitly. */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_FS_ALL |
               INIT_LIBRARY | INIT_CDROM | INIT_CONTROLLER | INIT_VMU);

#define NUM_STARS 120
#define NUM_SPIRAL_PTS 64

typedef struct {
    float x, y, z;
    float speed;
} star_t;

static star_t stars[NUM_STARS];

/* 8x8 font glyph bitmap table */
static const uint8_t FONT_8X8_W[][8] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['!'] = { 0x00, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00 },
    ['\"'] = { 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['#'] = { 0x24, 0x7E, 0x24, 0x24, 0x7E, 0x24, 0x00, 0x00 },
    ['$'] = { 0x10, 0x7C, 0x12, 0x7C, 0x48, 0x3E, 0x10, 0x00 },
    ['%'] = { 0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00 },
    ['&'] = { 0x30, 0x48, 0x30, 0x54, 0x48, 0x34, 0x00, 0x00 },
    ['\''] = { 0x10, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['('] = { 0x08, 0x10, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00 },
    [')'] = { 0x20, 0x10, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00 },
    ['*'] = { 0x00, 0x14, 0x08, 0x3E, 0x08, 0x14, 0x00, 0x00 },
    ['+'] = { 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00 },
    [','] = { 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00 },
    ['-'] = { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },
    ['/'] = { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00 },
    ['0'] = { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 },
    ['1'] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    ['2'] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00 },
    ['3'] = { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 },
    ['4'] = { 0x0C, 0x1C, 0x3C, 0x6C, 0xFE, 0x0C, 0x0C, 0x00 },
    ['5'] = { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 },
    ['6'] = { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 },
    ['7'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },
    ['8'] = { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 },
    ['9'] = { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 },
    [':'] = { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00 },
    [';'] = { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00 },
    ['<'] = { 0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00 },
    ['='] = { 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },
    ['>'] = { 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00 },
    ['?'] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00 },
    ['@'] = { 0x3C, 0x42, 0x99, 0xA5, 0x99, 0x42, 0x3C, 0x00 },
    ['A'] = { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 },
    ['B'] = { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 },
    ['C'] = { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 },
    ['D'] = { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 },
    ['E'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00 },
    ['F'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00 },
    ['G'] = { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00 },
    ['H'] = { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 },
    ['I'] = { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    ['J'] = { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00 },
    ['K'] = { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 },
    ['L'] = { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 },
    ['M'] = { 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00 },
    ['N'] = { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 },
    ['O'] = { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['P'] = { 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00 },
    ['Q'] = { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00 },
    ['R'] = { 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00 },
    ['S'] = { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 },
    ['T'] = { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },
    ['U'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['V'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 },
    ['W'] = { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },
    ['X'] = { 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00 },
    ['Y'] = { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00 },
    ['Z'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00 },
    ['['] = { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 },
    ['\\'] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00 },
    [']'] = { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 },
    ['^'] = { 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['_'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },
    ['a'] = { 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00 },
    ['b'] = { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00 },
    ['c'] = { 0x00, 0x00, 0x3C, 0x60, 0x60, 0x60, 0x3C, 0x00 },
    ['d'] = { 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00 },
    ['e'] = { 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00 },
    ['f'] = { 0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00 },
    ['g'] = { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C },
    ['h'] = { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 },
    ['i'] = { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    ['j'] = { 0x06, 0x00, 0x0E, 0x06, 0x06, 0x66, 0x3C, 0x00 },
    ['k'] = { 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00 },
    ['l'] = { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    ['m'] = { 0x00, 0x00, 0x66, 0x7F, 0x7B, 0x63, 0x63, 0x00 },
    ['n'] = { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 },
    ['o'] = { 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['p'] = { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 },
    ['q'] = { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06 },
    ['r'] = { 0x00, 0x00, 0x6C, 0x76, 0x60, 0x60, 0x60, 0x00 },
    ['s'] = { 0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00 },
    ['t'] = { 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00 },
    ['u'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00 },
    ['v'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 },
    ['w'] = { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },
    ['x'] = { 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00 },
    ['y'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C },
    ['z'] = { 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00 },
    ['|'] = { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }
};

static void draw_quad(float x, float y, float w, float h, float z, uint32_t col) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.x = x; vert.y = y; vert.z = z;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = col; vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x + w; vert.y = y;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x; vert.y = y + h;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x + w; vert.y = y + h;
    pvr_prim(&vert, sizeof(vert));
}

static void draw_text(float start_x, float start_y, float scale, const char *text, uint32_t col) {
    float cur_x = start_x;
    for (int i = 0; text[i]; i++) {
        char ch = text[i];
        if ((unsigned char)ch < sizeof(FONT_8X8_W) / sizeof(FONT_8X8_W[0])) {
            for (int r = 0; r < 8; r++) {
                uint8_t row = FONT_8X8_W[(unsigned char)ch][r];
                for (int c = 0; c < 8; c++) {
                    if (row & (0x80 >> c)) {
                        draw_quad(cur_x + c * scale, start_y + r * scale, scale, scale, 5.0f, col);
                    }
                }
            }
        }
        cur_x += 9.0f * scale;
    }
}

static void init_stars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = ((float)(rand() % 640) - 320.0f);
        stars[i].y = ((float)(rand() % 480) - 240.0f);
        stars[i].z = (float)(rand() % 400 + 50);
        stars[i].speed = (float)(rand() % 4 + 2);
    }
}

static void draw_stars(float speed_mult) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].z -= stars[i].speed * speed_mult;
        if (stars[i].z <= 10.0f) {
            stars[i].x = ((float)(rand() % 640) - 320.0f);
            stars[i].y = ((float)(rand() % 480) - 240.0f);
            stars[i].z = 400.0f;
        }

        float k = 220.0f / stars[i].z;
        float px = 320.0f + stars[i].x * k;
        float py = 240.0f + stars[i].y * k;
        float sz = (1.0f - stars[i].z / 400.0f) * 3.5f + 1.0f;
        float alpha = (1.0f - stars[i].z / 400.0f) * 0.85f;

        if (px >= 0 && px < 640 && py >= 0 && py < 480) {
            uint32_t col = PVR_PACK_COLOR(alpha, 0.8f, 0.95f, 1.0f);
            draw_quad(px, py, sz, sz, 1.0f, col);
        }
    }
}

static void draw_3d_spiral_logo(float center_x, float center_y, float progress, int frame) {
    float rot = frame * 0.04f;
    float max_r = 120.0f * progress;

    for (int i = 0; i < NUM_SPIRAL_PTS; i++) {
        float t = (float)i / NUM_SPIRAL_PTS;
        float r = t * max_r;
        float angle = t * 4.0f * 3.14159f + rot;
        
        float z_offset = sinf(angle * 2.0f + rot) * 30.0f;
        float sx = center_x + cosf(angle) * r;
        float sy = center_y + sinf(angle) * (r * 0.55f) + z_offset * 0.2f;
        float pt_size = 4.0f + t * 5.0f;

        float cr = sinf(t * 3.14f + frame * 0.05f) * 0.5f + 0.5f;
        float cg = 0.85f;
        float cb = cosf(t * 3.14f) * 0.5f + 0.5f;
        float alpha = progress * (0.4f + t * 0.55f);

        uint32_t col = PVR_PACK_COLOR(alpha, cr, cg, cb);
        draw_quad(sx - pt_size * 0.5f, sy - pt_size * 0.5f, pt_size, pt_size, 3.0f, col);
    }
}

#include "bootloader_gdrom.h"

static int check_disc_status(char *game_title, int max_len) {
    static int poll_count;
    static int cached_result;

    if (poll_count++ != 0 && (poll_count % 60) != 0) {
        if (cached_result && game_title)
            snprintf(game_title, max_len, "DREAMCAST GAME DISC");
        return cached_result;
    }

    /* The cold-boot loader already probed the TOC and ISO before KOS started.
       Consume that result passively; querying the KOS semaphore or issuing a
       raw read from the render loop can deadlock the first dashboard frame. */
    volatile gdrom_service_table_t *srv = gdrom_services();
    if (srv && srv->magic == GDROM_SERVICE_MAGIC && srv->disc_present) {
        cached_result = 1;
        if (game_title)
            snprintf(game_title, max_len, "DREAMCAST GAME DISC");
        return 1;
    }

    cached_result = 0;
    return 0;
}

/*
 * Use the established KOS GD-ROM launcher for now. The direct raw
 * 1ST_READ.BIN loading/descrambling path is not complete and can reset the
 * console after the splash. rungd.bin performs the BIOS-compatible GD-ROM
 * handoff used by DreamDash and the retail/proprietary bootloader.
 */
static void boot_inserted_disc(void) {
    volatile gdrom_service_table_t *srv = gdrom_services();

    /* The bootloader owns the direct cold-boot path.  It reads IP.BIN,
       enters its license-screen code, and then loads 1ST_READ.BIN. */
    if (srv && srv->magic == GDROM_SERVICE_MAGIC && srv->boot_game &&
        srv->disc_present && srv->data_fad) {
        srv->boot_game(srv->data_fad);
    }
}

enum {
    VIEW_MAIN_MENU = 0,
    VIEW_PLAY_DISC,
    VIEW_MEMORY_CARDS,
    VIEW_SETTINGS,
    VIEW_STATS
};

int main(int argc, char **argv) {
    /* KOS_INIT_FLAGS already initializes the GD-ROM before main().  Do not
       reinitialize it here: on some BIOS implementations a second
       cdrom_reinit() can block before the video dashboard is displayed. */
    fs_romdisk_mount("/rd", romdisk, 0);
    pvr_init_defaults();
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
    snd_init();
    init_stars();

    sfxhnd_t chime = snd_sfx_load("/rd/boot_chime.wav");
    sfxhnd_t theme = snd_sfx_load("/rd/bios_theme.wav");

    if (chime != SFXHND_INVALID) {
        snd_sfx_play(chime, 175, 128);
    }

    int frame = 0;
    int menu_sel = 0;
    int current_view = VIEW_MAIN_MENU;
    uint32_t prev_buttons = 0;

    const char *menu_items[] = {
        "PLAY DISC",
        "MEMORY CARDS",
        "SYSTEM SETTINGS",
        "HARDWARE STATS"
    };
    int num_items = 4;

    while (1) {
        frame++;

        if (frame >= 96 && (frame - 96) % 210 == 0 && theme != SFXHND_INVALID) {
            snd_sfx_play(theme, 140, 128);
        }

        /* Read Controller Inputs */
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (cont) {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if (st) {
                if (current_view == VIEW_MAIN_MENU) {
                    if ((st->buttons & CONT_DPAD_DOWN) && !(prev_buttons & CONT_DPAD_DOWN)) {
                        menu_sel = (menu_sel + 1) % num_items;
                    }
                    if ((st->buttons & CONT_DPAD_UP) && !(prev_buttons & CONT_DPAD_UP)) {
                        menu_sel = (menu_sel - 1 + num_items) % num_items;
                    }
                    if ((st->buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                        current_view = menu_sel + 1;
                    }
                } else {
                    /* Submenu: Press B to Return */
                    if ((st->buttons & CONT_B) && !(prev_buttons & CONT_B)) {
                        current_view = VIEW_MAIN_MENU;
                    }
                    /* In Play Disc: Press A to Launch */
                    if (current_view == VIEW_PLAY_DISC && (st->buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                        boot_inserted_disc();
                    }
                }
                prev_buttons = st->buttons;
            }
        }

        pvr_wait_ready();
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_TR_POLY);

        float star_speed = (frame < 120) ? 2.5f : 0.8f;
        draw_stars(star_speed);

        if (frame < 150) {
            /* Intro sequence */
            float intro_prog = (frame < 80) ? (float)frame / 80.0f : 1.0f;
            draw_3d_spiral_logo(320.0f, 210.0f, intro_prog, frame);

            if (frame > 30) {
                float title_alpha = (float)(frame - 30) / 50.0f;
                if (title_alpha > 1.0f) title_alpha = 1.0f;
                draw_text(160.0f, 340.0f, 3.0f, "CUSTOM BIOS", PVR_PACK_COLOR(title_alpha, 0.2f, 0.95f, 0.45f));
                draw_text(215.0f, 375.0f, 2.0f, "KALLISTIOS 2.0", PVR_PACK_COLOR(title_alpha * 0.7f, 0.5f, 0.8f, 1.0f));
            }
        } else {
            /* Keep the dashboard render loop free of blocking GD-ROM work.
               Boot is initiated explicitly from PLAY DISC after the menu is
               visible, so a slow/failing IP.BIN probe cannot freeze the intro
               frame and look like a video failure. */
            /* Interactive Views */
            draw_3d_spiral_logo(100.0f, 90.0f, 0.45f, frame);

            if (current_view == VIEW_MAIN_MENU) {
                draw_text(160.0f, 40.0f, 3.0f, "CUSTOM BIOS", PVR_PACK_COLOR(1.0f, 0.2f, 0.95f, 0.45f));
                draw_text(160.0f, 70.0f, 2.0f, "SEGA DREAMCAST DASHBOARD", PVR_PACK_COLOR(0.7f, 0.5f, 0.8f, 1.0f));

                for (int i = 0; i < num_items; i++) {
                    float my = 145.0f + i * 45.0f;
                    if (i == menu_sel) {
                        float glow = 0.8f + 0.2f * sinf(frame * 0.1f);
                        draw_quad(150.0f, my - 6.0f, 360.0f, 32.0f, 2.0f, PVR_PACK_COLOR(0.35f * glow, 0.1f, 0.6f, 0.9f));
                        draw_text(160.0f, my, 2.5f, ">", PVR_PACK_COLOR(1.0f, 1.0f, 0.3f, 0.1f));
                        draw_text(185.0f, my, 2.5f, menu_items[i], PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        draw_text(185.0f, my, 2.5f, menu_items[i], PVR_PACK_COLOR(0.55f, 0.6f, 0.7f, 0.8f));
                    }
                }
                draw_text(180.0f, 350.0f, 1.8f, "(A) SELECT  |  (D-PAD) NAVIGATE", PVR_PACK_COLOR(0.6f, 0.9f, 0.9f, 0.3f));
            } else if (current_view == VIEW_PLAY_DISC) {
                draw_text(160.0f, 45.0f, 3.0f, "PLAY DISC", PVR_PACK_COLOR(1.0f, 0.2f, 0.95f, 0.45f));
                
                char game_title[128];
                game_title[0] = '\0';
                int has_disc = check_disc_status(game_title, sizeof(game_title));
                
                if (has_disc) {
                    draw_quad(110.0f, 120.0f, 440.0f, 160.0f, 2.0f, PVR_PACK_COLOR(0.3f, 0.1f, 0.8f, 0.3f));
                    draw_text(130.0f, 135.0f, 2.2f, "DISC STATUS: READY", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.4f));
                    draw_text(130.0f, 165.0f, 1.8f, "TYPE: DREAMCAST GD-ROM", PVR_PACK_COLOR(0.7f, 0.7f, 0.8f, 1.0f));
                    draw_text(130.0f, 190.0f, 2.0f, game_title, PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));
                    draw_text(130.0f, 235.0f, 2.0f, "PRESS (A) TO BOOT DISC", PVR_PACK_COLOR(1.0f, 1.0f, 0.9f, 0.2f));
                } else {
                    draw_quad(130.0f, 130.0f, 400.0f, 140.0f, 2.0f, PVR_PACK_COLOR(0.25f, 0.4f, 0.4f, 0.5f));
                    draw_text(150.0f, 160.0f, 2.2f, "NO DISC INSERTED", PVR_PACK_COLOR(1.0f, 1.0f, 0.4f, 0.4f));
                    draw_text(150.0f, 200.0f, 1.8f, "INSERT A DISC OR USE KOS-INSERT", PVR_PACK_COLOR(0.8f, 0.7f, 0.8f, 0.9f));
                }
                draw_text(180.0f, 350.0f, 1.8f, "(B) RETURN TO MAIN MENU", PVR_PACK_COLOR(0.6f, 0.9f, 0.9f, 0.3f));
            } else if (current_view == VIEW_MEMORY_CARDS) {
                draw_text(160.0f, 45.0f, 3.0f, "MEMORY CARDS", PVR_PACK_COLOR(1.0f, 0.2f, 0.95f, 0.45f));
                
                const char *ports[] = { "PORT A1", "PORT A2", "PORT B1", "PORT B2" };
                for (int v = 0; v < 4; v++) {
                    float vy = 120.0f + v * 50.0f;
                    draw_quad(130.0f, vy, 400.0f, 40.0f, 2.0f, PVR_PACK_COLOR(0.2f, 0.2f, 0.4f, 0.8f));
                    
                    maple_device_t *vmu = maple_enum_type(v, MAPLE_FUNC_MEMCARD);
                    if (vmu) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s: VMU DETECTED (200 BLOCKS)", ports[v]);
                        draw_text(145.0f, vy + 10.0f, 1.8f, buf, PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.5f));
                    } else {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s: NO VMU INSERTED", ports[v]);
                        draw_text(145.0f, vy + 10.0f, 1.8f, buf, PVR_PACK_COLOR(0.6f, 0.6f, 0.7f, 0.8f));
                    }
                }
                draw_text(180.0f, 350.0f, 1.8f, "(B) RETURN TO MAIN MENU", PVR_PACK_COLOR(0.6f, 0.9f, 0.9f, 0.3f));
            } else if (current_view == VIEW_SETTINGS) {
                draw_text(160.0f, 45.0f, 3.0f, "SYSTEM SETTINGS", PVR_PACK_COLOR(1.0f, 0.2f, 0.95f, 0.45f));
                
                int cable = vid_check_cable();
                const char *cable_name = (cable == CT_VGA) ? "VGA BOX (640X480 60HZ RGB)" : 
                                         ((cable == CT_RGB) ? "RGB SCART (480I 60HZ)" : "COMPOSITE / S-VIDEO");
                
                draw_quad(130.0f, 120.0f, 400.0f, 180.0f, 2.0f, PVR_PACK_COLOR(0.25f, 0.2f, 0.5f, 0.7f));
                draw_text(150.0f, 140.0f, 2.0f, "VIDEO OUTPUT:", PVR_PACK_COLOR(1.0f, 1.0f, 0.9f, 0.3f));
                draw_text(150.0f, 165.0f, 1.8f, cable_name, PVR_PACK_COLOR(0.9f, 0.9f, 0.9f, 1.0f));
                
                draw_text(150.0f, 205.0f, 2.0f, "AUDIO MODE:", PVR_PACK_COLOR(1.0f, 1.0f, 0.9f, 0.3f));
                draw_text(150.0f, 230.0f, 1.8f, "STEREO 16-BIT 44.1KHZ", PVR_PACK_COLOR(0.9f, 0.9f, 0.9f, 1.0f));
                
                time_t now = rtc_boot_time();
                char time_buf[64];
                snprintf(time_buf, sizeof(time_buf), "RTC BOOT EPOCH: %lu", (unsigned long)now);
                draw_text(150.0f, 265.0f, 1.6f, time_buf, PVR_PACK_COLOR(0.7f, 0.6f, 0.8f, 0.9f));

                draw_text(180.0f, 350.0f, 1.8f, "(B) RETURN TO MAIN MENU", PVR_PACK_COLOR(0.6f, 0.9f, 0.9f, 0.3f));
            } else if (current_view == VIEW_STATS) {
                draw_text(160.0f, 45.0f, 3.0f, "HARDWARE STATS", PVR_PACK_COLOR(1.0f, 0.2f, 0.95f, 0.45f));
                
                draw_quad(100.0f, 110.0f, 460.0f, 200.0f, 2.0f, PVR_PACK_COLOR(0.25f, 0.1f, 0.5f, 0.8f));
                draw_text(120.0f, 130.0f, 1.8f, "CPU: SH-4 7091 @ 200 MHZ (1.4 GFLOPS)", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f));
                draw_text(120.0f, 160.0f, 1.8f, "GPU: POWERVR2 CLX2 @ 100 MHZ (3M POLY/S)", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f));
                draw_text(120.0f, 190.0f, 1.8f, "SPU: YAMAHA AICA 64-CH 32-BIT RISC", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f));
                draw_text(120.0f, 220.0f, 1.8f, "MAIN RAM: 16 MB SDRAM (800 MB/S)", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f));
                draw_text(120.0f, 250.0f, 1.8f, "VRAM: 8 MB | SOUND RAM: 2 MB", PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f));
                draw_text(120.0f, 280.0f, 1.8f, "FIRMWARE: KALLISTIOS 2.0 CUSTOM BIOS", PVR_PACK_COLOR(1.0f, 1.0f, 0.9f, 0.2f));

                draw_text(180.0f, 350.0f, 1.8f, "(B) RETURN TO MAIN MENU", PVR_PACK_COLOR(0.6f, 0.9f, 0.9f, 0.3f));
            }

            draw_text(80.0f, 430.0f, 1.5f, "CPU: SH-4 200MHZ | RAM: 16MB SDRAM | GPU: PVR2 CLX2", PVR_PACK_COLOR(0.5f, 0.4f, 0.7f, 0.9f));
        }

        pvr_list_finish();
        pvr_scene_finish();
    }

    return 0;
}
