#include "syscalls.h"
#include "gdrom.h"
#include "wince.h"
#include <stddef.h>

typedef struct {
    int32_t status;
    int32_t disc_type;
} kos_drive_status_t;

typedef struct {
    int32_t err1;
    int32_t err2;
    size_t size;
    int32_t ata;
} kos_command_status_t;

typedef struct {
    uint32_t start_sec;
    size_t num_sec;
    void *buffer;
    uint32_t is_test;
} kos_read_params_t;

typedef struct {
    int32_t area;
    kos_toc_t *buffer;
} kos_toc_params_t;

typedef struct {
    uint32_t rw;
    int32_t sector_part;
    int32_t track_type;
    int32_t sector_size;
} kos_sector_mode_t;

typedef struct {
    void *addr;
    size_t size;
} kos_transfer_params_t;

enum {
    KOS_FUNC_SEND_COMMAND  = 0,
    KOS_FUNC_CHECK_COMMAND = 1,
    KOS_FUNC_EXEC_SERVER   = 2,
    KOS_FUNC_INIT          = 3,
    KOS_FUNC_DRIVE_STATUS  = 4,
    KOS_FUNC_DMA_CALLBACK  = 5,
    KOS_FUNC_DMA_TRANSFER  = 6,
    KOS_FUNC_DMA_CHECK     = 7,
    KOS_FUNC_ABORT_COMMAND = 8,
    KOS_FUNC_RESET         = 9,
    KOS_FUNC_SECTOR_MODE   = 10,
    KOS_FUNC_PIO_CALLBACK  = 11,
    KOS_FUNC_PIO_TRANSFER  = 12,
    KOS_FUNC_PIO_CHECK     = 13,
    KOS_CMD_PIOREAD        = 16,
    KOS_CMD_DMAREAD        = 17,
    KOS_CMD_GETTOC         = 18,
    KOS_CMD_GETTOC2        = 19,
    KOS_CMD_INIT           = 24,
    KOS_CMD_NOP            = 29
};

enum {
    KOS_STATUS_BUSY    = 0,
    KOS_STATUS_STANDBY = 2,
    KOS_STATUS_NO_DISC = 7,
    KOS_STATUS_ERROR   = 9,
    KOS_DISC_GDROM     = 0x80,
    KOS_CMD_NOT_FOUND  = 0,
    KOS_CMD_PROCESSING = 1,
    KOS_CMD_COMPLETED  = 2,
    KOS_CMD_FAILED     = -1
};

typedef struct {
    int32_t handle;
    uint32_t command;
    uint32_t params[4];
    int32_t result;
    size_t transferred;
    int active;
    int status; /* GDC_OK = 0, GDC_BUSY = 1, GDC_COMPLETE = 2, GDC_CONTINUE = 3, GDC_ERR = -1 */
} kos_pending_command_t;

static kos_pending_command_t pending_command;
static int32_t next_handle = 1;
static kos_sector_mode_t sector_mode = { 1, 0x1000, 1, 2048 };

typedef void (*kos_callback_t)(void *param);
static kos_callback_t dma_callback = NULL;
static void *dma_callback_param = NULL;
static kos_callback_t pio_callback = NULL;
static void *pio_callback_param = NULL;

static void *kos_buffer_address(void *buffer) {
    uintptr_t address = (uintptr_t)buffer;
    if(address < 0x20000000UL)
        address |= 0x8C000000UL;
    return (void *)address;
}

static void clear_command_status(kos_command_status_t *status) {
    if(status) {
        status->err1 = 0;
        status->err2 = 0;
        status->size = 0;
        status->ata = 0;
    }
}

