#include "syscalls.h"
#include "gdrom.h"
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
    void *params;
    int32_t result;
    size_t transferred;
    int active;
    int executed;
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
        case KOS_CMD_INIT:
            result = gdrom_init();
            if(result == GDROM_OK) {
                result = gdrom_prepare_disk();
            }
            break;

        case KOS_CMD_GETTOC:
        case KOS_CMD_GETTOC2: {
            kos_toc_params_t *params = (kos_toc_params_t *)pending_command.params;
            if(!params || !params->buffer || params->area < 0 || params->area > 1) {
                result = GDROM_BAD_ARG;
                break;
            }
            (void)gdrom_prepare_disk();
            result = gdrom_read_toc(params->buffer, (uint8_t)params->area);
            if(result != GDROM_OK && params->area == 0)
                result = gdrom_read_toc(params->buffer, 1);
            if(result == GDROM_OK)
                pending_command.transferred = sizeof(kos_toc_t);
            break;
        }

        case KOS_CMD_PIOREAD:
        case KOS_CMD_DMAREAD: {
            kos_read_params_t *params = (kos_read_params_t *)pending_command.params;
            if(!params || !params->buffer || params->num_sec == 0 ||
               params->num_sec > 0xFFFFU || params->start_sec > 0xFFFFFFU - 150U) {
                result = GDROM_BAD_ARG;
                break;
            }
            uint32_t fad = params->start_sec + 150U;
            result = gdrom_read_fad(kos_buffer_address(params->buffer), fad,
                                    (uint16_t)params->num_sec);
            if(result == GDROM_OK)
                pending_command.transferred = params->num_sec * 2048U;
            break;
        }

        case 36: { /* KOS_CMD_REQ_STAT */
            uint32_t *stat = (uint32_t *)pending_command.params;
            if(stat) {
                stat[0] = KOS_STATUS_STANDBY;
                stat[1] = KOS_DISC_GDROM;
            }
            result = GDROM_OK;
            break;
        }

        case 32: { /* KOS_CMD_REQ_TOC */
            uint32_t *toc_buf = (uint32_t *)pending_command.params;
            if(toc_buf) {
                result = gdrom_read_toc((kos_toc_t *)toc_buf, 0);
            }
            break;
        }

        case 2:  /* KOS_CMD_CHECK_LICENSE */
        case 27: /* KOS_CMD_SEEK */
        case 30: /* KOS_CMD_REQ_MODE */
        case 31: /* KOS_CMD_SET_MODE */
        case KOS_CMD_NOP:
            result = GDROM_OK;
            break;

        default:
            result = GDROM_NOT_READY;
            break;
    }

    pending_command.result = result;
    pending_command.executed = 1;

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
                                  uint32_t super_function,
                                  uint32_t function) {
    /* Handle MISC superfunction (r6 == -1) */
    if((int32_t)super_function == -1) {
        if(function == 0) {
            /* MISC_INIT */
            return 0;
        } else if(function == 1) {
            /* MISC_SETVECTOR */
            return 0;
        }
        return 0;
    }

    switch(function) {
        case KOS_FUNC_INIT:
        case KOS_FUNC_RESET:
            return gdrom_init();

        case KOS_FUNC_DRIVE_STATUS: {
            kos_drive_status_t *status = (kos_drive_status_t *)arg0;
            if(!status) return -1;
            status->status = KOS_STATUS_STANDBY;
            status->disc_type = KOS_DISC_GDROM;
            return 0;
        }

        case KOS_FUNC_SEND_COMMAND:
            if(pending_command.active && !pending_command.executed)
                execute_pending_command();
            if(arg0 == 0 || arg0 >= 47)
                return 0;
            pending_command.handle = next_handle++;
            if(next_handle <= 0) next_handle = 1;
            pending_command.command = arg0;
            pending_command.params = (void *)arg1;
            pending_command.result = 0;
            pending_command.transferred = 0;
            pending_command.active = 1;
            pending_command.executed = 0;
            return pending_command.handle;

        case KOS_FUNC_EXEC_SERVER:
            if(pending_command.active && !pending_command.executed)
                execute_pending_command();
            return 0;

        case KOS_FUNC_CHECK_COMMAND: {
            kos_command_status_t *status = (kos_command_status_t *)arg1;
            clear_command_status(status);
            if(!pending_command.active || (int32_t)arg0 != pending_command.handle)
                return KOS_CMD_NOT_FOUND;
            if(!pending_command.executed)
                execute_pending_command();
            if(status)
                status->size = pending_command.transferred;
            if(pending_command.result != GDROM_OK) {
                if(status)
                    status->err1 = (pending_command.result == GDROM_NOT_READY) ? 2 : 1;
                return KOS_CMD_FAILED;
            }
            return KOS_CMD_COMPLETED;
        }

        case KOS_FUNC_ABORT_COMMAND:
            if(pending_command.active && (int32_t)arg0 == pending_command.handle)
                pending_command.active = 0;
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
            kos_transfer_params_t *transfer =
                (kos_transfer_params_t *)arg1;
            if(!pending_command.active ||
               (int32_t)arg0 != pending_command.handle || !transfer)
                return -1;
            if(!pending_command.executed)
                execute_pending_command();
            return 0;
        }

        case KOS_FUNC_SECTOR_MODE:
            if(!arg0) return -1;
            if(((kos_sector_mode_t *)arg0)->rw == 0)
                sector_mode = *(kos_sector_mode_t *)arg0;
            else
                *(kos_sector_mode_t *)arg0 = sector_mode;
            return 0;

        case KOS_FUNC_DMA_CHECK:
        case KOS_FUNC_PIO_CHECK:
            if(!pending_command.active ||
               (int32_t)arg0 != pending_command.handle)
                return -1;
            if(!pending_command.executed)
                execute_pending_command();
            if(arg1)
                *(size_t *)arg1 = pending_command.transferred;
            return 0;

        default:
            return 0;
    }
}

