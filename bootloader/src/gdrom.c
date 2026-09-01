#include "gdrom.h"

typedef volatile uint8_t  reg8_t;
typedef volatile uint16_t reg16_t;
typedef volatile uint32_t reg32_t;

/* These structures mirror the public KOS GD-ROM ABI.  The bootloader is
   freestanding, so it deliberately does not include KOS headers. */
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
    uint32_t entry[99];
    uint32_t first;
    uint32_t last;
    uint32_t leadout_sector;
} kos_toc_t;

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
    KOS_SYSINFO_VECTOR      = 0xAC0000B0UL,
    KOS_GDROM_VECTOR       = 0xAC0000BCUL,
    KOS_GDROM_SUPERFUNC    = 0,
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

/* Verified against kos/kernel/arch/dreamcast/hardware/g1ata.c. */
#define G1_ATA_ALTSTATUS       (*(reg8_t  *)0xA05F7018UL)
#define G1_ATA_DATA            (*(reg16_t *)0xA05F7080UL)
#define G1_ATA_ERROR           (*(reg8_t  *)0xA05F7084UL)
#define G1_ATA_FEATURES        (*(reg8_t  *)0xA05F7084UL)
#define G1_ATA_IRQ_REASON      (*(reg8_t  *)0xA05F7088UL)
#define G1_ATA_BYTE_COUNT_LO   (*(reg8_t  *)0xA05F7090UL)
#define G1_ATA_BYTE_COUNT_HI   (*(reg8_t  *)0xA05F7094UL)
#define G1_ATA_DEVICE_SELECT   (*(reg8_t  *)0xA05F7098UL)
#define G1_ATA_STATUS          (*(reg8_t  *)0xA05F709CUL)
#define G1_ATA_COMMAND         (*(reg8_t  *)0xA05F709CUL)

#define G1_ATA_CTL             (*(reg8_t  *)0xA05F7018UL)
#define G1_ATA_PIO_RACCESS     (*(reg32_t *)0xA05F7490UL)
#define G1_ATA_PIO_WACCESS     (*(reg32_t *)0xA05F7494UL)

#define ATA_CMD_PACKET         0xA0
#define ATA_DEVICE_MASTER      0x00
#define G1_ACCESS_PIO_DEFAULT  0x00000222UL
#define GDROM_TIMEOUT_LOOPS    1000000UL

/*
 * Boot policy:
 *   1 = execute the disc's IP.BIN at 0x8c008300 so the normal SEGA license
 *       screen appears.  The BIOS syscall vectors are left untouched.
 *   0 = skip IP.BIN and load 1ST_READ.BIN directly.
 *
 * Keep this at 1 while diagnosing the post-license exception.
 */
#ifndef GDROM_BOOT_THROUGH_IP
#define GDROM_BOOT_THROUGH_IP 1
#endif

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
static int gdrom_swap_data_words = 0;
static int32_t cached_disc_status = KOS_STATUS_NO_DISC;
static int32_t cached_disc_type = 0;
static uint32_t cached_data_fad;

static void *kos_buffer_address(void *buffer) {
    uintptr_t address = (uintptr_t)buffer;
    if(address < 0x20000000UL)
        address |= 0x8C000000UL;
    return (void *)address;
}

static uint32_t toc_to_kos_track(uint32_t raw) {
    uint32_t control = (raw & 0xF0U) >> 4;
    uint32_t fad = (((raw >> 8) & 0xFFU) << 16) |
                   (((raw >> 16) & 0xFFU) << 8) |
                   ((raw >> 24) & 0xFFU);
    if(raw == 0xFFFFFFFFUL || raw == 0) return 0xFFFFFFFFUL;
    return (control << 28) | (fad & 0x00FFFFFFUL);
}

static uint32_t toc_to_kos_boundary(uint32_t raw) {
    uint32_t track = (raw >> 8) & 0xFFU;
    uint32_t ctrl  = (raw & 0xF0U) >> 4;
    return (ctrl << 28) | (track << 16);
}

static int kos_sysinfo_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return 0;
}