static int execute_pending_command(void) {
    int result = GDROM_OK;
    pending_command.transferred = 0;

    switch(pending_command.command) {
        case 24: /* GDCC_INIT / KOS_CMD_INIT */
            result = gdrom_init();
            if(result == GDROM_OK) {
                result = gdrom_prepare_disk();
            }
            break;

        case 18: /* GDCC_GETTOC */
        case 19: /* GDCC_GETTOC2 */ {
            uint32_t area = pending_command.params[0];
            void *dst_buf = (void *)(uintptr_t)pending_command.params[1];
            if(!dst_buf) {
                result = GDROM_BAD_ARG;
                break;
            }
            (void)gdrom_prepare_disk();
            int is_gdi = (gdrom_get_cached_disc_type() == 0x80);
            uint8_t ata_session = is_gdi ? ((area == 0) ? 1 : 0) : 0;
            result = gdrom_read_toc(kos_buffer_address(dst_buf), ata_session);
            if(result != GDROM_OK && is_gdi && area == 0)
                result = gdrom_read_toc(kos_buffer_address(dst_buf), 0);
            if(result == GDROM_OK)
                pending_command.transferred = sizeof(kos_toc_t);
            break;
        }

        case 16: /* GDCC_PIOREAD */
        case 17: /* GDCC_DMAREAD */
        case 28: /* GDCC_DMA_READ_REQ */
        case 48: /* GDCC_MULTI_DMAREAD */
        case 49: /* GDCC_MULTI_PIOREAD */ {
            uint32_t fad = pending_command.params[0] & 0x00FFFFFF;
            uint32_t num_sec = pending_command.params[1];
            void *dst_buf = (void *)(uintptr_t)pending_command.params[2];

            if(fad < 150U && fad > 0)
                fad += 150U;
            if(fad == 0)
                fad = 45150U;

            if(num_sec == 0 || num_sec > 0xFFFFU)
                num_sec = 1;

            if(dst_buf) {
                result = gdrom_read_fad(kos_buffer_address(dst_buf), fad, (uint16_t)num_sec);
            } else {
                result = GDROM_OK;
            }
            if(result == GDROM_OK)
                pending_command.transferred = num_sec * 2048U;
            break;
        }

        case 30: { /* GDCC_REQ_MODE */
            uint32_t *dest = (uint32_t *)(uintptr_t)pending_command.params[0];
            if(dest) {
                uint32_t *d = (uint32_t *)kos_buffer_address(dest);
                d[0] = 0;      /* speed (standard / default) */
                d[1] = 0x0000; /* standby */
                d[2] = 0;      /* read_flags */
                d[3] = 0;      /* read_retry */
            }
            pending_command.transferred = 10;
            result = GDROM_OK;
            break;
        }

        case 31: { /* GDCC_SET_MODE */
            pending_command.transferred = 10;
            result = GDROM_OK;
            break;
        }

        case 36: { /* GDCC_REQ_STAT */
            uint32_t *dst0 = (uint32_t *)(uintptr_t)pending_command.params[0];
            uint32_t *dst1 = (uint32_t *)(uintptr_t)pending_command.params[1];
            uint32_t *dst2 = (uint32_t *)(uintptr_t)pending_command.params[2];
            uint32_t *dst3 = (uint32_t *)(uintptr_t)pending_command.params[3];

            if(dst0) *(uint32_t *)kos_buffer_address(dst0) = 2;     /* GD_STANDBY */
            if(dst1) *(uint32_t *)kos_buffer_address(dst1) = 3;     /* Track 3 */
            if(dst2) *(uint32_t *)kos_buffer_address(dst2) = 45150; /* FAD */
            if(dst3) *(uint32_t *)kos_buffer_address(dst3) = 1;     /* Index */

            pending_command.transferred = 16;
            result = GDROM_OK;
            break;
        }

        case 50: { /* GDCC_GET_VERSION */
            char *dest = (char *)(uintptr_t)pending_command.params[0];
            if(dest) {
                char *d = (char *)kos_buffer_address(dest);
                const char ver[] = "GDC Version 1.10 1999-03-31\x02";
                for(int i = 0; i < 28; i++) {
                    d[i] = ver[i];
                }
            }
            pending_command.transferred = 28;
            result = GDROM_OK;
            break;
        }

        case 2:  /* CD_CMD_CHECK_LICENSE */
        case 25: /* CD_CMD_DMA_ABORT */
        case 27: /* CD_CMD_SEEK */
        case 29: /* CD_CMD_NOP */
        case 32: /* CD_CMD_SCAN_CD */
        case 33: /* CD_CMD_STOP */
        case 34: /* CD_CMD_GETSCD */
        case 35: /* CD_CMD_REQ_SES */
        default:
            result = GDROM_OK;
            break;
    }

    pending_command.result = result;
    pending_command.status = (result == GDROM_OK) ? 2 /* GDC_COMPLETE */ : -1 /* GDC_ERR */;

    /* Signal completion to hardware and Katana callbacks */
    *(volatile uint32_t *)0xA05F6900UL |= (1UL << 14); /* GD-ROM DMA complete */
    *(volatile uint32_t *)0xA05F6904UL |= (1UL << 0);  /* GD-ROM status */

    if(dma_callback) {
        kos_callback_t cb = dma_callback;
        void *param = dma_callback_param;
        dma_callback = NULL;
        cb(param);
    }
    if(pio_callback) {
        kos_callback_t cb = pio_callback;
        void *param = pio_callback_param;
        pio_callback = NULL;
        cb(param);
    }

    return result;
}

