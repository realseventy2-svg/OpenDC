#!/usr/bin/env python3
import struct
import os

DCUI_MAGIC = 0x49554344
DCUI_VERSION = 1

# Node Types
NODE_CONTAINER = 0
NODE_MESH = 1
NODE_CAMERA = 2
NODE_LIGHT = 3
NODE_TEXT_ANCHOR = 4

# Events
EVENT_ON_PRESS_A = 4
EVENT_ON_PRESS_B = 5
EVENT_ON_PRESS_X = 6
EVENT_ON_PRESS_Y = 7
EVENT_ON_PRESS_START = 8
EVENT_ON_NAV_UP = 9
EVENT_ON_NAV_DOWN = 10
EVENT_ON_NAV_LEFT = 11
EVENT_ON_NAV_RIGHT = 12

# Opcodes
OP_NOP = 0x00
OP_PLAY_ANIM = 0x01
OP_CAMERA_GOTO = 0x03
OP_FOCUS_NODE = 0x04
OP_PLAY_SOUND = 0x05
OP_CALL_SERVICE = 0x06
OP_HALT = 0xFF

SERVICE_BOOT_DISC = 1
SERVICE_REBOOT = 2
SERVICE_GOTO_SCREEN = 3

def build_default_dcui(output_path):
    nodes = [
        # Name, type, flags, parent, pos(x,y,z), rot(x,y,z,w), scl(x,y,z), mesh_idx, mat_idx, cam_idx, text_bind, nav(u,d,l,r)
        ("Root_Scene", NODE_CONTAINER, 1, -1, (0,0,0), (0,0,0,1), (1,1,1), -1, -1, -1, "", -1, -1, -1, -1),
        ("Main_Camera", NODE_CAMERA, 1, 0, (0, 1.5, 4.5), (0,0,0,1), (1,1,1), -1, -1, 0, "", -1, -1, -1, -1),
        ("Btn_Boot_Disc", NODE_MESH, 3, 0, (-1.5, 0.6, 0.0), (0,0,0,1), (0.8,0.8,0.8), 0, 0, -1, "Boot GD-ROM", -1, 4, -1, 3),
        ("Btn_SysInfo", NODE_MESH, 3, 0, (1.5, 0.6, 0.0), (0,0,0,1), (0.8,0.8,0.8), 1, 0, -1, "Diagnostics", -1, 5, 2, -1),
        ("Btn_VMU", NODE_MESH, 3, 0, (-1.5, -0.4, 0.0), (0,0,0,1), (0.8,0.8,0.8), 2, 0, -1, "VMU Manager", 2, 6, -1, 5),
        ("Btn_Settings", NODE_MESH, 3, 0, (1.5, -0.4, 0.0), (0,0,0,1), (0.8,0.8,0.8), 3, 0, -1, "Settings", 3, 6, 4, -1),
        ("Btn_Reboot", NODE_MESH, 3, 0, (0.0, -1.3, 0.0), (0,0,0,1), (0.6,0.6,0.6), 4, 0, -1, "Reboot", 4, -1, -1, -1),
        ("Text_Status", NODE_TEXT_ANCHOR, 1, 0, (0, -2.0, 0), (0,0,0,1), (1,1,1), -1, -1, -1, "sys.status", -1, -1, -1, -1),
    ]

    nodes_bytes = bytearray()
    for n in nodes:
        name_bytes = n[0].encode('utf-8')[:31].ljust(32, b'\x00')
        text_binding = n[10].encode('utf-8')[:31].ljust(32, b'\x00')
        node_entry = struct.pack(
            '<32sIIi'
            'fff'
            'ffff'
            'fff'
            'iii'
            '32s'
            'iiii',
            name_bytes, n[1], n[2], n[3],
            n[4][0], n[4][1], n[4][2],
            n[5][0], n[5][1], n[5][2], n[5][3],
            n[6][0], n[6][1], n[6][2],
            n[7], n[8], n[9],
            text_binding,
            n[11], n[12], n[13], n[14]
        )
        nodes_bytes += node_entry

    meshes_bytes = bytearray()
    for _ in range(5):
        meshes_bytes += struct.pack('<II', 24, 36)

    materials_bytes = bytearray()
    materials_bytes += struct.pack('<IIhH', 0xFFFFFFFF, 0x00000000, -1, 0)

    textures_bytes = bytearray()
    anim_clips_bytes = bytearray()

    # Blueprint Bytecode
    instructions = [
        # 0: Boot disc action
        (OP_PLAY_SOUND, 0, 1, 255),
        (OP_CALL_SERVICE, 0, SERVICE_BOOT_DISC, 0),
        (OP_HALT, 0, 0, 0),

        # 3: Diagnostics action
        (OP_PLAY_SOUND, 0, 1, 255),
        (OP_CALL_SERVICE, 0, SERVICE_GOTO_SCREEN, 1),
        (OP_HALT, 0, 0, 0),

        # 6: VMU action
        (OP_PLAY_SOUND, 0, 1, 255),
        (OP_CALL_SERVICE, 0, SERVICE_GOTO_SCREEN, 2),
        (OP_HALT, 0, 0, 0),

        # 9: Settings action
        (OP_PLAY_SOUND, 0, 1, 255),
        (OP_CALL_SERVICE, 0, SERVICE_GOTO_SCREEN, 3),
        (OP_HALT, 0, 0, 0),

        # 12: Reboot action
        (OP_PLAY_SOUND, 0, 1, 255),
        (OP_CALL_SERVICE, 0, SERVICE_REBOOT, 0),
        (OP_HALT, 0, 0, 0),
    ]

    code_bytes = bytearray()
    for op, op1, op2, op3 in instructions:
        code_bytes += struct.pack('<BBHI', op, op1, op2, op3)

    events = [
        (EVENT_ON_PRESS_A, 2, 0),   # Btn_Boot_Disc -> IP 0
        (EVENT_ON_PRESS_A, 3, 3),   # Btn_SysInfo   -> IP 3
        (EVENT_ON_PRESS_A, 4, 6),   # Btn_VMU       -> IP 6
        (EVENT_ON_PRESS_A, 5, 9),   # Btn_Settings  -> IP 9
        (EVENT_ON_PRESS_A, 6, 12),  # Btn_Reboot    -> IP 12
        (EVENT_ON_PRESS_START, -1, 0), # Fast-boot anywhere
    ]

    events_bytes = bytearray()
    for ev, src, ip in events:
        events_bytes += struct.pack('<Hhi', ev, src, ip)

    hdr_fmt = '<III II II II II II II II ii'
    header_size = struct.calcsize(hdr_fmt)

    node_offset = header_size
    mesh_offset = node_offset + len(nodes_bytes)
    material_offset = mesh_offset + len(meshes_bytes)
    texture_offset = material_offset + len(materials_bytes)
    anim_clip_offset = texture_offset + len(textures_bytes)
    event_offset = anim_clip_offset + len(anim_clips_bytes)
    code_offset = event_offset + len(events_bytes)
    total_size = code_offset + len(code_bytes)

    header = struct.pack(
        hdr_fmt,
        DCUI_MAGIC, DCUI_VERSION, total_size,
        len(nodes), node_offset,
        5, mesh_offset,
        1, material_offset,
        0, texture_offset,
        0, anim_clip_offset,
        len(events), event_offset,
        len(instructions), code_offset,
        1, 2 # Initial camera = 1, initial focus = 2 (Btn_Boot_Disc)
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(nodes_bytes)
        f.write(meshes_bytes)
        f.write(materials_bytes)
        f.write(textures_bytes)
        f.write(anim_clips_bytes)
        f.write(events_bytes)
        f.write(code_bytes)

    print(f"Generated sample DCUI scene: {output_path} ({total_size:,} bytes, header={header_size})")

if __name__ == '__main__':
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else 'bios/romdisk/menu.dcui'
    build_default_dcui(out)