static int kos_sysinfo_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2;
    if(function == 0) {
        /* SYSINFO_INIT: returns 0 on success */
        return 0;
    }
    /* SYSINFO_ID (3) or SYSINFO_ICON (2): return pointer to 0x8C000068 */
    return (int)0x8C000068UL;
}

static int kos_biofont_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return (int)0xA000B000UL;
}


static const uint8_t syscfg_block[64] = {
    0x05, 0x00,             /* block_id = 5 (LE) */
    0x00, 0x00, 0x00, 0x00, /* date = 0 */
    0x00,                   /* unk1 */
    0x01,                   /* lang = 1 (English) */
    0x00,                   /* mono = 0 (Stereo) */
    0x00,                   /* autostart = 0 */
    0x00, 0x00, 0x00, 0x00, /* unk2 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x40, 0x03              /* CRC16 = 0x0340 */
};

static uint8_t fake_flashrom_byte(uint32_t addr) {
    /* Partition 0: System (Factory info, starts at 0x1A000) */
    if(addr >= 0x1A000 && addr < 0x1A005) {
        const char *region = "00110"; /* USA NTSC */
        return (uint8_t)region[addr - 0x1A000];
    }
    if(addr >= 0x1A005 && addr < 0x1A018) {
        const char *serial = "0000000000000000000";
        return (uint8_t)serial[addr - 0x1A005];
    }

    /* Partition 2: Block 1 / User Settings (starts at 0x1C000) */
    /* Header (18 bytes): "KATANA_FLASH____\x02\x00" */
    if(addr >= 0x1C000 && addr < 0x1C012) {
        const char *magic = "KATANA_FLASH____\x02\x00";
        return (uint8_t)magic[addr - 0x1C000];
    }

    /* Physical block 1 (offset 0x1C040 to 0x1C07F): Block 5 (Sysconfig) */
    if(addr >= 0x1C040 && addr < 0x1C080) {
        return syscfg_block[addr - 0x1C040];
    }

    /* Allocation bitmap at end of Partition 2 (offset 0x1FFC0 to 0x1FFFF) */
    if(addr >= 0x1FFC0 && addr < 0x20000) {
        if(addr == 0x1FFC0)
            return 0x40; /* Block 0 used, block 1 unused (terminates scan) */
        return 0xFF;
    }

    /* Fallback to physical flash or 0xFF */
    return *(const volatile uint8_t *)(0xA0200000UL + addr);
}