static int gdrom_syscall_dispatch(uint32_t arg0, uint32_t arg1,
                                  uint32_t superfunc, uint32_t func) {
    /* Handle MISC superfunction */
    if((int32_t)superfunc == -1 || (int32_t)func == -1) {
        if(func == 0 || arg0 == 0) {
            /* MISC_INIT */
            return 0;
        } else if(func == 1 || arg0 == 1) {
            /* MISC_SETVECTOR */
            return 0;
        }
        return 0;
    }

    switch(func) {
        case KOS_FUNC_INIT:
        case KOS_FUNC_RESET:
            return gdrom_init();

        case KOS_FUNC_DRIVE_STATUS: {
            if(!arg0) return -1;
            kos_drive_status_t *status = (kos_drive_status_t *)kos_buffer_address((void *)arg0);
            status->status = KOS_STATUS_STANDBY;
            status->disc_type = gdrom_get_cached_disc_type();
            return 0;
        }

        case KOS_FUNC_SEND_COMMAND: {
            if(arg0 == 0 || arg0 >= 0x40)
                return 0;
            if(pending_command.status == 1 /* BUSY */) {
                return 0;
            }
            pending_command.handle = next_handle++;
            if(next_handle <= 0) next_handle = 1;
            pending_command.command = arg0;
            if(arg1) {
                uint32_t *p = (uint32_t *)kos_buffer_address((void *)arg1);
                pending_command.params[0] = p[0];
                pending_command.params[1] = p[1];
                pending_command.params[2] = p[2];
                pending_command.params[3] = p[3];
            } else {
                pending_command.params[0] = 0;
                pending_command.params[1] = 0;
                pending_command.params[2] = 0;
                pending_command.params[3] = 0;
            }
            pending_command.result = 0;
            pending_command.transferred = 0;
            pending_command.active = 1;
            pending_command.status = 1; /* GDC_BUSY */
            return pending_command.handle;
        }

        case KOS_FUNC_EXEC_SERVER:
            if(pending_command.active && pending_command.status == 1 /* BUSY */)
                execute_pending_command();
            return 0;

        case KOS_FUNC_CHECK_COMMAND: {
            if(arg1) {
                uint32_t *status = (uint32_t *)kos_buffer_address((void *)arg1);
                status[0] = (pending_command.result != GDROM_OK) ? 2 : 0; /* Error code (0 = NOERR) */
                status[1] = 0;                                           /* Sub-error */
                status[2] = pending_command.transferred;                 /* Size */
                status[3] = 0;                                           /* Wait state */
            }
            if(!pending_command.active || (int32_t)arg0 != pending_command.handle)
                return 0; /* GDC_OK (no request active) */

            if(pending_command.status == 1 /* BUSY */)
                execute_pending_command();

            int ret_status = pending_command.status;
            if(pending_command.status == 2 /* COMPLETE */) {
                pending_command.status = 0;
                pending_command.active = 0;
            }
            return ret_status;
        }

        case KOS_FUNC_ABORT_COMMAND:
            if(pending_command.active && (int32_t)arg0 == pending_command.handle) {
                pending_command.active = 0;
                pending_command.status = 0;
            }
            return 0;

        case KOS_FUNC_DMA_CALLBACK:
            dma_callback = (kos_callback_t)arg0;
            dma_callback_param = (void *)arg1;
            return 0;

        case KOS_FUNC_PIO_CALLBACK:
            pio_callback = (kos_callback_t)arg0;
            pio_callback_param = (void *)arg1;
            return 0;

        case KOS_FUNC_DMA_TRANSFER:
        case KOS_FUNC_PIO_TRANSFER: {
            if(!pending_command.active || (int32_t)arg0 != pending_command.handle || !arg1)
                return -1;
            kos_transfer_params_t *transfer =
                (kos_transfer_params_t *)kos_buffer_address((void *)arg1);
            if(transfer->addr && pending_command.params[1] > 0) {
                uint32_t fad = pending_command.params[0] & 0x00FFFFFF;
                if(fad < 150U && fad > 0) fad += 150U;
                if(fad == 0) {
                    fad = (gdrom_get_cached_disc_type() == 0x80) ? 45150U : gdrom_get_cached_data_fad();
                }
                uint32_t num = pending_command.params[1];
                if(num > 0xFFFFU) num = 1;
                gdrom_read_fad(kos_buffer_address(transfer->addr), fad, (uint16_t)num);
                pending_command.transferred = num * 2048U;
            }
            pending_command.status = 2; /* GDC_COMPLETE */
            *(volatile uint32_t *)0xA05F6900UL |= (1UL << 14); /* GD-ROM DMA complete */
            if(dma_callback) {
                kos_callback_t cb = dma_callback;
                void *param = dma_callback_param;
                dma_callback = NULL;
                cb(param);
            }
            if(pio_callback) {
                kos_callback_t cb = pio_callback;
                void *param = pio_callback_param;
                pio_callback = NULL;
                cb(param);
            }
            return 0;
        }

        case KOS_FUNC_SECTOR_MODE: {
            if(!arg0) return -1;
            kos_sector_mode_t *sm = (kos_sector_mode_t *)kos_buffer_address((void *)arg0);
            if(sm->rw == 0)
                sector_mode = *sm;
            else
                *sm = sector_mode;
            return 0;
        }

        case KOS_FUNC_DMA_CHECK:
        case KOS_FUNC_PIO_CHECK: {
            if(!pending_command.active || (int32_t)arg0 != pending_command.handle)
                return -1;
            if(pending_command.status == 1 /* BUSY */)
                execute_pending_command();
            if(arg1) {
                size_t *sz = (size_t *)kos_buffer_address((void *)arg1);
                *sz = pending_command.transferred;
            }
            return 0;
        }

        default:
            return 0;
    }
}

