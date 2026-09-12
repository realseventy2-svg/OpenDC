#ifndef __BIOS_MEMORY_SERVICE_H
#define __BIOS_MEMORY_SERVICE_H

#include <stdint.h>
#include <stddef.h>

void memory_service_read_bytes(uint32_t addr, uint8_t *buffer, size_t size);
void memory_service_format_hex_row(uint32_t addr, char *hex1_out, char *hex2_out, char *ascii_out);

#endif /* __BIOS_MEMORY_SERVICE_H */
