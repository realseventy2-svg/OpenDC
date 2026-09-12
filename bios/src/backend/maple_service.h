#ifndef __BIOS_MAPLE_SERVICE_H
#define __BIOS_MAPLE_SERVICE_H

#include <stdint.h>

typedef struct {
    int controller_connected;
    char controller_name[32];
    uint32_t functions;
    uint8_t area_code;

    int vmu1_connected;
    char vmu1_name[32];

    int vmu2_connected;
    char vmu2_name[32];
} maple_port_info_t;

void maple_service_init(void);
void maple_service_poll_port(int port, maple_port_info_t *out_info);

#endif /* __BIOS_MAPLE_SERVICE_H */