static int kos_sysinfo_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t func) {
    (void)arg2;
    if(func == 0) {
        /* SYSINFO_INIT: returns 0 on success */
        return 0;
    }
    if(func == 2) {
        /* SYSINFO_ICON: arg0 = icon num (0-9), arg1 = destination buffer (704 bytes) */
        if(arg0 > 9) return -1;
        if(arg1) {
            uint8_t *dst = (uint8_t *)kos_buffer_address((void *)arg1);
            for(int i = 0; i < 704; i++) dst[i] = 0;
        }
        return 704;
    }
    /* SYSINFO_ID (3): return pointer to 0x8C000068 */
    return (int)0x8C000068UL;
}

static uint32_t kos_biofont_dispatch(uint32_t ch, uint32_t arg0,
                                     uint32_t arg1, uint32_t arg2) {
    (void)arg0; (void)arg1; (void)arg2;
    if(ch == 0) {
        return 0xA0100020UL;
    }
    if(ch >= 32 && ch <= 126) {
        return 0xA0100020UL + ((ch - 32) * 36U);
    }
    if(ch >= 0xA0 && ch <= 0xDF) {
        /* Half-width katakana */
        return 0xA0100020UL + ((ch - 0xA0 + 96) * 36U);
    }
    if(ch >= 0x0100) {
        /* Full-width Japanese character (Shift-JIS) */
        uint32_t c1 = (ch >> 8) & 0xFF;
        uint32_t c2 = ch & 0xFF;
        if(c1 >= 0x81 && c1 <= 0x9F) c1 -= 0x81;
        else if(c1 >= 0xE0 && c1 <= 0xEA) c1 -= 0xC1;
        if(c2 >= 0x40 && c2 <= 0x7E) c2 -= 0x40;
        else if(c2 >= 0x80 && c2 <= 0xFC) c2 -= 0x41;
        uint32_t jis_idx = (c1 * 188) + c2;
        return 0xA0100000UL + 0x2000 + (jis_idx * 72U);
    }
    return 0xA0100020UL;
}

#define FLASH_RAM_BASE 0x8C004000UL

