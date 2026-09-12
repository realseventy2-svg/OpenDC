#include "memory_service.h"
#include <stdio.h>
#include <string.h>

void memory_service_read_bytes(uint32_t addr, uint8_t *buffer, size_t size) {
    if(!buffer) return;
    const volatile uint8_t *src = (const volatile uint8_t *)addr;
    for(size_t i = 0; i < size; i++) {
        buffer[i] = src[i];
    }
}

void memory_service_format_hex_row(uint32_t addr, char *hex1_out, char *hex2_out, char *ascii_out) {
    const volatile uint8_t *ptr = (const volatile uint8_t *)addr;

    if(hex1_out) {
        for(int b = 0; b < 8; b++) {
            sprintf(hex1_out + (b * 3), "%02X ", ptr[b]);
        }
    }

    if(hex2_out) {
        for(int b = 0; b < 8; b++) {
            sprintf(hex2_out + (b * 3), "%02X ", ptr[8 + b]);
        }
    }

    if(ascii_out) {
        for(int b = 0; b < 16; b++) {
            uint8_t byte = ptr[b];
            ascii_out[b] = (byte >= 32 && byte <= 126) ? (char)byte : '.';
        }
        ascii_out[16] = '\0';
    }
}
