#ifndef __DCUI_TYPES_H
#define __DCUI_TYPES_H

#include <stdint.h>

#define DCUI_MAGIC 0x49554344 /* 'DCUI' in Little-Endian */
#define DCUI_VERSION 1

/* Node Types */
typedef enum {
    DCUI_NODE_CONTAINER   = 0,
    DCUI_NODE_MESH        = 1,
    DCUI_NODE_CAMERA      = 2,
    DCUI_NODE_LIGHT       = 3,
    DCUI_NODE_TEXT_ANCHOR = 4,
    DCUI_NODE_TRIGGER     = 5
} dcui_node_type_t;

/* Node Flags */
#define DCUI_NODE_FLAG_VISIBLE      (1 << 0)
#define DCUI_NODE_FLAG_INTERACTABLE (1 << 1)
#define DCUI_NODE_FLAG_FOCUSED      (1 << 2)
#define DCUI_NODE_FLAG_BILLBOARD    (1 << 3)
#define DCUI_NODE_FLAG_EMISSIVE     (1 << 4)

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
    DCUI_OP_PLAY_ANIM       = 0x01, /* op1: node_idx, op2: clip_idx, op3: flags (loop/once) */
    DCUI_OP_STOP_ANIM       = 0x02, /* op1: node_idx */
    DCUI_OP_CAMERA_GOTO     = 0x03, /* op1: cam_node_idx, op2: duration_frames, op3: easing */
    DCUI_OP_FOCUS_NODE      = 0x04, /* op1: target_node_idx */
    DCUI_OP_PLAY_SOUND      = 0x05, /* op1: sound_id, op2: volume */
    DCUI_OP_CALL_SERVICE    = 0x06, /* op1: service_id, op2: param */
    DCUI_OP_SET_PROPERTY    = 0x07, /* op1: node_idx, op2: prop_id, op3: value */
    DCUI_OP_SET_VAR         = 0x08, /* op1: var_id, op2: value */
    DCUI_OP_BRANCH_IF       = 0x09, /* op1: var_id, op2: op (==, !=, >, <), op3: target_ip */
    DCUI_OP_BRANCH_DISC     = 0x0A, /* op1: if_present_ip, op2: if_empty_ip */
    DCUI_OP_DELAY           = 0x0B, /* op1: frames */
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

/* Alignment Macro */
#define DCUI_ALIGN4 __attribute__((aligned(4)))

/* 3D Vector */
typedef struct {
    float x, y, z;
} DCUI_ALIGN4 dcui_vec3_t;

/* Quaternion */
typedef struct {
    float x, y, z, w;
} DCUI_ALIGN4 dcui_quat_t;

/* Vertex Definition for PVR Hardware Rasterizer */
typedef struct {
    float x, y, z;          /* Position */
    float nx, ny, nz;       /* Normal */
    float u, v;             /* UV Coordinates */
    uint32_t argb;          /* Packed ARGB8888 vertex color / tint */
} DCUI_ALIGN4 dcui_vertex_t;

/* Material Definition */
typedef struct {
    uint32_t diffuse_color;
    uint32_t specular_color;
    int16_t  texture_id;    /* -1 if untextured */
    uint16_t flags;         /* Shading modes: Flat, Gourard, Alpha blend, Additive */
} DCUI_ALIGN4 dcui_material_t;

/* Scene Node in Binary Container */
typedef struct {
    char        name[32];
    uint32_t    type;               /* dcui_node_type_t */
    uint32_t    flags;              /* DCUI_NODE_FLAG_* */
    int32_t     parent_idx;         /* -1 for root */
    dcui_vec3_t position;
    dcui_quat_t rotation;
    dcui_vec3_t scale;
    int32_t     mesh_idx;           /* -1 if no mesh */
    int32_t     material_idx;       /* -1 if no material */
    int32_t     camera_idx;         /* -1 if no camera */
    char        text_binding[32];   /* Dynamic string template or binding variable */
    int32_t     nav_up;             /* Explicit spatial navigation links (-1 = auto raycast) */
    int32_t     nav_down;
    int32_t     nav_left;
    int32_t     nav_right;
} DCUI_ALIGN4 dcui_binary_node_t;

/* Keyframe Channel Types */
typedef enum {
    DCUI_ANIM_CHANNEL_POS = 0,
    DCUI_ANIM_CHANNEL_ROT = 1,
    DCUI_ANIM_CHANNEL_SCL = 2
} dcui_anim_channel_type_t;

/* Keyframe Transform Entry */
typedef struct {
    float       time;       /* In frames or seconds */
    dcui_vec3_t vec_val;    /* For Pos / Scale */
    dcui_quat_t quat_val;   /* For Rotation */
} DCUI_ALIGN4 dcui_keyframe_t;

/* Animation Track */
typedef struct {
    char     name[32];
    int32_t  target_node_idx;
    uint32_t channel_type;  /* dcui_anim_channel_type_t */
    uint32_t keyframe_count;
    uint32_t keyframe_offset; /* Offset to dcui_keyframe_t array */
} DCUI_ALIGN4 dcui_anim_track_t;

/* Animation Clip */
typedef struct {
    char     name[32];
    float    duration_frames;
    uint32_t track_count;
    uint32_t track_offset;   /* Offset to dcui_anim_track_t array */
} DCUI_ALIGN4 dcui_anim_clip_t;

/* Logic Event Binding */
typedef struct {
    uint16_t event_type;        /* dcui_event_type_t */
    int16_t  source_node_idx;   /* -1 for global/any */
    uint32_t instruction_ip;    /* Offset into bytecode instruction stream */
} DCUI_ALIGN4 dcui_event_entry_t;

/* VM Instruction */
typedef struct {
    uint8_t  opcode;    /* dcui_opcode_t */
    uint8_t  op1;
    uint16_t op2;
    uint32_t op3;
} DCUI_ALIGN4 dcui_instruction_t;

/* File Header */
typedef struct {
    uint32_t magic;         /* DCUI_MAGIC ('DCUI') */
    uint32_t version;       /* DCUI_VERSION */
    uint32_t file_size;     /* Total byte size */

    /* Section Offsets and Counts */
    uint32_t node_count;
    uint32_t node_offset;

    uint32_t mesh_count;
    uint32_t mesh_offset;

    uint32_t material_count;
    uint32_t material_offset;

    uint32_t texture_count;
    uint32_t texture_offset;

    uint32_t anim_clip_count;
    uint32_t anim_clip_offset;

    uint32_t event_count;
    uint32_t event_offset;

    uint32_t code_size;     /* Number of instructions */
    uint32_t code_offset;

    int32_t  initial_camera_node_idx;
    int32_t  initial_focus_node_idx;
} DCUI_ALIGN4 dcui_header_t;

#endif /* __DCUI_TYPES_H */