static const uint8_t s_syscfg_default[64] = {
    0x05, 0x00,             /* block_id = 5 (LE) */
    0x00, 0x00, 0x00, 0x00, /* date = 0 */
    0x00,                   /* unk1 */
    0x01,                   /* lang = 1 (English) */
    0x00,                   /* mono = 0 (Stereo) */
    0x00,                   /* autostart = 0 (Enabled) */
    0x00, 0x00, 0x00, 0x00, /* unk2 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x40, 0x03              /* CRC16 = 0x0340 */
};

static const char *get_disc_region_props(void) {
    const char *cc = (const char *)0x8C008030UL;
    int has_j = 0, has_u = 0, has_e = 0;
    for(int i = 0; i < 8; i++) {
        if(cc[i] == 'J') has_j = 1;
        if(cc[i] == 'U') has_u = 1;
        if(cc[i] == 'E') has_e = 1;
    }
    if(has_j && !has_u && !has_e) {
        return "00010"; /* Japan NTSC */
    }
    if(has_e && !has_u && !has_j) {
        return "00220"; /* Europe PAL */
    }
    return "00110";     /* USA / World NTSC */
}

static void jedec_flash_write_byte(uint32_t offset, uint8_t data) {
    volatile uint8_t *p_flash = (volatile uint8_t *)0xA0200000UL;
    /* Standard Macronix/AMD JEDEC 8-bit NOR Flash Unlock & Program Sequence */
    p_flash[0x5555] = 0xAA;
    p_flash[0x2AAA] = 0x55;
    p_flash[0x5555] = 0xA0;
    p_flash[offset] = data;
    /* Direct write fallback */
    p_flash[offset] = data;
}

static void jedec_flash_erase_sector(uint32_t offset) {
    volatile uint8_t *p_flash = (volatile uint8_t *)0xA0200000UL;
    /* Standard Macronix/AMD JEDEC Sector Erase Sequence */
    p_flash[0x5555] = 0xAA;
    p_flash[0x2AAA] = 0x55;
    p_flash[0x5555] = 0x80;
    p_flash[0x5555] = 0xAA;
    p_flash[0x2AAA] = 0x55;
    p_flash[offset] = 0x30;
}

static void init_flashrom_if_needed(void) {
    volatile uint8_t *p2_ram = (volatile uint8_t *)FLASH_RAM_BASE;

    /* 1. Check if Partition 2 is already initialized in SDRAM (survives soft reset) */
    if(p2_ram[0] == 'K' && p2_ram[1] == 'A' &&
       p2_ram[2] == 'T' && p2_ram[3] == 'A') {
        return;
    }

    /* 2. Check if physical FlashROM at 0xA021C000 has valid KATANA header (from Flycast dc_flash.bin) */
    volatile const uint8_t *p2_rom = (volatile const uint8_t *)0xA021C000UL;
    if(p2_rom[0] == 'K' && p2_rom[1] == 'A' &&
       p2_rom[2] == 'T' && p2_rom[3] == 'A') {
        for(uint32_t i = 0; i < 0x4000; i++) {
            p2_ram[i] = p2_rom[i];
        }
        return;
    }

    /* 3. If neither has valid data, initialize defaults into both SDRAM and physical FlashROM */
    for(uint32_t i = 0; i < 0x4000; i++) {
        p2_ram[i] = 0xFF;
    }

    /* Header (18 bytes): "KATANA_FLASH____\x02\x00" */
    const char *magic = "KATANA_FLASH____\x02\x00";
    for(int i = 0; i < 18; i++) {
        p2_ram[i] = (uint8_t)magic[i];
        jedec_flash_write_byte(0x1C000 + i, (uint8_t)magic[i]);
    }

    /* Block 1 (Sysconfig block at offset 0x40 and offset 0x80) */
    for(int i = 0; i < 64; i++) {
        p2_ram[0x40 + i] = s_syscfg_default[i];
        jedec_flash_write_byte(0x1C040 + i, s_syscfg_default[i]);
        p2_ram[0x80 + i] = s_syscfg_default[i];
        jedec_flash_write_byte(0x1C080 + i, s_syscfg_default[i]);
    }

    /* Bitmap at offset 0x3FC0 */
    for(int i = 0; i < 64; i++) {
        p2_ram[0x3FC0 + i] = 0xFF;
    }
    p2_ram[0x3FC0] = 0x3F; /* Block 0 & 1 allocated (bits 7 & 6 cleared) */
    jedec_flash_write_byte(0x1FFC0, 0x3F);


    const char *reg = get_disc_region_props();
    for(int i = 0; i < 5; i++) {
        jedec_flash_write_byte(0x1A000 + i, (uint8_t)reg[i]);
        jedec_flash_write_byte(0x1AA00 + i, (uint8_t)reg[i]);
    }
    const char *sys = "Dreamcast  ";
    for(int i = 0; i < 11; i++) {
        jedec_flash_write_byte(0x1A005 + i, (uint8_t)sys[i]);
        jedec_flash_write_byte(0x1AA05 + i, (uint8_t)sys[i]);
    }
}

