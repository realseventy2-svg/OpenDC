#ifndef __DCUI_VM_H
#define __DCUI_VM_H

#include "dcui_types.h"

typedef struct {
    const dcui_header_t       *header;
    const dcui_binary_node_t  *nodes;
    const dcui_event_entry_t  *events;
    const dcui_instruction_t  *code;
    uint32_t                   variables[16];
    int32_t                    current_focus_node;
    int32_t                    active_camera_node;
    int32_t                    target_camera_node;
    float                      cam_trans_t;
    int                        cam_trans_frames;
} dcui_vm_state_t;

void dcui_vm_init(dcui_vm_state_t *vm, const uint8_t *dcui_blob);
int  dcui_vm_dispatch_event(dcui_vm_state_t *vm, dcui_event_type_t event_type, int32_t source_node);
void dcui_vm_execute(dcui_vm_state_t *vm, uint32_t start_ip);

#endif /* __DCUI_VM_H */
