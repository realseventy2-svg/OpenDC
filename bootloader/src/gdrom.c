#include "gdrom.h"
#include "ata.h"

static int gdrom_swap_data_words = 0;
static int32_t cached_disc_status = 7; /* KOS_STATUS_NO_DISC */
static int32_t cached_disc_type = 0;
static uint32_t cached_data_fad = 0;

uint32_t gdrom_get_cached_data_fad(void) {
    return cached_data_fad;
}

int32_t gdrom_get_cached_disc_type(void) {
    return cached_disc_type;
}

uint8_t gdrom_status(void) {
    return ata_status();
}

int gdrom_drive_ready(void) {
    uint8_t status = ata_status();
    return (status & (ATA_ST_BSY | ATA_ST_ERR | ATA_ST_DF)) == 0 &&
           (status & ATA_ST_DRDY) != 0;
}

int gdrom_init(void) {
    return ata_init();
}

int gdrom_prepare_disk(void) {
    uint8_t packet[12] = { 0 };
    int result;

    packet[0] = 0x70; /* REQ_STAT */
    packet[2] = 0x1F;

    result = ata_packet_begin(0);
    if(result != ATA_OK) return result;
    result = ata_packet_write(packet);
    if(result != ATA_OK) return result;
    return ata_wait_complete();
}

int gdrom_read_raw_toc(uint8_t *buffer, uint8_t session) {
    uint8_t packet[12] = { 0 };
    packet[0] = 0x14; /* Sega GET_TOC */
    packet[1] = session;
    packet[3] = 0x01;
    packet[4] = 0x98;

    int result = ata_packet_begin(408);
    if(result != ATA_OK) return result;
    result = ata_packet_write(packet);
    if(result != ATA_OK) return result;
    result = ata_pio_read(buffer, 408, gdrom_swap_data_words);
    if(result != ATA_OK) return result;
    return ata_wait_complete();
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

int gdrom_read_toc(void *buffer, uint8_t session) {
    if(buffer == 0 || session > 1) return GDROM_BAD_ARG;

    int result = gdrom_read_raw_toc((uint8_t *)buffer, session);
    if(result != GDROM_OK) return result;

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

        result = ata_packet_begin(2048);
        if(result != ATA_OK) return result;
        result = ata_packet_write(packet);
        if(result != ATA_OK) return result;
        result = ata_pio_read(dst + ((size_t)s * 2048U), 2048, gdrom_swap_data_words);
        if(result != ATA_OK) return result;
        result = ata_wait_complete();
        if(result != ATA_OK) return result;
    }

    return GDROM_OK;
}

int gdrom_probe_toc(void) {
    uint8_t *toc = (uint8_t *)0x8C004000UL;
    int result = gdrom_prepare_disk();
    if(result != GDROM_OK) return result;

    result = gdrom_read_raw_toc(toc, 1);
    if(result != GDROM_OK)
        result = gdrom_read_raw_toc(toc, 0);

    if(result == GDROM_OK) {
        cached_disc_status = 2; /* STANDBY */
        cached_disc_type = (toc[0] != 0xFF && toc[0] != 0) ? 0x80 : 0x10;
    } else {
        cached_disc_status = 7; /* NO_DISC */
        cached_disc_type = 0;
    }
    return result;
}

int gdrom_probe_iso(uint32_t *data_fad_out, uint8_t *pvd_head) {
    uint8_t *toc = (uint8_t *)0x8C004000UL;
    uint8_t *pvd = (uint8_t *)0x8C004200UL;
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
    }

    /* 3. Fallback candidate probing */
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
    services->disc_present = (cached_disc_status == 2) ? 1U : 0U;
    services->data_fad = cached_data_fad;
}