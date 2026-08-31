#include "gdrom.h"

typedef volatile uint8_t  reg8_t;
typedef volatile uint16_t reg16_t;
typedef volatile uint32_t reg32_t;

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
/* Dreamcast G1 master selection is 0x00; 0xB0 is the slave. */
#define ATA_DEVICE_MASTER      0x00
#define G1_ACCESS_PIO_DEFAULT  0x00000222UL
#define GDROM_TIMEOUT_LOOPS    5000000UL

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
    /* These are the same conservative PIO timings used by KOS. */
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
    int result;

    if(byte_count == 0) {
        return GDROM_BAD_ARG;
    }

    result = wait_status(0, GDROM_ST_BSY | GDROM_ST_DRQ);
    if(result != GDROM_OK) {
        return result;
    }

    G1_ATA_DEVICE_SELECT = ATA_DEVICE_MASTER;
    G1_ATA_FEATURES = 0;
    G1_ATA_BYTE_COUNT_LO = (uint8_t)(byte_count & 0xFF);
    G1_ATA_BYTE_COUNT_HI = (uint8_t)(byte_count >> 8);
    G1_ATA_COMMAND = ATA_CMD_PACKET;

    return wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
}

int gdrom_packet_write(const uint8_t packet[12]) {
    unsigned i;
    int result;

    if(packet == 0) {
        return GDROM_BAD_ARG;
    }
    result = wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
    if(result != GDROM_OK) {
        return result;
    }

    /* ATA data words carry the first packet byte in the high byte on SH-4. */
    for(i = 0; i < 12; i += 2) {
        G1_ATA_DATA = (uint16_t)(((uint16_t)packet[i] << 8) | packet[i + 1]);
    }
    return GDROM_OK;
}

int gdrom_pio_read(void *dst, size_t bytes) {
    uint8_t *out = (uint8_t *)dst;

    if(dst == 0 || bytes == 0 || (bytes & 1) != 0) {
        return GDROM_BAD_ARG;
    }

    while(bytes != 0) {
        size_t words;
        size_t i;
        int result = wait_status(GDROM_ST_DRQ, GDROM_ST_BSY);
        if(result != GDROM_OK) {
            return result;
        }

        /* The drive may split a response into phases; respect its byte count. */
        words = ((size_t)G1_ATA_BYTE_COUNT_HI << 8) |
                (size_t)G1_ATA_BYTE_COUNT_LO;
        if(words == 0) {
            words = bytes;
        }
        if(words > bytes) {
            words = bytes;
        }
        words &= ~((size_t)1);

        for(i = 0; i < words; i += 2) {
            uint16_t value = G1_ATA_DATA;
            out[i] = (uint8_t)(value >> 8);
            out[i + 1] = (uint8_t)value;
        }
        out += words;
        bytes -= words;
    }
    return GDROM_OK;
}

int gdrom_wait_complete(void) {
    return wait_status(0, GDROM_ST_BSY | GDROM_ST_DRQ);
}

int gdrom_read_toc(void *buffer, uint8_t session) {
    uint8_t packet[12] = { 0 };
    int result;

    if(buffer == 0 || session > 1) {
        return GDROM_BAD_ARG;
    }

    packet[0] = 0x14; /* Sega GET_TOC */
    packet[1] = session;
    packet[3] = 0x01; /* 408-byte response */
    packet[4] = 0x98;

    result = gdrom_packet_begin(408);
    if(result != GDROM_OK) return result;
    result = gdrom_packet_write(packet);
    if(result != GDROM_OK) return result;
    result = gdrom_pio_read(buffer, 408);
    if(result != GDROM_OK) return result;
    return gdrom_wait_complete();
}

int gdrom_read_fad(void *buffer, uint32_t fad, uint16_t sectors) {
    uint8_t packet[12] = { 0 };
    int result;

    if(buffer == 0 || sectors == 0 || fad > 0xFFFFFFUL) {
        return GDROM_BAD_ARG;
    }

    packet[0] = 0x30; /* Sega CD_READ */
    packet[2] = (uint8_t)(fad >> 16);
    packet[3] = (uint8_t)(fad >> 8);
    packet[4] = (uint8_t)fad;
    packet[9] = (uint8_t)(sectors >> 8);
    packet[10] = (uint8_t)sectors;

    result = gdrom_packet_begin(2048);
    if(result != GDROM_OK) return result;
    result = gdrom_packet_write(packet);
    if(result != GDROM_OK) return result;
    result = gdrom_pio_read(buffer, (size_t)sectors * 2048U);
    if(result != GDROM_OK) return result;
    return gdrom_wait_complete();
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
