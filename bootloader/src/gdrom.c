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

enum {
    KOS_SYSINFO_VECTOR      = 0xAC0000B0UL,
    KOS_GDROM_VECTOR       = 0xAC0000BCUL,
    KOS_GDROM_SUPERFUNC    = 0,
    KOS_FUNC_SEND_COMMAND  = 0,
    KOS_FUNC_CHECK_COMMAND = 1,
    KOS_FUNC_EXEC_SERVER   = 2,
    KOS_FUNC_INIT          = 3,
    KOS_FUNC_DRIVE_STATUS  = 4,
    KOS_FUNC_DMA_CHECK     = 7,
    KOS_FUNC_ABORT_COMMAND = 8,
    KOS_FUNC_RESET         = 9,
    KOS_FUNC_SECTOR_MODE   = 10,
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
    uint8_t packet[12] = { 0 };
    uint32_t total_bytes = (uint32_t)sectors * 2048U;
    int result;

    if(buffer == 0 || sectors == 0 || sectors > 31 || fad > 0xFFFFFFUL) {
        return GDROM_BAD_ARG;
    }

    packet[0]  = 0x30; /* Sega CD_READ */
    packet[1]  = 0x20; /* 2048 bytes/sector mode */
    packet[2]  = (uint8_t)(fad >> 16);
    packet[3]  = (uint8_t)(fad >> 8);
    packet[4]  = (uint8_t)fad;
    packet[8]  = (uint8_t)(sectors >> 16);
    packet[9]  = (uint8_t)(sectors >> 8);
    packet[10] = (uint8_t)sectors;

    result = gdrom_packet_begin((uint16_t)total_bytes);
    if(result != GDROM_OK) return result;
    result = gdrom_packet_write(packet);
    if(result != GDROM_OK) return result;
    result = gdrom_pio_read(buffer, (size_t)sectors * 2048U);
    if(result != GDROM_OK) return result;
    return gdrom_wait_complete();
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

        case KOS_CMD_NOP:
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
    if(super_function != KOS_GDROM_SUPERFUNC)
        return -1;

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

        case KOS_FUNC_SECTOR_MODE:
            if(!arg0) return -1;
            if(((kos_sector_mode_t *)arg0)->rw == 0)
                sector_mode = *(kos_sector_mode_t *)arg0;
            else
                *(kos_sector_mode_t *)arg0 = sector_mode;
            return 0;

        case KOS_FUNC_DMA_CHECK:
        case KOS_FUNC_PIO_CHECK:
            return -1;

        default:
            return -1;
    }
}

void gdrom_install_syscall(void) {
    *(volatile uintptr_t *)KOS_SYSINFO_VECTOR =
        (uintptr_t)&kos_sysinfo_dispatch;
    *(volatile uintptr_t *)KOS_GDROM_VECTOR =
        (uintptr_t)&gdrom_syscall_dispatch;
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
}
