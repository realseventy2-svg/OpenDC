#include "gdrom.h"
#include "retail_vectors.h"
#include "video.h"
#include "screen.h"
#include "sound.h"
#include "boot.h"

/* Provided by crt0.s: exception and interrupt vector stubs */
extern const uint8_t vector_stub_template[];
extern const uint8_t vector_stub_template_end[];
extern const uint8_t interrupt_stub_template[];
extern const uint8_t interrupt_stub_template_end[];

static void install_exception_vectors(void) {
    /* Copy authentic retail exception vectors (0x0000..0x0800, 2048 bytes).
       This includes the real general exception (0x100), TLB miss (0x400),
       and interrupt (0x600) dispatchers that Windows CE and Katana expect. */
    uint32_t *dst_c = (uint32_t *)0x8C000000UL;
    uint32_t *dst_u = (uint32_t *)0xAC000000UL;
    const uint32_t *src = (const uint32_t *)dc_retail_vectors;
    for(size_t i = 0; i < (sizeof(dc_retail_vectors) / sizeof(uint32_t)); i++) {
        dst_c[i] = src[i];
        dst_u[i] = src[i];
    }

    /* Flush I/D caches */
    *(volatile uint32_t *)0xFF00001CUL = 0x0808;
}

static const uint32_t val_stack_handoff = 0x8D000000UL;
static const uint32_t val_entry_handoff = 0x8C010000UL;

/* Called from _generic_exception_handler in crt0.s when any unhandled CPU exception fires */
void report_exception(uint32_t pc, uint32_t expevt) {
    screen_show_fault(pc, expevt);
}

void chainload_custom_bios(void) {
    /* Stop bootloader sound engine before handoff */
    sound_stop();

    /* Clean VRAM and reset PVR state before BIOS handoff */
    video_clean_handoff();

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
        : "r1"
    );
}

void main(void) {
    /* 0. Install exception vector stubs before anything else runs */
    install_exception_vectors();

    /* 1. Initialize hardware video output */
    video_init();

    /* 2. Initialize modular boot theme and render splash screen */
    screen_init(&BOOT_THEME_DEFAULT);
    const boot_theme_t *theme = screen_get_theme();
    if (theme) {
        boot_set_sega_license_enabled(theme->sega_license_enabled);
        if (theme->music_enabled) {
            sound_init();
        }
    }
    screen_draw_splash();

    /* 3. Bring up the GD-ROM drive and probe disc */
    (void)gdrom_init();
    int toc_result = gdrom_probe_toc();
    uint32_t iso_fad = 0;
    uint8_t iso_head[8];
    int iso_result = gdrom_probe_iso(&iso_fad, iso_head);

    /* 4. Update modular UI with disc status and diagnostics */
    screen_draw_disc_status(toc_result == GDROM_OK, iso_result == GDROM_OK, iso_fad, iso_head);

    /* 5. 60 FPS Animated 3D Spinning Cube Splash with Ambient MIDI Music */
    int frames = screen_get_boot_duration_frames();
    if (frames > 0) {
        screen_animate_splash(frames);
    }

    /* 6. If a bootable disc is detected, stop sound and start the game */
    if (iso_result == GDROM_OK) {
        sound_stop();
        (void)gdrom_boot_game(iso_fad);
    }

    /* 7. Fall back to Custom BIOS Dashboard if no bootable disc */
    chainload_custom_bios();
}