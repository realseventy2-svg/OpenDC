#include "maple_service.h"
#include <kos.h>
#include <dc/maple.h>
#include <string.h>

static void clean_string(char *dst, const char *src, int max_len) {
    int len = 0;
    for(int i = 0; i < max_len && src[i] != '\0'; i++) {
        if((unsigned char)src[i] >= 32 && (unsigned char)src[i] <= 126) {
            dst[len++] = src[i];
        }
    }
    while(len > 0 && dst[len - 1] == ' ') len--;
    dst[len] = '\0';
}

void maple_service_init(void) {
    /* Maple bus initialized by KOS */
}

void maple_service_poll_port(int port, maple_port_info_t *out_info) {
    if(!out_info || port < 0 || port >= 4) return;
    memset(out_info, 0, sizeof(maple_port_info_t));

    maple_device_t *dev = maple_enum_dev(port, 0);
    maple_device_t *vmu1 = maple_enum_dev(port, 1);
    maple_device_t *vmu2 = maple_enum_dev(port, 2);

    if(dev) {
        out_info->controller_connected = 1;
        clean_string(out_info->controller_name, dev->info.product_name, 28);
        out_info->functions = (uint32_t)dev->info.functions;
        out_info->area_code = dev->info.area_code;
    }

    if(vmu1) {
        out_info->vmu1_connected = 1;
        clean_string(out_info->vmu1_name, vmu1->info.product_name, 28);
    }

    if(vmu2) {
        out_info->vmu2_connected = 1;
        clean_string(out_info->vmu2_name, vmu2->info.product_name, 28);
    }
}
