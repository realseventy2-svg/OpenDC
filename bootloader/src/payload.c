#include "gdrom.h"

typedef volatile uint32_t vuint32_t;

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480

/* PowerVR2 Video Hardware Registers */
#define PVR_BASE            0xA05F8000UL
#define PVR_BORDER_COLOR    (*(vuint32_t*)(PVR_BASE + 0x0040))
#define PVR_FB_CFG_1        (*(vuint32_t*)(PVR_BASE + 0x0044))
#define PVR_FB_CFG_2        (*(vuint32_t*)(PVR_BASE + 0x0048))
#define PVR_RENDER_MODULO   (*(vuint32_t*)(PVR_BASE + 0x004C))
#define PVR_FB_ADDR         (*(vuint32_t*)(PVR_BASE + 0x0050))
#define PVR_FB_IL_ADDR      (*(vuint32_t*)(PVR_BASE + 0x0054))
#define PVR_FB_SIZE         (*(vuint32_t*)(PVR_BASE + 0x005C))
#define PVR_IL_CFG          (*(vuint32_t*)(PVR_BASE + 0x00D0))
#define PVR_BORDER_X        (*(vuint32_t*)(PVR_BASE + 0x00D4))
#define PVR_SCAN_CLK        (*(vuint32_t*)(PVR_BASE + 0x00D8))
#define PVR_BORDER_Y        (*(vuint32_t*)(PVR_BASE + 0x00DC))
#define PVR_VIDEO_CFG       (*(vuint32_t*)(PVR_BASE + 0x00E8))
#define PVR_BITMAP_X        (*(vuint32_t*)(PVR_BASE + 0x00EC))
#define PVR_BITMAP_Y        (*(vuint32_t*)(PVR_BASE + 0x00F0))
#define PVR_SCALER_CFG      (*(vuint32_t*)(PVR_BASE + 0x00F4))
#define PVR_SYNC_STATUS     (*(vuint32_t*)(PVR_BASE + 0x010C))

#define VRAM_BASE           0xA5000000UL

/* RGB565 Colors */
#define RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define COLOR_BLACK         RGB565(0,   0,   0)
#define COLOR_CYAN          RGB565(0,   220, 255)
#define COLOR_GREEN         RGB565(40,  255, 120)
#define COLOR_WHITE         RGB565(255, 255, 255)
#define COLOR_GOLD          RGB565(255, 200, 40)

/* Simple 8x8 font glyphs */
static const uint8_t FONT_8X8[][8] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['A'] = { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 },
    ['B'] = { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 },
    ['C'] = { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 },
    ['D'] = { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 },
    ['E'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00 },
    ['G'] = { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00 },
    ['I'] = { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },
    ['K'] = { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 },
    ['0'] = { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 },
    ['1'] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    ['2'] = { 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00 },
    ['3'] = { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 },
    ['4'] = { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 },
    ['5'] = { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 },
    ['6'] = { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 },
    ['7'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },
    ['8'] = { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 },
    ['9'] = { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 },
    ['A'] = { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 },
    ['F'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00 },
    ['D'] = { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 },
    [':'] = { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 },
    ['L'] = { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 },
    ['M'] = { 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00 },
    ['N'] = { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 },
    ['O'] = { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['R'] = { 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00 },
    ['S'] = { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 },
    ['T'] = { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },
    ['U'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['W'] = { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }
};

static void draw_rect(volatile uint16_t* fb, int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; row++) {
        if ((unsigned)row >= SCREEN_HEIGHT) continue;
        volatile uint16_t* line = fb + (row * SCREEN_WIDTH) + x;
        for (int col = 0; col < w; col++) {
            if ((unsigned)(x + col) >= SCREEN_WIDTH) continue;
            line[col] = color;
        }
    }
}

static void draw_char(volatile uint16_t* fb, int x, int y, char c, uint16_t color, int scale) {
    uint8_t uc = (uint8_t)c;
    if (uc >= sizeof(FONT_8X8)/sizeof(FONT_8X8[0])) return;
    const uint8_t* glyph = FONT_8X8[uc];
    for (int r = 0; r < 8; r++) {
        uint8_t row = glyph[r];
        for (int col = 0; col < 8; col++) {
            if (row & (0x80 >> col)) {
                draw_rect(fb, x + col * scale, y + r * scale, scale, scale, color);
            }
        }
    }
}

static void draw_string_centered(volatile uint16_t* fb, int center_x, int y, const char* str, uint16_t color, int scale) {
    int len = 0;
    const char* p = str;
    while (*p++) len++;
    int total_w = len * 8 * scale;
    int start_x = center_x - (total_w / 2);

    int cx = start_x;
    while (*str) {
        draw_char(fb, cx, y, *str++, color, scale);
        cx += 8 * scale;
    }
}

