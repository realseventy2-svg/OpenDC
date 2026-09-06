#include "ata.h"

typedef volatile uint8_t  reg8_t;
typedef volatile uint16_t reg16_t;
typedef volatile uint32_t reg32_t;

/* Verified against kos/kernel/arch/dreamcast/hardware/g1ata.c */
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
#define ATA_TIMEOUT_LOOPS      1000000UL

void ata_delay(unsigned count) {
    while(count--) {
        __asm__ volatile("nop");
    }
}

uint8_t ata_status(void) {
    return G1_ATA_STATUS;
}

uint8_t ata_altstatus(void) {
    return G1_ATA_ALTSTATUS;
}

int ata_wait_status(uint8_t must_set, uint8_t must_clear) {
    for(unsigned long i = 0; i < ATA_TIMEOUT_LOOPS; ++i) {
        uint8_t status = G1_ATA_ALTSTATUS;
        if(status & (ATA_ST_ERR | ATA_ST_DF)) {
            return ATA_DEVICE_ERR;
        }
        if((status & must_set) == must_set && !(status & must_clear)) {
            return ATA_OK;
        }
        ata_delay(2);
    }
    return ATA_TIMEOUT;
}

int ata_wait_complete(void) {
    return ata_wait_status(0, ATA_ST_BSY | ATA_ST_DRQ);
}

int ata_init(void) {
    G1_ATA_PIO_RACCESS = G1_ACCESS_PIO_DEFAULT;
    G1_ATA_PIO_WACCESS = G1_ACCESS_PIO_DEFAULT;
    G1_ATA_CTL = 0;
    G1_ATA_DEVICE_SELECT = ATA_DEVICE_MASTER;
    ata_delay(20);

    return ata_wait_status(ATA_ST_DRDY, ATA_ST_BSY);
}

int ata_packet_begin(uint16_t byte_count) {
    int result = ata_wait_status(0, ATA_ST_BSY | ATA_ST_DRQ);
    if(result != ATA_OK) return result;

    G1_ATA_DEVICE_SELECT = ATA_DEVICE_MASTER;
    G1_ATA_CTL = 0x08;
    G1_ATA_FEATURES = 0;
    G1_ATA_IRQ_REASON = 0;
    G1_ATA_ERROR = 0;
    G1_ATA_BYTE_COUNT_LO = (uint8_t)(byte_count & 0xFF);
    G1_ATA_BYTE_COUNT_HI = (uint8_t)(byte_count >> 8);
    G1_ATA_COMMAND = ATA_CMD_PACKET;

    return ata_wait_status(ATA_ST_DRQ, ATA_ST_BSY);
}

int ata_packet_write(const uint8_t packet[12]) {
    if(packet == 0) return ATA_BAD_ARG;

    int result = ata_wait_status(ATA_ST_DRQ, ATA_ST_BSY);
    if(result != ATA_OK) return result;

    for(unsigned i = 0; i < 12; i += 2) {
        G1_ATA_DATA = (uint16_t)(packet[i] | ((uint16_t)packet[i + 1] << 8));
    }
    return ATA_OK;
}

int ata_pio_read(void *dst, size_t bytes, int swap_words) {
    uint8_t *out = (uint8_t *)dst;
    if(dst == 0 || bytes == 0 || (bytes & 1) != 0) return ATA_BAD_ARG;

    while(bytes != 0) {
        int result = ata_wait_status(ATA_ST_DRQ, ATA_ST_BSY);
        if(result != ATA_OK) return result;

        size_t words = ((size_t)G1_ATA_BYTE_COUNT_HI << 8) | (size_t)G1_ATA_BYTE_COUNT_LO;
        if(words == 0 || words > bytes) words = bytes;
        words &= ~((size_t)1);

        for(size_t i = 0; i < words; i += 2) {
            uint16_t value = G1_ATA_DATA;
            if(swap_words) {
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
    return ATA_OK;
}