static void gdrom_delay(unsigned count) {
    while(count--) {
        __asm__ volatile("nop");
    }
}

uint8_t gdrom_status(void) {
    return G1_ATA_STATUS;
}

static int wait_status(uint8_t must_set, uint8_t must_clear) {
    unsigned long i;
    for(i = 0; i < GDROM_TIMEOUT_LOOPS; ++i) {
        uint8_t status = G1_ATA_ALTSTATUS;
        if(status & (GDROM_ST_ERR | GDROM_ST_DF)) {
            return GDROM_DEVICE_ERR;
        }
        if((status & must_set) == must_set && !(status & must_clear)) {
            return GDROM_OK;
        }
        gdrom_delay(2);
    }
    return GDROM_TIMEOUT;
}

int gdrom_init(void) {
    G1_ATA_PIO_RACCESS = G1_ACCESS_PIO_DEFAULT;
    G1_ATA_PIO_WACCESS = G1_ACCESS_PIO_DEFAULT;
    G1_ATA_CTL = 0;
    G1_ATA_DEVICE_SELECT = ATA_DEVICE_MASTER;
    gdrom_delay(20);

    return wait_status(GDROM_ST_DRDY, GDROM_ST_BSY);
}

int gdrom_drive_ready(void) {
    uint8_t status = gdrom_status();
    return (status & (GDROM_ST_BSY | GDROM_ST_ERR | GDROM_ST_DF)) == 0 &&
           (status & GDROM_ST_DRDY) != 0;
}

int gdrom_packet_begin(uint16_t byte_count) {
    int result = wait_status(0, GDROM_ST_BSY | GDROM_ST_DRQ);
    if(result != GDROM_OK) return result;

    G1_ATA_DEVICE_SELECT = ATA_DEVICE_MASTER;
    G1_ATA_CTL = 0x08;
    G1_ATA_FEATURES = 0;
    G1_ATA_IRQ_REASON = 0;
    G1_ATA_ERROR = 0;
    G1_ATA_BYTE_COUNT_LO = (uint8_t)(byte_count & 0xFF);
    G1_ATA_BYTE_COUNT_HI = (uint8_t)(byte_count >> 8);
    G1_ATA_COMMAND = ATA_CMD_PACKET;

    return wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
}

static int gdrom_prepare_disk(void) {
    uint8_t packet[12] = { 0 };
    int result;

    packet[0] = 0x70; /* REQ_STAT */
    packet[2] = 0x1F;

    result = gdrom_packet_begin(0);
    if(result != GDROM_OK) return result;
    result = gdrom_packet_write(packet);
    if(result != GDROM_OK) return result;
    return gdrom_wait_complete();
}

int gdrom_packet_write(const uint8_t packet[12]) {
    if(packet == 0) return GDROM_BAD_ARG;

    int result = wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
    if(result != GDROM_OK) return result;

    for(unsigned i = 0; i < 12; i += 2) {
        G1_ATA_DATA = (uint16_t)(packet[i] | ((uint16_t)packet[i + 1] << 8));
    }
    return GDROM_OK;
}

int gdrom_pio_read(void *dst, size_t bytes) {
    uint8_t *out = (uint8_t *)dst;
    if(dst == 0 || bytes == 0 || (bytes & 1) != 0) return GDROM_BAD_ARG;

    while(bytes != 0) {
        int result = wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
        if(result != GDROM_OK) return result;

        size_t words = ((size_t)G1_ATA_BYTE_COUNT_HI << 8) | (size_t)G1_ATA_BYTE_COUNT_LO;
        if(words == 0 || words > bytes) words = bytes;
        words &= ~((size_t)1);

        for(size_t i = 0; i < words; i += 2) {
            uint16_t value = G1_ATA_DATA;
            if(gdrom_swap_data_words) {
                out[i]     = (uint8_t)(value >> 8);
                out[i + 1] = (uint8_t)value;
            } else {
                out[i]     = (uint8_t)value;
                out[i + 1] = (uint8_t)(value >> 8);
            }
        }
        out += words;
        bytes -= words;
    }
    return GDROM_OK;
}