static uint8_t fake_flashrom_byte(uint32_t addr) {
    init_flashrom_if_needed();

    if(addr >= 0x1C000 && addr < 0x20000) {
        return *(volatile const uint8_t *)(FLASH_RAM_BASE + (addr - 0x1C000));
    }

    const char *region = get_disc_region_props();
    if(addr >= 0x1A000 && addr < 0x1A005) return (uint8_t)region[addr - 0x1A000];
    if(addr >= 0x1A005 && addr < 0x1A010) {
        const char *sys = "Dreamcast  ";
        return (uint8_t)sys[addr - 0x1A005];
    }
    if(addr >= 0x1A0A0 && addr < 0x1A0A5) return (uint8_t)region[addr - 0x1A0A0];
    if(addr >= 0x1A0A5 && addr < 0x1A0B0) {
        const char *sys = "Dreamcast  ";
        return (uint8_t)sys[addr - 0x1A0A5];
    }

    return *(const volatile uint8_t *)(0xA0200000UL + addr);
}


static int kos_flashrom_dispatch(uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t func) {
    init_flashrom_if_needed();

    if(func == 0) {
        /* flashrom_info (partition, ptrs[2]) */
        int part = (int)arg0;
        if(!arg1) return -1;
        int *ptrs = (int *)kos_buffer_address((void *)arg1);

        switch(part) {
            case 0: ptrs[0] = 0x1A000; ptrs[1] = 0x02000; break; /* System (Factory) */
            case 1: ptrs[0] = 0x18000; ptrs[1] = 0x02000; break; /* Reserved */
            case 2: ptrs[0] = 0x1C000; ptrs[1] = 0x04000; break; /* Block 1 (User / Syscfg) */
            case 3: ptrs[0] = 0x10000; ptrs[1] = 0x08000; break; /* Settings */
            case 4: ptrs[0] = 0x00000; ptrs[1] = 0x10000; break; /* Block 2 */
            default: return -1;
        }
        return 0;
    } else if(func == 1) {
        /* flashrom_read (offset, buffer, bytes) */
        uint32_t offset = arg0;
        if(!arg1) return -1;
        uint8_t *dst = (uint8_t *)kos_buffer_address((void *)arg1);
        uint32_t bytes = arg2;
        if(offset >= 0x20000) return -1;
        if(offset + bytes > 0x20000) bytes = 0x20000 - offset;

        for(uint32_t i = 0; i < bytes; i++) {
            dst[i] = fake_flashrom_byte(offset + i);
        }
        return 0; /* Returns 0 on success */
    } else if(func == 2) {
        /* flashrom_write (offset, buffer, bytes) */
        uint32_t offset = arg0;
        if(!arg1) return -1;
        const uint8_t *src = (const uint8_t *)kos_buffer_address((void *)arg1);
        uint32_t bytes = arg2;
        if(offset >= 0x20000) return -1;
        if(offset + bytes > 0x20000) bytes = 0x20000 - offset;

        for(uint32_t i = 0; i < bytes; i++) {
            uint32_t cur = offset + i;
            if(cur >= 0x1C000 && cur < 0x20000) {
                *(volatile uint8_t *)(FLASH_RAM_BASE + (cur - 0x1C000)) = src[i];
            }
            jedec_flash_write_byte(cur, src[i]);
        }
        return (int)bytes;
    } else if(func == 3) {
        /* flashrom_delete (offset) */
        uint32_t offset = arg0;
        if(offset >= 0x20000) return -1;
        if(offset == 0x1C000) {
            volatile uint8_t *p2_ram = (volatile uint8_t *)FLASH_RAM_BASE;
            for(uint32_t i = 0; i < 0x4000; i++) p2_ram[i] = 0xFF;
        }
        jedec_flash_erase_sector(offset);
        volatile uint8_t *dst = (volatile uint8_t *)(0xA0200000UL + offset);
        for(uint32_t i = 0; i < 0x4000 && (offset + i) < 0x20000; i++) {
            dst[i] = 0xFF;
        }
        return 0;
    }
    return 0;
}


