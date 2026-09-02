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
    return result;
}

static int gdrom_syscall_dispatch(uint32_t arg0, uint32_t arg1,
                                  uint32_t super_function,
                                  uint32_t function) {
    (void)super_function;

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
                return 0;
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
                return KOS_CMD_PROCESSING;
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
        case KOS_FUNC_PIO_CALLBACK:
            return 0;

        case KOS_FUNC_DMA_TRANSFER:
        case KOS_FUNC_PIO_TRANSFER: {
            kos_transfer_params_t *transfer =
                (kos_transfer_params_t *)arg1;
            if(!pending_command.active ||
               (int32_t)arg0 != pending_command.handle || !transfer)
                return -1;
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
            if(arg1)
                *(size_t *)arg1 = pending_command.transferred;
            return 0;

        default:
            return -1;
    }
}

static int kos_sysinfo_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return (int)0x8C000068UL;
}

static int kos_biofont_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return (int)0xA000B000UL;
}

static int kos_flashrom_dispatch(uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t function) {
    if(function == 0) {
        int part = (int)arg0;
        int *ptrs = (int *)arg1;
        if(!ptrs) return -1;

        switch(part) {
            case 0: ptrs[0] = 0x00000; ptrs[1] = 0x02000; break;
            case 1: ptrs[0] = 0x08000; ptrs[1] = 0x04000; break;
            case 2: ptrs[0] = 0x0C000; ptrs[1] = 0x04000; break;
            case 3: ptrs[0] = 0x10000; ptrs[1] = 0x08000; break;
            case 4: ptrs[0] = 0x18000; ptrs[1] = 0x08000; break;
            default: return -1;
        }
        return 0;
    } else if(function == 1) {
        uint32_t offset = arg0;
        uint8_t *dst = (uint8_t *)arg1;
        uint32_t bytes = arg2;
        if(!dst) return -1;
        if(offset >= 0x20000) return 0;
        if(offset + bytes > 0x20000) bytes = 0x20000 - offset;

        const uint8_t *src = (const uint8_t *)(0xA0200000UL + offset);
        for(uint32_t i = 0; i < bytes; i++) {
            dst[i] = src[i];
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
    /* Initialize Sysinfo Block at 0x8C000068 */
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