int gdrom_wait_complete(void) {
    return wait_status(0, GDROM_ST_BSY | GDROM_ST_DRQ);
}

static int gdrom_read_raw_toc(uint8_t *buffer, uint8_t session) {
    uint8_t packet[12] = { 0 };
    packet[0] = 0x14; /* Sega GET_TOC */
    packet[1] = session;
    packet[3] = 0x01;
    packet[4] = 0x98;

    int result = gdrom_packet_begin(408);
    if(result != GDROM_OK) return result;
    result = gdrom_packet_write(packet);
    if(result != GDROM_OK) return result;
    result = gdrom_pio_read(buffer, 408);
    if(result != GDROM_OK) return result;
    return gdrom_wait_complete();
}

int gdrom_read_toc(void *buffer, uint8_t session) {
    if(buffer == 0 || session > 1) return GDROM_BAD_ARG;

    int result = gdrom_read_raw_toc((uint8_t *)buffer, session);
    if(result != GDROM_OK) return result;

    /* Convert raw 408-byte TOC into public KOS TOC format for syscalls */
    uint32_t *toc = (uint32_t *)buffer;
    for(unsigned i = 0; i < 99; ++i)
        toc[i] = toc_to_kos_track(toc[i]);
    toc[99]  = toc_to_kos_boundary(toc[99]);
    toc[100] = toc_to_kos_boundary(toc[100]);
    toc[101] = toc_to_kos_track(toc[101]);

    return GDROM_OK;
}

int gdrom_read_fad(void *buffer, uint32_t fad, uint16_t sectors) {
    if(buffer == 0 || sectors == 0 || fad > 0xFFFFFFUL ||
       (uint32_t)(sectors - 1U) > (0xFFFFFFUL - fad)) {
        return GDROM_BAD_ARG;
    }

    uint8_t *dst = (uint8_t *)buffer;
    for(uint16_t s = 0; s < sectors; s++) {
        uint8_t packet[12] = { 0 };
        uint32_t cur_fad = fad + (uint32_t)s;
        int result;

        packet[0]  = 0x30; /* Sega CD_READ */
        packet[1]  = 0x20; /* 2048 bytes/sector mode */
        packet[2]  = (uint8_t)(cur_fad >> 16);
        packet[3]  = (uint8_t)(cur_fad >> 8);
        packet[4]  = (uint8_t)cur_fad;
        packet[8]  = 0;
        packet[9]  = 0;
        packet[10] = 1;

        result = gdrom_packet_begin(2048);
        if(result != GDROM_OK) return result;
        result = gdrom_packet_write(packet);
        if(result != GDROM_OK) return result;
        result = gdrom_pio_read(dst + ((size_t)s * 2048U), 2048);
        if(result != GDROM_OK) return result;
        result = gdrom_wait_complete();
        if(result != GDROM_OK) return result;
    }

    return GDROM_OK;
}

int gdrom_probe_toc(void) {
    static uint8_t toc[408] __attribute__((aligned(4)));
    int result = gdrom_prepare_disk();
    if(result != GDROM_OK) return result;

    result = gdrom_read_raw_toc(toc, 1);
    if(result != GDROM_OK)
        result = gdrom_read_raw_toc(toc, 0);

    if(result == GDROM_OK) {
        cached_disc_status = KOS_STATUS_STANDBY;
        cached_disc_type = (toc[0] != 0xFF && toc[0] != 0) ? KOS_DISC_GDROM : 0x10;
    } else {
        cached_disc_status = KOS_STATUS_NO_DISC;
        cached_disc_type = 0;
    }
    return result;
}