static int kos_system_dispatch(uint32_t arg0, uint32_t arg1,
                               uint32_t arg2, uint32_t func) {
    (void)arg0; (void)arg1; (void)arg2;
    if(func == 0) {
        /* MISC normal init: set GD DMA start, clear normal interrupts and set border color */
        uint32_t gdstard = wince_get_gdstard();
        *(volatile uint32_t *)0xA05F7404UL = gdstard; /* SB_GDSTARD */
        *(volatile uint32_t *)0xA05F6808UL = 0;           /* SB_IML2NRM = 0 */
        *(volatile uint32_t *)0xA05F8040UL = 0x00C0BEBCUL; /* VO_BORDER_COL */
        return 0x00C0BEBC;
    }
    if(func == 2) {
        /* MISC check disk: return 0 = disc ready */
        return 0;
    }
    return 0;
}

void gdrom_install_syscall(void) {
    /* 1. Safe rts; nop stubs at 0x8C000000..0x8C000064 */
    uint16_t *low_stubs = (uint16_t *)0x8C000000UL;
    for(int i = 0; i < 50; i += 2) {
        low_stubs[i]     = 0x000B; /* rts */
        low_stubs[i + 1] = 0x0009; /* nop */
    }

    /* System drive status global variables checked by IP.BIN */
    uint32_t disc_type = (uint32_t)gdrom_get_cached_disc_type();
    if(disc_type == 0) disc_type = 0x10;
    *(volatile uint32_t *)0x8C000040UL = 0;
    *(volatile uint32_t *)0x8C000048UL = 2; /* STANDBY */
    *(volatile uint32_t *)0x8C00004CUL = disc_type;
    *(volatile uint32_t *)0xAC000040UL = 0;
    *(volatile uint32_t *)0xAC000048UL = 2;
    *(volatile uint32_t *)0xAC00004CUL = disc_type;

    /* 1b. Handoff stub at 0x8C000050 / 0xAC000050:
           Jumps to 0x8C010000 when the Sega license routine at 0xAC008300 executes rts. */
    uint16_t *handoff_c = (uint16_t *)0x8C000050UL;
    uint16_t *handoff_u = (uint16_t *)0xAC000050UL;
    handoff_c[0] = 0xD001; /* mov.l @(4,pc), r0 */
    handoff_c[1] = 0x402B; /* jmp @r0 */
    handoff_c[2] = 0x0009; /* nop (delay slot) */
    handoff_c[3] = 0x0009; /* nop */
    *(volatile uint32_t *)0x8C000058UL = 0x8C010000UL;

    handoff_u[0] = 0xD001;
    handoff_u[1] = 0x402B;
    handoff_u[2] = 0x0009;
    handoff_u[3] = 0x0009;
    *(volatile uint32_t *)0xAC000058UL = 0x8C010000UL;

    /* 2. Exception & Interrupt stubs for SH-4 VBR at 0x8C000000:
          0x8C000100: General Exception -> rte; nop
          0x8C000400: TLB Miss Exception -> rte; nop
          0x8C000600: Interrupt Vector (VBlank, TMU, DMA) -> acknowledge ASIC & rte */
    uint16_t *exc_stub = (uint16_t *)0x8C000100UL;
    exc_stub[0] = 0x002B; /* rte */
    exc_stub[1] = 0x0009; /* nop */

    uint16_t *tlb_stub = (uint16_t *)0x8C000400UL;
    tlb_stub[0] = 0x002B; /* rte */
    tlb_stub[1] = 0x0009; /* nop */

    extern const uint8_t interrupt_stub_template[];
    extern const uint8_t interrupt_stub_template_end[];

    size_t irq_len = (size_t)(interrupt_stub_template_end - interrupt_stub_template);
    const uint8_t *irq_src = interrupt_stub_template;
    uint8_t *irq_dst_cached = (uint8_t *)0x8C000600UL;
    uint8_t *irq_dst_uncached = (uint8_t *)0xAC000600UL;
    for(size_t i = 0; i < irq_len; i++) {
        irq_dst_cached[i]   = irq_src[i];
        irq_dst_uncached[i] = irq_src[i];
    }

    /* Replicate exception stubs in uncached P2 mirror (0xAC000000) */
    uint16_t *uncached_exc = (uint16_t *)0xAC000100UL;
    uncached_exc[0] = 0x002B; uncached_exc[1] = 0x0009;
    uint16_t *uncached_tlb = (uint16_t *)0xAC000400UL;
    uncached_tlb[0] = 0x002B; uncached_tlb[1] = 0x0009;

    /* 3. Initialize Sysinfo Block at 0x8C000068 (Factory Dreamcast Layout) */
    char *sysinfo = (char *)0x8C000068UL;
    char *sysinfo_uncached = (char *)0xAC000068UL;
    /* 0x00-0x07: system_id from flashrom 0x1A056 ("SEGA    ") */
    sysinfo[0] = 'S'; sysinfo[1] = 'E'; sysinfo[2] = 'G'; sysinfo[3] = 'A';
    sysinfo[4] = ' '; sysinfo[5] = ' '; sysinfo[6] = ' '; sysinfo[7] = ' ';
    /* 0x08-0x0C: system_props from disc region */
    const char *props = get_disc_region_props();
    sysinfo[8]  = props[0];
    sysinfo[9]  = props[1];
    sysinfo[10] = props[2];
    sysinfo[11] = props[3];
    sysinfo[12] = props[4];
    /* 0x0D-0x0F: padding (zeroes) */
    sysinfo[13] = 0; sysinfo[14] = 0; sysinfo[15] = 0;
    /* 0x10-0x17: time_lo (0), time_hi (0) */
    *(uint32_t *)&sysinfo[16] = 0;
    *(uint32_t *)&sysinfo[20] = 0;

    for(int i = 0; i < 24; i++) {
        sysinfo_uncached[i] = sysinfo[i];
    }

    /* 4. Install indirect function vectors in cached and uncached RAM */
    *(volatile uintptr_t *)0x8C0000B0UL = (uintptr_t)&kos_sysinfo_dispatch;
    *(volatile uintptr_t *)0x8C0000B4UL = (uintptr_t)&kos_biofont_dispatch;
    *(volatile uintptr_t *)0x8C0000B8UL = (uintptr_t)&kos_flashrom_dispatch;
    *(volatile uintptr_t *)0x8C0000BCUL = (uintptr_t)&gdrom_syscall_dispatch;
    *(volatile uintptr_t *)0x8C0000C0UL = (uintptr_t)&gdrom_syscall_dispatch;
    *(volatile uintptr_t *)0x8C0000E0UL = (uintptr_t)&kos_system_dispatch;

    *(volatile uintptr_t *)0xAC0000B0UL = (uintptr_t)&kos_sysinfo_dispatch;
    *(volatile uintptr_t *)0xAC0000B4UL = (uintptr_t)&kos_biofont_dispatch;
    *(volatile uintptr_t *)0xAC0000B8UL = (uintptr_t)&kos_flashrom_dispatch;
    *(volatile uintptr_t *)0xAC0000BCUL = (uintptr_t)&gdrom_syscall_dispatch;
    *(volatile uintptr_t *)0xAC0000C0UL = (uintptr_t)&gdrom_syscall_dispatch;
    *(volatile uintptr_t *)0xAC0000E0UL = (uintptr_t)&kos_system_dispatch;

    /* Install trampoline at 0x8C0010F0 for direct gd2 entry calls */
    volatile uint16_t *tramp_c = (volatile uint16_t *)0x8C0010F0UL;
    volatile uint16_t *tramp_u = (volatile uint16_t *)0xAC0010F0UL;
    tramp_c[0] = 0xD001;
    tramp_c[1] = 0x402B;
    tramp_c[2] = 0x0009;
    tramp_c[3] = 0x0009;
    *(volatile uint32_t *)&tramp_c[4] = (uint32_t)&gdrom_syscall_dispatch;

    for(int i = 0; i < 6; i++) {
        tramp_u[i] = tramp_c[i];
    }
}