static char hex_digit(uint8_t value) {
    return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

static void draw_hex32(volatile uint16_t *fb, int x, int y, uint32_t value,
                       uint16_t color, int scale) {
    char text[9];
    char *p;
    int i;
    for(i = 0; i < 8; ++i)
        text[i] = hex_digit((uint8_t)(value >> (28 - i * 4)) & 0x0F);
    text[8] = 0;
    p = text;
    while(*p) {
        draw_char(fb, x, y, *p++, color, scale);
        x += 8 * scale;
    }
}

static void draw_hex8(volatile uint16_t *fb, int x, int y, const uint8_t *data,
                      uint16_t color, int scale) {
    int i;
    for(i = 0; i < 8; ++i) {
        draw_char(fb, x, y, hex_digit(data[i] >> 4), color, scale);
        draw_char(fb, x + 8 * scale, y, hex_digit(data[i] & 0x0F), color, scale);
        x += 24 * scale;
    }
}

static void init_pvr_video(void) {
    PVR_VIDEO_CFG     = 0x00000008;
    PVR_BORDER_COLOR  = 0x00000000;

    PVR_BORDER_X      = 0x007E0345;
    PVR_BORDER_Y      = 0x00240204;
    PVR_SCAN_CLK      = 0x020C0359;
    PVR_IL_CFG        = 0x00000100;
    PVR_BITMAP_X      = 0x000000AC;
    PVR_BITMAP_Y      = 0x00280028;
    PVR_SCALER_CFG    = 0x00000400;

    PVR_FB_ADDR       = 0x00000000;
    PVR_FB_IL_ADDR    = 0x00000000;
    PVR_FB_CFG_1      = 0x00800005;
    PVR_FB_CFG_2      = 0x00000009;
    PVR_RENDER_MODULO = 160;
    PVR_FB_SIZE       = (1 << 20) | (479 << 10) | 319;

    PVR_VIDEO_CFG     = 0x00000000;
}

/* Hardware VBlank delay. A bounded fallback is required because a video
   sync-status fault must not prevent the BIOS payload from starting. */
static void wait_vblank(void) {
    volatile uint32_t timeout = 1000000;

    while (!(PVR_SYNC_STATUS & 0x01FF) && --timeout != 0) { }

    timeout = 1000000;
    while ((PVR_SYNC_STATUS & 0x01FF) && --timeout != 0) { }
}

static void wait_seconds_exact(int seconds) {
    int total_vblanks = seconds * 60;
    for (int i = 0; i < total_vblanks; i++) {
        wait_vblank();
    }
}

/* Provided by crt0.s: a small, position-independent block of code that gets
   copied verbatim into each VBR-relative exception vector slot. See the
   comment above _vector_stub_template in crt0.s for why this is necessary:
   an unhandled exception previously executed garbage memory and
   double-faulted. */
extern const uint8_t vector_stub_template[];
extern const uint8_t vector_stub_template_end[];

/* Real, linker-allocated storage for the exception vector table. This is
   intentionally NOT a hardcoded address like 0x8C000000: this program's own
   .data/.bss (toc[], pvd[], sector[], pending_command, etc.) is placed by
   the linker starting at SDRAM base, so a hardcoded VBR guess stomped on
   live globals and corrupted the program before it ever reached IP.BIN.
   Letting the linker place this array guarantees no overlap with anything
   else. crt0.s points VBR here (see val_vbr_init in crt0.s). Needs to
   cover offsets 0x000 (used), 0x100 (general exception), 0x400 (TLB miss)
   and 0x600 (interrupt), each followed by a small stub -- 2KB is generous. */
__attribute__((aligned(1024)))
uint8_t exception_vector_table[0x800];

static void install_exception_vectors(void) {
    const uint8_t *tmpl = vector_stub_template;
    size_t len = (size_t)(vector_stub_template_end - vector_stub_template);
    uint32_t vbr_base = (uint32_t)exception_vector_table;
    const uint32_t vector_offsets[3] = {
        0x100UL, /* general exception (incl. address errors, illegal instr) */
        0x400UL, /* TLB miss */
        0x600UL  /* interrupt */
    };

    for (unsigned t = 0; t < 3; t++) {
        uint8_t *dst = (uint8_t *)(vbr_base + vector_offsets[t]);
        for (size_t i = 0; i < len; i++) {
            dst[i] = tmpl[i];
        }
    }

    /* Flush I/D caches so the CPU fetches the freshly written stub code
       rather than stale/absent cache lines. */
    *(volatile uint32_t *)0xFF00001CUL = 0x0808;
}

static const uint32_t val_stack_handoff = 0x8D000000UL;
static const uint32_t val_entry_handoff = 0x8C010000UL;

/* Called from _generic_exception_handler in crt0.s when any unhandled CPU
   exception fires. Paints the faulting PC and the EXPEVT exception code on
   screen instead of just hanging, so a fault is diagnosable without a
   hardware debugger.

   Common EXPEVT values worth knowing:
     0x000000E0  general illegal instruction
     0x0000001A  slot illegal instruction
     0x000000E4  reserved instruction (slot)
     0x000000E0  ... consult the SH-4 manual's EXPEVT table for the rest --
                     the important thing here is that you now get *a* code
                     and PC instead of a black screen or a double-fault.

   Only uses glyphs already present in FONT_8X8 (F, A, U, L, T, C, O, D, E,
   plus 0-9/A-F for the hex digits) to avoid needing to extend the font. */
void report_exception(uint32_t pc, uint32_t expevt) {
    volatile uint16_t *fb = (volatile uint16_t *)VRAM_BASE;

    draw_rect(fb, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
    draw_string_centered(fb, 320, 140, "FAULT", COLOR_GOLD, 5);

    draw_string_centered(fb, 220, 260, "AT", COLOR_WHITE, 3);
    draw_hex32(fb, 300, 260, pc, COLOR_WHITE, 3);

    draw_string_centered(fb, 220, 320, "CODE", COLOR_WHITE, 3);
    draw_hex32(fb, 340, 320, expevt, COLOR_WHITE, 3);
}

void chainload_custom_bios(void) {
    /* Publish the service ABI before the custom BIOS is started. */
    gdrom_install_services();

    /* 1. Install the open-source KOS-compatible GD-ROM syscall entry. */
    gdrom_install_syscall();

    /* 2. Copy payload from ROM offset 64KB (0xA0010000) into SDRAM */
    volatile uint32_t *src = (volatile uint32_t *)(0xA0010000UL);
    volatile uint32_t *dst = (volatile uint32_t *)(0x8C010000UL);
    uint32_t size_words = 0x1E0000 / 4;

    for (uint32_t i = 0; i < size_words; i++) {
        dst[i] = src[i];
    }

    /* 3. Invalidate instruction and operand caches (CCR register). */
    volatile uint32_t *ccr = (volatile uint32_t *)0xFF00001CUL;
    *ccr = 0x00000808;

    /* 4. Set r15 to top of SDRAM and jump into KallistiOS. */
    __asm__ volatile(
        "mov.l  %0, r15\n\t"
        "mov.l  %1, r1\n\t"
        "jmp    @r1\n\t"
        "nop\n\t"
        :
        : "m"(val_stack_handoff), "m"(val_entry_handoff)
        : "r1", "r15"
    );
}

void main(void) {
    /* 0. Install exception vector stubs before anything else runs, so a
       fault anywhere below (or later, inside IP.BIN / the syscall shim)
       hits a defined handler instead of double-faulting into whatever
       garbage was previously sitting at the VBR-relative vector slots. */
    install_exception_vectors();

    /* 1. Hardware video init */
    init_pvr_video();

    /* 2. Bring up the GD-ROM before the splash delay. KOS will initialize it
       again through the installed syscall ABI after the payload starts, but
       cold-boot detection must not depend on the later KOS handoff. */
    (void)gdrom_init();
    int toc_result = gdrom_probe_toc();
    uint32_t iso_fad = 0;
    uint8_t iso_head[8];
    int iso_result = gdrom_probe_iso(&iso_fad, iso_head);

    volatile uint16_t* fb = (volatile uint16_t*)VRAM_BASE;

    /* 3. Display pure custom splash */
    // draw_rect(fb, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
    // draw_rect(fb, 30, 30, 580, 4, COLOR_CYAN);
    // draw_rect(fb, 30, 446, 580, 4, COLOR_CYAN);
    // draw_rect(fb, 30, 30, 4, 420, COLOR_CYAN);
    // draw_rect(fb, 606, 30, 4, 420, COLOR_CYAN);

    draw_string_centered(fb, 320, 110, "SEGA DREAMCAST", COLOR_GREEN, 3);
    draw_string_centered(fb, 320, 200, "CUSTOM BOOT ROM", COLOR_WHITE, 4);
    //draw_string_centered(fb, 320, 300, "NO SEGA SWIRL", COLOR_GOLD, 3);
    draw_string_centered(fb, 320, 380,
                         toc_result == GDROM_OK ? "TOC OK" : "TOC ERROR",
                         toc_result == GDROM_OK ? COLOR_GREEN : COLOR_GOLD, 2);
    draw_string_centered(fb, 320, 410,
                         iso_result == GDROM_OK ? "ISO OK" : "ISO ERROR",
                         iso_result == GDROM_OK ? COLOR_GREEN : COLOR_GOLD, 2);
    draw_string_centered(fb, 320, 440, "FAD", COLOR_WHITE, 1);
    draw_hex32(fb, 340, 440, iso_fad, COLOR_WHITE, 1);
    draw_hex8(fb, 220, 458, iso_head, COLOR_WHITE, 1);

    /* 4. Hardware-Locked Exact 8.000 Seconds */
    wait_seconds_exact(8);

    /* 5. Match the normal Dreamcast cold-boot order: once a valid bootable
       disc has been identified, enter its IP.BIN before starting the KOS
       dashboard.  Raw GD-ROM access is reliable here because KOS has not yet
       installed its background CD/vblank machinery. */
    if(iso_result == GDROM_OK) {
        (void)gdrom_boot_game(iso_fad);
    }

    /* 6. Fall back to the Custom BIOS Dashboard when no bootable disc is
       present or when the direct IP.BIN loader returns an error. */
    chainload_custom_bios();
}