int gdrom_probe_iso(uint32_t *data_fad_out, uint8_t *pvd_head) {
    static uint8_t toc[408] __attribute__((aligned(4)));
    static uint8_t pvd[2048] __attribute__((aligned(4)));
    uint32_t data_fad = 0;
    int result;

    if(data_fad_out) *data_fad_out = 0;
    if(pvd_head) {
        for(int i = 0; i < 8; ++i) pvd_head[i] = 0;
    }

    result = gdrom_prepare_disk();
    if(result != GDROM_OK) return result;

    /* 1. Try Session 2 (GD-ROM High Density Area) */
    if(gdrom_read_raw_toc(toc, 1) == GDROM_OK) {
        for(int i = 0; i < 99; ++i) {
            const uint8_t *entry = toc + (i * 4);
            if(entry[0] == 0xFF) continue;
            uint8_t ctrl = (entry[0] >> 4) & 0x0F;
            uint32_t fad = ((uint32_t)entry[1] << 16) |
                           ((uint32_t)entry[2] << 8)  |
                            (uint32_t)entry[3];
            if(ctrl == 4 && fad >= 150 && fad < 0x00FFFFFF) {
                data_fad = fad;
                break;
            }
        }
    }

    /* 2. If Session 2 has no data track, try Session 1 (CDI / MIL-CD) */
    if(data_fad == 0) {
        if(gdrom_read_raw_toc(toc, 0) == GDROM_OK) {
            /* Scan backwards to find the last data track in Session 1 */
            for(int i = 98; i >= 0; --i) {
                const uint8_t *entry = toc + (i * 4);
                if(entry[0] == 0xFF) continue;
                uint8_t ctrl = (entry[0] >> 4) & 0x0F;
                uint32_t fad = ((uint32_t)entry[1] << 16) |
                               ((uint32_t)entry[2] << 8)  |
                                (uint32_t)entry[3];
                if(ctrl == 4 && fad >= 150 && fad < 0x00FFFFFF) {
                    data_fad = fad;
                    break;
                }
            }
        }
    }

    /* 3. Fallback candidate probing (45000, 11702, 150) */
    if(data_fad == 0) {
        uint32_t candidates[] = { 45000, 11702, 150 };
        for(int c = 0; c < 3; c++) {
            if(gdrom_read_fad(pvd, candidates[c] + 16, 1) == GDROM_OK) {
                if(pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0' &&
                   pvd[4] == '0' && pvd[5] == '1') {
                    data_fad = candidates[c];
                    break;
                }
            }
        }
    }

    if(data_fad == 0) return GDROM_NOT_READY;
    cached_data_fad = data_fad;
    if(data_fad_out) *data_fad_out = data_fad;

    /* Read ISO PVD at data_fad + 16 */
    result = gdrom_read_fad(pvd, data_fad + 16U, 1);
    if(result != GDROM_OK) return result;

    if(!(pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0' &&
         pvd[4] == '0' && pvd[5] == '1')) {
        /* Retry with opposite word endianness if needed */
        gdrom_swap_data_words = !gdrom_swap_data_words;
        result = gdrom_read_fad(pvd, data_fad + 16U, 1);
        if(result != GDROM_OK) return result;
    }

    if(pvd_head) {
        for(int i = 0; i < 8; ++i) pvd_head[i] = pvd[i];
    }

    return (pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0' &&
            pvd[4] == '0' && pvd[5] == '1') ? GDROM_OK : GDROM_DEVICE_ERR;
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
                /* KOS calls INIT before every media reinitialization.  The
                   real BIOS follows ATA setup with REQ_STAT (0x70); doing
                   only gdrom_init() leaves the subsequent GETTOC command in
                   the wrong drive state and KOS reports the filesystem as
                   absent. */
                result = gdrom_prepare_disk();
                if(result == GDROM_OK) {
                    cached_disc_status = KOS_STATUS_STANDBY;
                    cached_disc_type = KOS_DISC_GDROM;
                }
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

        case 2:  /* KOS_CMD_CHECK_LICENSE */
        case 27: /* KOS_CMD_SEEK */
        case 30: /* KOS_CMD_REQ_MODE */
        case 31: /* KOS_CMD_SET_MODE */
        case 36: /* KOS_CMD_REQ_STAT */
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
            status->status = cached_disc_status;
            status->disc_type = cached_disc_type;
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
            /* The standalone loader completes transfers synchronously, so
               there is no interrupt callback to register. */
            return 0;

        case KOS_FUNC_DMA_TRANSFER:
        case KOS_FUNC_PIO_TRANSFER: {
            kos_transfer_params_t *transfer =
                (kos_transfer_params_t *)arg1;
            if(!pending_command.active ||
               (int32_t)arg0 != pending_command.handle || !transfer)
                return -1;
            /* PIO/DMAREAD already placed the requested sectors in the
               command's destination. Report the transfer as complete. */
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

#define KOS_SYSINFO_VECTOR   0x8C0000B0UL
#define KOS_BIOFONT_VECTOR   0x8C0000B4UL
#define KOS_FLASHROM_VECTOR  0x8C0000B8UL
#define KOS_GDROM_VECTOR     0x8C0000BCUL
#define KOS_GDROM2_VECTOR    0x8C0000C0UL
#define KOS_SYSTEM_VECTOR    0x8C0000E0UL



static int kos_biofont_dispatch(uint32_t arg0, uint32_t arg1,
                                uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return 0;
}

static int kos_flashrom_dispatch(uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return 0;
}

static int kos_system_dispatch(uint32_t arg0, uint32_t arg1,
                               uint32_t arg2, uint32_t function) {
    (void)arg0; (void)arg1; (void)arg2; (void)function;
    return 0;
}

void gdrom_install_syscall(void) {
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

/* ISO9660 directory-record fields are not 4-byte aligned within a sector
   buffer. SH-4 requires natural alignment for 16/32-bit loads and raises an
   address-error exception on a misaligned mov.l/mov.w -- unlike x86, which
   tolerates it silently. Always read multi-byte fields byte-wise. */
static uint32_t read_le32_unaligned(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void gdrom_descramble(const uint8_t *src, uint8_t *dst, uint32_t size) {
    uint32_t chunks = size / 2048;
    for(uint32_t c = 0; c < chunks; c++) {
        const uint8_t *in = src + (c * 2048);
        uint8_t *out = dst + (c * 2048);
        for(uint32_t i = 0; i < 2048; i++) {
            uint32_t idx = ((i & 0x001) << 10) |
                           ((i & 0x002) << 8)  |
                           ((i & 0x004) << 6)  |
                           ((i & 0x008) << 4)  |
                           ((i & 0x010) << 2)  |
                           ((i & 0x020) << 0)  |
                           ((i & 0x040) >> 2)  |
                           ((i & 0x080) >> 4)  |
                           ((i & 0x100) >> 6)  |
                           ((i & 0x200) >> 8)  |
                           ((i & 0x400) >> 10);
            out[idx] = in[i];
        }
    }
    if(size % 2048) {
        for(uint32_t i = 0; i < (size % 2048); i++) {
            dst[chunks * 2048 + i] = src[chunks * 2048 + i];
        }
    }
}

int gdrom_boot_game(uint32_t data_fad) {
    /*
     * IMPORTANT: do not change SR here and do not replace the BIOS syscall
     * vectors before entering IP.BIN.
     *
     * IP.BIN's license-screen code is designed to run in the BIOS-provided
     * environment.  In particular, it eventually uses the system-call
     * mechanism and the BIOS continues the boot at 0x8c00b800.  Installing
     * our KOS-compatible C dispatcher here changes that ABI and can make the
     * license code return to the wrong place.
     *
     * The standalone GD-ROM driver does not need the KOS syscall table in
     * order to read the disc.  Keep gdrom_install_syscall() available for
     * callers that explicitly need it, but NEVER install it as part of the
     * retail-IP.BIN handoff.
     */
    if(gdrom_init() != GDROM_OK || gdrom_prepare_disk() != GDROM_OK)
        return GDROM_NOT_READY;

    if(data_fad == 0)
        data_fad = cached_data_fad;
    if(data_fad == 0)
        return GDROM_NOT_READY;

    /* 1. Try booting standard Sega IP.BIN (displays Sega license screen & boots 1ST_READ.BIN) */
    uint8_t *ip_buf = (uint8_t *)0x8C008000UL;
    int ip_res = GDROM_NOT_READY;
    uint32_t ip_candidates[5];
    unsigned ip_candidate_count = 0;

    /* Prefer the actual data-track FAD.  The other candidates are retained
       only as compatibility probes for dumps/images whose track offset was
       supplied differently.  Require the IP.BIN signature before executing. */
    ip_candidates[ip_candidate_count++] = data_fad;
    if(data_fad >= 150)
        ip_candidates[ip_candidate_count++] = data_fad - 150;
    ip_candidates[ip_candidate_count++] = 45000;
    ip_candidates[ip_candidate_count++] = 150;
    ip_candidates[ip_candidate_count++] = 11702;

    for(unsigned i = 0; i < ip_candidate_count; ++i) {
        uint32_t candidate = ip_candidates[i];
        if(candidate > 0xFFFFFFUL)
            continue;
        /* Probe one sector first.  Reading sixteen sectors for every
           candidate makes a bad CDI layout look like a hang. */
        ip_res = gdrom_read_fad(ip_buf, candidate, 1);
        if(ip_res == GDROM_OK && ip_buf[0] == 'S' && ip_buf[1] == 'E' &&
           ip_buf[2] == 'G' && ip_buf[3] == 'A') {
            ip_res = gdrom_read_fad(ip_buf, candidate, 16);
            break;
        }
    }

    if(ip_res == GDROM_OK && ip_buf[0] == 'S' && ip_buf[1] == 'E' && ip_buf[2] == 'G' && ip_buf[3] == 'A') {
        /*
         * 0x0808 is NOT an instruction-cache invalidate value on SH-4
         * (ICI is bit 11 / 0x0800).  Do not write it here.
         *
         * We may have executed code from the same P1 RAM area before loading
         * IP.BIN, so the safest first-stage handoff is to disable both L1
         * caches.  IP.BIN's bootstrap will establish its normal cache state.
         */
        *(volatile uint32_t *)0xFF00001CUL = 0x00000000UL;
        __asm__ volatile("nop\n\t" "nop\n\t" "nop\n\t" "nop" ::: "memory");

        /*
         * Keep these in static storage because the inline assembly changes
         * r15 before it loads the second address.
         */
        static const uint32_t ip_bin_stack_top = 0x8C008000UL;
        static const uint32_t ip_bin_entry      = 0x8C008300UL;

        __asm__ volatile(
            "mov.l  %0, r15\n\t"
            "mov    #0, r0\n\t"
            "mov.l  %1, r1\n\t"
            "jmp    @r1\n\t"
            "nop\n\t"
            :
            : "m"(ip_bin_stack_top), "m"(ip_bin_entry)
            : "r0", "r1", "r15"
        );
        return GDROM_OK;
    }

    static uint8_t sector[2048] __attribute__((aligned(4)));
    uint32_t file_fad = 0;
    uint32_t file_size = 0;

    /* 2. Read the ISO9660 Primary Volume Descriptor. */
    if(gdrom_read_fad(sector, data_fad + 16U, 1) != GDROM_OK) {
        return GDROM_DEVICE_ERR;
    }

    if(sector[0] != 1 || sector[1] != 'C' || sector[2] != 'D' ||
       sector[3] != '0' || sector[4] != '0' || sector[5] != '1') {
        return GDROM_DEVICE_ERR;
    }

    /*
     * The root directory record begins at byte 156 and is guaranteed to
     * contain these fields in a valid ISO9660 PVD.
     */
    uint8_t root_record_len = sector[156];
    if(root_record_len < 34)
        return GDROM_DEVICE_ERR;

    uint32_t root_lba = read_le32_unaligned(sector + 156 + 2);
    uint32_t root_size = read_le32_unaligned(sector + 156 + 10);
    uint32_t root_fad = data_fad + root_lba;

    /* 2. Traverse ISO root directory to locate 1ST_READ.BIN */
    uint32_t root_sectors = (root_size + 2047) / 2048;
    if(root_sectors > 16) root_sectors = 16;

    for(uint32_t s = 0; s < root_sectors; s++) {
        if(gdrom_read_fad(sector, root_fad + s, 1) != GDROM_OK) break;

        uint32_t offset = 0;
        while(offset < 2048) {
            uint8_t rec_len = sector[offset];
            if(rec_len == 0) break;

            uint32_t lba = read_le32_unaligned(sector + offset + 2);
            uint32_t sz = read_le32_unaligned(sector + offset + 10);
            uint8_t name_len = sector[offset + 32];
            const char *name = (const char *)(sector + offset + 33);

            char clean_name[32];
            size_t nlen = (name_len < 31) ? name_len : 31;
            for(size_t k = 0; k < nlen; k++) {
                char ch = name[k];
                if(ch == ';') { clean_name[k] = '\0'; nlen = k; break; }
                if(ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
                clean_name[k] = ch;
            }
            clean_name[nlen] = '\0';

            const char *target = "1ST_READ.BIN";
            int match = (nlen == 12);
            if(match) {
                for(int k = 0; k < 12; k++) {
                    if(clean_name[k] != target[k]) {
                        match = 0;
                        break;
                    }
                }
            }

            if(match) {
                file_fad = data_fad + lba;
                file_size = sz;
                break;
            }
            offset += rec_len;
        }
        if(file_fad != 0) break;
    }

    if(file_fad == 0 || file_size == 0) {
        /* Never guess a game executable location. A wrong FAD means we
           would execute arbitrary sector contents as SH-4 instructions. */
        return GDROM_DEVICE_ERR;
    }

    /*
     * 3. Read 1ST_READ.BIN directly into its normal Dreamcast execution
     *    address.
     *
     * This loader is targeting a GD-ROM.  Do NOT apply the MIL-CD/CD
     * scrambling transform here.  The public boot-process documentation
     * explicitly distinguishes CD scrambling from GD-ROM loading.
     */
    uint32_t total_sectors = (file_size + 2047U) / 2048U;
    uint8_t *dest = (uint8_t *)0x8C010000UL;

    /*
     * A normal 16 MB Dreamcast has RAM through 0x8CFFFFFF.  Keep the load
     * inside RAM and reserve the upper 1 MB for the loader/stack.
     */
    if(file_size == 0 || file_size > 0x00D00000UL)
        return GDROM_BAD_ARG;

    uint32_t read_count = 0;
    while(read_count < total_sectors) {
        uint32_t batch = total_sectors - read_count;
        if(batch > 16U) batch = 16U;

        if(gdrom_read_fad(dest + (read_count * 2048U),
                          file_fad + read_count,
                          (uint16_t)batch) != GDROM_OK) {
            return GDROM_DEVICE_ERR;
        }
        read_count += batch;
    }

    /*
     * We intentionally do not descramble a GD-ROM executable.  For a
     * MIL-CD/CD boot path, scrambling support must be selected explicitly;
     * applying it unconditionally destroys a normal GD-ROM 1ST_READ.BIN.
     */

    /* 4. Disable L1 caches before transferring to freshly loaded code. */
    *(volatile uint32_t *)0xFF00001CUL = 0x00000000UL;

    /* 5. Launch Game: reset stack pointer and jump to 0x8C010000. */
    static const uint32_t game_stack_top = 0x8D000000UL;
    static const uint32_t game_entry      = 0x8C010000UL;

    __asm__ volatile(
        "mov.l  %0, r15\n\t"
        "mov.l  %1, r1\n\t"
        "jmp    @r1\n\t"
        "nop\n\t"
        :
        : "m"(game_stack_top), "m"(game_entry)
        : "r1", "r15"
    );

    return GDROM_OK;
}

void gdrom_install_services(void) {
    volatile gdrom_service_table_t *services =
        (volatile gdrom_service_table_t *)GDROM_SERVICE_ADDR;

    services->magic = GDROM_SERVICE_MAGIC;
    services->version = GDROM_SERVICE_VERSION;
    services->size = (uint32_t)sizeof(gdrom_service_table_t);
    services->init = gdrom_init;
    services->status = gdrom_status;
    services->drive_ready = gdrom_drive_ready;
    services->read_toc = gdrom_read_toc;
    services->read_fad = gdrom_read_fad;
    services->boot_game = gdrom_boot_game;
    services->disc_present =
        (cached_disc_status == KOS_STATUS_STANDBY) ? 1U : 0U;
    services->data_fad = cached_data_fad;
}