static int kos_flashrom_dispatch(uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t function) {
    if(function == 0) {
        /* flashrom_info (partition, ptrs[2]) */
        int part = (int)arg0;
        int *ptrs = (int *)arg1;
        if(!ptrs) return -1;

        switch(part) {
            case 0: ptrs[0] = 0x1A000; ptrs[1] = 0x02000; break; /* System (Factory) */
            case 1: ptrs[0] = 0x18000; ptrs[1] = 0x02000; break; /* Reserved */
            case 2: ptrs[0] = 0x1C000; ptrs[1] = 0x04000; break; /* Block 1 (User / Syscfg) */
            case 3: ptrs[0] = 0x10000; ptrs[1] = 0x08000; break; /* Settings */
            case 4: ptrs[0] = 0x00000; ptrs[1] = 0x10000; break; /* Block 2 */
            default: return -1;
        }
        return 0;
    } else if(function == 1) {
        /* flashrom_read (offset, buffer, bytes) */
        uint32_t offset = arg0;
        uint8_t *dst = (uint8_t *)arg1;
        uint32_t bytes = arg2;
        if(!dst) return -1;
        if(offset >= 0x20000) return 0;
        if(offset + bytes > 0x20000) bytes = 0x20000 - offset;

        for(uint32_t i = 0; i < bytes; i++) {
            dst[i] = fake_flashrom_byte(offset + i);
        }
        return (int)bytes;
    }
    return 0;
}

static int kos_system_dispatch(uint32_t arg0, uint32_t arg1,
                               uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return 0;
}

void gdrom_install_syscall(void) {
    /* 1. Safe rts; nop stubs at 0x8C000000..0x8C000064 */
    uint16_t *low_stubs = (uint16_t *)0x8C000000UL;
    for(int i = 0; i < 50; i += 2) {
        low_stubs[i]     = 0x000B; /* rts */
        low_stubs[i + 1] = 0x0009; /* nop */
    }

    /* 2. Initialize Sysinfo Block at 0x8C000068 */
    char *sysinfo = (char *)0x8C000068UL;
    char *sysinfo_uncached = (char *)0xAC000068UL;
    sysinfo[0] = 'S'; sysinfo[1] = 'E'; sysinfo[2] = 'G'; sysinfo[3] = 'A';
    sysinfo[4] = ' '; sysinfo[5] = 'S'; sysinfo[6] = 'E'; sysinfo[7] = 'G';
    sysinfo[8] = 'A'; sysinfo[9] = 'K'; sysinfo[10] = 'A'; sysinfo[11] = 'T';
    sysinfo[12] = 'A'; sysinfo[13] = 'N'; sysinfo[14] = 'A'; sysinfo[15] = ' ';
    /* System configuration at 0x8C000078 */
    sysinfo[16] = '1'; /* Auto-start */
    sysinfo[17] = '0'; /* Stereo */
    sysinfo[18] = '0'; /* Japan / Universal Region */
    sysinfo[19] = '1'; /* English */
    sysinfo[20] = '0'; /* NTSC */
    sysinfo[21] = '0';
    sysinfo[22] = '0';
    sysinfo[23] = '0';

    for(int i = 0; i < 24; i++) {
        sysinfo_uncached[i] = sysinfo[i];
    }

    /* 3. Install indirect function vectors in cached and uncached RAM */
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
}
