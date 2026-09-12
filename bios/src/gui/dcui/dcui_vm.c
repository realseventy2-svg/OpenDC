#include "dcui_vm.h"
#include "config.h"
#include "audio_driver.h"
#include "disc_service.h"
#include <arch/arch.h>
#include <string.h>

extern bios_screen_t g_next_screen_request;

void dcui_vm_init(dcui_vm_state_t *vm, const uint8_t *blob) {
    if (!vm || !blob) return;

    memset(vm, 0, sizeof(dcui_vm_state_t));
    vm->header = (const dcui_header_t *)blob;

    if (vm->header->magic != DCUI_MAGIC) return;

    vm->objects = (const dcui_mesh_object_t *)(blob + vm->header->off_objs);
    vm->vertices = (const dcui_vertex_t *)(blob + vm->header->off_v);
    vm->indices = (const uint16_t *)(blob + vm->header->off_idx);
    vm->events = (const dcui_event_entry_t *)(blob + vm->header->off_events);
    vm->code = (const dcui_instruction_t *)(blob + vm->header->off_code);

    vm->current_focus_node = vm->header->first_interactable;
}

int dcui_vm_dispatch_event(dcui_vm_state_t *vm, dcui_event_type_t event_type, int32_t source_node) {
    if (!vm || !vm->header || !vm->events) return 0;

    for (uint32_t i = 0; i < vm->header->event_count; i++) {
        const dcui_event_entry_t *ev = &vm->events[i];
        if (ev->event_type == (uint16_t)event_type) {
            if (ev->source_node_idx == -1 || ev->source_node_idx == source_node) {
                dcui_vm_execute(vm, ev->instruction_ip);
                return 1;
            }
        }
    }
    return 0;
}

void dcui_vm_execute(dcui_vm_state_t *vm, uint32_t start_ip) {
    if (!vm || !vm->code) return;

    uint32_t ip = start_ip;
    while (ip < vm->header->code_count) {
        const dcui_instruction_t *inst = &vm->code[ip++];
        switch (inst->opcode) {
            case DCUI_OP_PLAY_SOUND:
                if (inst->op2 == 0) audio_play_click();
                else if (inst->op2 == 1) audio_play_confirm();
                else if (inst->op2 == 2) audio_play_click();
                else if (inst->op2 == 3) audio_play_tone(0, 0x111C, 12, 0x1F, 20);
                break;

            case DCUI_OP_CALL_SERVICE:
                if (inst->op2 == DCUI_SERVICE_BOOT_DISC) {
                    disc_service_launch();
                } else if (inst->op2 == DCUI_SERVICE_REBOOT) {
                    arch_reboot();
                } else if (inst->op2 == DCUI_SERVICE_GOTO_SCREEN) {
                    g_next_screen_request = (bios_screen_t)inst->op3;
                }
                break;

            case DCUI_OP_FOCUS_NODE:
                vm->current_focus_node = (int32_t)inst->op2;
                break;

            case DCUI_OP_SET_VAR:
                if (inst->op1 < 16) vm->variables[inst->op1] = inst->op3;
                break;

            case DCUI_OP_HALT:
            default:
                return;
        }
    }
}
