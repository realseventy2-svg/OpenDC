#ifndef __DCUI_TYPES_H
#define __DCUI_TYPES_H

#include <stdint.h>

#define DCUI_MAGIC 0x49554344 /* 'DCUI' in Little-Endian */
#define DCUI_VERSION 2

/* Node Types */
typedef enum {
    DCUI_NODE_CONTAINER    = 0,
    DCUI_NODE_MESH         = 1,
    DCUI_NODE_INTERACTABLE = 2,
    DCUI_NODE_CAMERA       = 3,
    DCUI_NODE_LIGHT        = 4,
    DCUI_NODE_TEXT_ANCHOR  = 5
} dcui_node_type_t;

/* Node Flags */
#define DCUI_NODE_FLAG_VISIBLE      (1 << 0)
#define DCUI_NODE_FLAG_INTERACTABLE (1 << 1)
#define DCUI_NODE_FLAG_FOCUSED      (1 << 2)

/* Logic Event Triggers */
typedef enum {
    DCUI_EVENT_NONE             = 0,
    DCUI_EVENT_ON_INIT          = 1,
    DCUI_EVENT_ON_FOCUS         = 2,
    DCUI_EVENT_ON_BLUR          = 3,
    DCUI_EVENT_ON_PRESS_A       = 4,
    DCUI_EVENT_ON_PRESS_B       = 5,
    DCUI_EVENT_ON_PRESS_X       = 6,
    DCUI_EVENT_ON_PRESS_Y       = 7,
    DCUI_EVENT_ON_PRESS_START   = 8,
    DCUI_EVENT_ON_NAV_UP        = 9,
    DCUI_EVENT_ON_NAV_DOWN      = 10,
    DCUI_EVENT_ON_NAV_LEFT      = 11,
    DCUI_EVENT_ON_NAV_RIGHT     = 12,
    DCUI_EVENT_ON_MEDIA_CHANGE  = 13
} dcui_event_type_t;

/* Micro-VM Opcodes (Compiled Blender Blueprint Actions) */
typedef enum {
    DCUI_OP_NOP             = 0x00,
    DCUI_OP_PLAY_ANIM       = 0x01,
    DCUI_OP_STOP_ANIM       = 0x02,
    DCUI_OP_CAMERA_GOTO     = 0x03,
    DCUI_OP_FOCUS_NODE      = 0x04,
    DCUI_OP_PLAY_SOUND      = 0x05,
    DCUI_OP_CALL_SERVICE    = 0x06,
    DCUI_OP_SET_PROPERTY    = 0x07,
    DCUI_OP_SET_VAR         = 0x08,
    DCUI_OP_BRANCH_IF       = 0x09,
    DCUI_OP_BRANCH_DISC     = 0x0A,
    DCUI_OP_DELAY           = 0x0B,
    DCUI_OP_HALT            = 0xFF
} dcui_opcode_t;

/* Console System Service IDs */
typedef enum {
    DCUI_SERVICE_BOOT_DISC      = 1,
    DCUI_SERVICE_REBOOT         = 2,
    DCUI_SERVICE_GOTO_SCREEN    = 3,
    DCUI_SERVICE_TOGGLE_AUDIO   = 4,
    DCUI_SERVICE_CYCLE_LANG     = 5,
    DCUI_SERVICE_SAVE_FLASHROM  = 6,
    DCUI_SERVICE_AUDIO_TEST     = 7
} dcui_service_id_t;

#define DCUI_ALIGN4 __attribute__((aligned(4)))

/* DCUI 3D Vertex */
typedef struct {
    float vx, vy, vz;
    float nx, ny, nz;
} DCUI_ALIGN4 dcui_vertex_t;

/* Object Entry in Object Table (112 bytes) */
typedef struct {
    char     name[32];
    uint32_t type;
    uint32_t flags;
    uint32_t start_v;
    uint32_t v_count;
    uint32_t start_idx;
    uint32_t idx_count;
    uint16_t color_565;
    uint16_t pad;
    float    cx, cy, cz;
    char     text_binding[32];
    int16_t  nav_u, nav_d, nav_l, nav_r;
} DCUI_ALIGN4 dcui_mesh_object_t;

/* Logic Event Binding (8 bytes) */
typedef struct {
    uint16_t event_type;        /* dcui_event_type_t */
    int16_t  source_node_idx;   /* -1 for global/any */
    int32_t  instruction_ip;    /* Offset into bytecode instruction stream */
} DCUI_ALIGN4 dcui_event_entry_t;

/* VM Instruction (8 bytes) */
typedef struct {
    uint8_t  opcode;    /* dcui_opcode_t */
    uint8_t  op1;
    uint16_t op2;
    uint32_t op3;
} DCUI_ALIGN4 dcui_instruction_t;

/* File Header (80 bytes) */
typedef struct {
    uint32_t magic;         /* DCUI_MAGIC ('DCUI') */
    uint32_t version;       /* DCUI_VERSION */
    uint32_t total_size;    /* Total byte size */

    uint32_t obj_count;
    uint32_t vert_count;
    uint32_t idx_count;
    uint32_t event_count;
    uint32_t code_count;

    uint32_t off_objs;
    uint32_t off_v;
    uint32_t off_idx;
    uint32_t off_events;
    uint32_t off_code;

    int32_t  first_interactable;
    float    cam_x, cam_y, cam_z;
    uint32_t pad[3];
} DCUI_ALIGN4 dcui_header_t;

#endif /* __DCUI_TYPES_H */
