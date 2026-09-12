import bpy
import bmesh
import struct
import math
import os
import sys

DCUI_MAGIC = 0x49554344 # 'DCUI'
DCUI_VERSION = 2

# Node Types
NODE_CONTAINER    = 0
NODE_MESH         = 1
NODE_INTERACTABLE = 2
NODE_CAMERA       = 3
NODE_LIGHT        = 4
NODE_TEXT_ANCHOR  = 5

# Events
EVENT_NONE             = 0
EVENT_ON_INIT          = 1
EVENT_ON_FOCUS         = 2
EVENT_ON_BLUR          = 3
EVENT_ON_PRESS_A       = 4
EVENT_ON_PRESS_B       = 5
EVENT_ON_PRESS_X       = 6
EVENT_ON_PRESS_Y       = 7
EVENT_ON_PRESS_START   = 8
EVENT_ON_NAV_UP        = 9
EVENT_ON_NAV_DOWN      = 10
EVENT_ON_NAV_LEFT      = 11
EVENT_ON_NAV_RIGHT     = 12

# Opcodes
OP_NOP             = 0x00
OP_PLAY_ANIM       = 0x01
OP_STOP_ANIM       = 0x02
OP_CAMERA_GOTO     = 0x03
OP_FOCUS_NODE      = 0x04
OP_PLAY_SOUND      = 0x05
OP_CALL_SERVICE    = 0x06
OP_SET_PROPERTY    = 0x07
OP_SET_VAR         = 0x08
OP_BRANCH_IF       = 0x09
OP_BRANCH_DISC     = 0x0A
OP_DELAY           = 0x0B
OP_HALT            = 0xFF

SOUND_IDS = {
    'CLICK': 0,
    'CONFIRM': 1,
    'CANCEL': 2,
    'BOOT_CHIME': 3,
    'ERROR': 4
}

SERVICE_IDS = {
    'BOOT_DISC': 1,
    'REBOOT': 2,
    'GOTO_SCREEN': 3,
    'TOGGLE_AUDIO': 4,
    'CYCLE_LANG': 5,
    'SAVE_FLASHROM': 6,
    'AUDIO_TEST': 7
}

SCREEN_PARAMS = {
    'SYSINFO': 1,
    'MAPLE': 2,
    'FLASHROM': 3,
    'MEMORY': 4,
    'TEST': 5
}

def linear_to_srgb(c):
    c = max(c, 0.0)
    if c <= 0.0031308:
        return c * 12.92
    return 1.055 * math.pow(c, 1.0 / 2.4) - 0.055

def color_to_rgb565(col):
    r_srgb = linear_to_srgb(col[0])
    g_srgb = linear_to_srgb(col[1])
    b_srgb = linear_to_srgb(col[2])
    r = int(min(max(r_srgb, 0.0), 1.0) * 31.0 + 0.5)
    g = int(min(max(g_srgb, 0.0), 1.0) * 63.0 + 0.5)
    b = int(min(max(b_srgb, 0.0), 1.0) * 31.0 + 0.5)
    return (r << 11) | (g << 5) | b

def get_object_color(obj, fallback=(0.85, 0.85, 0.88)):
    if obj and obj.data and hasattr(obj.data, 'materials') and len(obj.data.materials) > 0:
        mat = obj.data.materials[0]
        if mat:
            if getattr(mat, 'use_nodes', False) and mat.node_tree:
                for n in mat.node_tree.nodes:
                    if n.type == 'BSDF_PRINCIPLED' and 'Base Color' in n.inputs:
                        col = n.inputs['Base Color'].default_value
                        return (col[0], col[1], col[2])
                    elif n.type == 'EMISSION' and 'Color' in n.inputs:
                        col = n.inputs['Color'].default_value
                        return (col[0], col[1], col[2])
            if hasattr(mat, 'diffuse_color'):
                col = mat.diffuse_color
                return (col[0], col[1], col[2])
    return fallback

def export_dcui(output_path=None, scene=None):
    if scene is None:
        scene = bpy.context.scene

    cam = scene.camera
    if not cam:
        for o in scene.objects:
            if o.type == 'CAMERA':
                cam = o
                break
    cam_pos = cam.matrix_world.translation if cam else (0.0, -4.6, 3.2)

    all_objs = [o for o in scene.objects if not getattr(o, "hide_viewport", False)]
    mesh_objs = [o for o in all_objs if o.type == 'MESH']
    
    interactable_objs = [o for o in mesh_objs if getattr(o, "opendc_node_type", "") == 'INTERACTABLE']
    static_objs = [o for o in mesh_objs if getattr(o, "opendc_node_type", "") != 'INTERACTABLE']
    ordered_objs = static_objs + interactable_objs

    obj_to_idx = {obj: idx for idx, obj in enumerate(ordered_objs)}
    first_interactable = obj_to_idx.get(interactable_objs[0], 0) if interactable_objs else 0

    vert_bytes = bytearray()
    idx_bytes = bytearray()
    obj_defs = []

    total_vert_count = 0
    total_idx_count = 0

    depsgraph = bpy.context.evaluated_depsgraph_get()

    for idx, obj in enumerate(ordered_objs):
        eval_obj = obj.evaluated_get(depsgraph)
        me = eval_obj.to_mesh()

        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')

        mw = obj.matrix_world
        normal_matrix = mw.to_3x3().inverted().transposed()

        obj_verts = []
        obj_indices = []

        for f in bm.faces:
            tri_idx = []
            f_normal = (normal_matrix @ f.normal).normalized()
            for loop in f.loops:
                v = loop.vert
                w_pos = mw @ v.co
                vert_idx = len(obj_verts)
                obj_verts.append((w_pos.x, w_pos.y, w_pos.z, f_normal.x, f_normal.y, f_normal.z))
                tri_idx.append(vert_idx)
            if len(tri_idx) == 3:
                obj_indices.extend(tri_idx)

        bm.free()
        eval_obj.to_mesh_clear()

        start_v = total_vert_count
        v_count = len(obj_verts)
        start_idx = total_idx_count
        i_count = len(obj_indices)

        for vx, vy, vz, nx, ny, nz in obj_verts:
            vert_bytes.extend(struct.pack('<6f', vx, vy, vz, nx, ny, nz))
        for i_val in obj_indices:
            idx_bytes.extend(struct.pack('<H', i_val))

        raw_color = get_object_color(obj)
        color_565 = color_to_rgb565(raw_color)

        is_interactable = getattr(obj, "opendc_node_type", "") == 'INTERACTABLE'
        flags = 1
        if is_interactable:
            flags |= 2
        
        node_type = NODE_INTERACTABLE if is_interactable else NODE_MESH
        
        cx, cy, cz = mw.translation.x, mw.translation.y, mw.translation.z
        name_bytes = obj.name.encode('utf-8')[:31].ljust(32, b'\x00')
        text_binding = getattr(obj, "opendc_text_binding", "").encode('utf-8')[:31].ljust(32, b'\x00')

        nav_u = obj_to_idx.get(getattr(obj, "opendc_nav_up", None), -1)
        nav_d = obj_to_idx.get(getattr(obj, "opendc_nav_down", None), -1)
        nav_l = obj_to_idx.get(getattr(obj, "opendc_nav_left", None), -1)
        nav_r = obj_to_idx.get(getattr(obj, "opendc_nav_right", None), -1)

        obj_entry = struct.pack(
            '<32sIIIIIIHHfff32shhhh',
            name_bytes, node_type, flags,
            start_v, v_count, start_idx, i_count,
            color_565, 0,
            cx, cy, cz,
            text_binding,
            nav_u, nav_d, nav_l, nav_r
        )
        obj_defs.append(obj_entry)

        total_vert_count += v_count
        total_idx_count += i_count
        print(f"  [3D Mesh {idx:02d}] '{obj.name:20s}': {v_count} verts, {i_count//3} tris, color=0x{color_565:04X}, interactable={is_interactable}")

    # Compile Visual Logic Blueprints
    events_bytes = bytearray()
    code_bytes = bytearray()
    instructions = []
    event_count = 0

    def compile_chain(ntree, current_node, inst_list, visited=None):
        if visited is None:
            visited = set()
        if current_node in visited:
            return
        visited.add(current_node)

        for link in ntree.links:
            if link.from_node == current_node:
                target_node = link.to_node
                tid = getattr(target_node, "bl_idname", "")
                if tid.startswith('OpenDCActionPlaySound'):
                    s_id = SOUND_IDS.get(getattr(target_node, "sound_id", "CONFIRM"), 1)
                    inst_list.append((OP_PLAY_SOUND, 0, s_id, 255))
                elif tid.startswith('OpenDCServiceBootDisc'):
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['BOOT_DISC'], 0))
                elif tid.startswith('OpenDCServiceGotoScreen'):
                    s_param = SCREEN_PARAMS.get(getattr(target_node, "target_screen", "SYSINFO"), 1)
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['GOTO_SCREEN'], s_param))
                elif tid.startswith('OpenDCServiceReboot'):
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['REBOOT'], 0))
                elif tid.startswith('OpenDCActionCameraGoto'):
                    cam_idx = obj_to_idx.get(getattr(target_node, "target_camera", None), 0)
                    inst_list.append((OP_CAMERA_GOTO, 0, cam_idx, getattr(target_node, "duration_frames", 30)))

                compile_chain(ntree, target_node, inst_list, visited)

    unique_trees = set()
    for obj in ordered_objs:
        nt = getattr(obj, "opendc_logic_tree", None)
        if nt: unique_trees.add(nt)
    for tree in bpy.data.node_groups:
        if getattr(tree, "bl_idname", "") == 'OpenDCLogicTreeType':
            unique_trees.add(tree)

    for ntree in unique_trees:
        for node in ntree.nodes:
            if getattr(node, "bl_idname", "").startswith('OpenDCEventButton'):
                btn_map = {
                    'PRESS_A': EVENT_ON_PRESS_A,
                    'PRESS_B': EVENT_ON_PRESS_B,
                    'PRESS_X': EVENT_ON_PRESS_X,
                    'PRESS_Y': EVENT_ON_PRESS_Y,
                    'PRESS_START': EVENT_ON_PRESS_START,
                    'NAV_UP': EVENT_ON_NAV_UP,
                    'NAV_DOWN': EVENT_ON_NAV_DOWN,
                    'NAV_LEFT': EVENT_ON_NAV_LEFT,
                    'NAV_RIGHT': EVENT_ON_NAV_RIGHT,
                }
                ev_type = btn_map.get(getattr(node, "button", "PRESS_A"), EVENT_ON_PRESS_A)
                src_idx = obj_to_idx.get(getattr(node, "target_object", None), -1)
                entry_ip = len(instructions)

                compile_chain(ntree, node, instructions)
                instructions.append((OP_HALT, 0, 0, 0))

                events_bytes += struct.pack('<Hhi', ev_type, src_idx, entry_ip)
                event_count += 1

    for op, op1, op2, op3 in instructions:
        code_bytes += struct.pack('<BBHI', op, op1, op2, op3)

    obj_table_bytes = bytearray()
    for ob_d in obj_defs:
        obj_table_bytes.extend(ob_d)

    HEADER_SIZE = 80
    off_objs = HEADER_SIZE
    off_v = off_objs + len(obj_table_bytes)
    off_idx = off_v + len(vert_bytes)
    off_events = off_idx + len(idx_bytes)
    off_code = off_events + len(events_bytes)
    total_size = off_code + len(code_bytes)

    header = struct.pack(
        '<IIIIIIIIIIIIiifffIII',
        DCUI_MAGIC,
        DCUI_VERSION,
        total_size,
        len(ordered_objs),
        total_vert_count,
        total_idx_count,
        event_count,
        len(instructions),
        off_objs,
        off_v,
        off_idx,
        off_events,
        off_code,
        first_interactable,
        cam_pos[0], cam_pos[1], cam_pos[2],
        0, 0, 0
    )

    full_blob = header + obj_table_bytes + vert_bytes + idx_bytes + events_bytes + code_bytes

    tool_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    project_root = os.path.dirname(os.path.dirname(tool_dir))

    targets = [
        output_path if output_path else os.path.join(project_root, "bios", "romdisk", "menu.dcui"),
        os.path.join(project_root, "bios", "res", "ui", "default_menu.dcui")
    ]

    for tgt in targets:
        os.makedirs(os.path.dirname(tgt), exist_ok=True)
        with open(tgt, 'wb') as f:
            f.write(full_blob)
        print(f"  Exported DCUI v2 -> {tgt}")

    c_out = os.path.join(project_root, "bios", "src", "gui", "dcui", "default_scene.c")
    lines = [
        "#include <stdint.h>",
        "",
        f"const uint8_t DEFAULT_MENU_DCUI[{len(full_blob)}] = {{"
    ]
    for i in range(0, len(full_blob), 16):
        chunk = full_blob[i:i+16]
        hex_vals = [f"0x{b:02X}" for b in chunk]
        lines.append("    " + ", ".join(hex_vals) + ",")
    lines.append("};")
    lines.append("")
    with open(c_out, 'w') as f:
        f.write("\n".join(lines))
    print(f"  Updated embedded C scene -> {c_out}")

    print(f"=======================================================")
    print(f"  DCUI v2 Container Built: {len(full_blob):,} bytes (Header: {HEADER_SIZE} B)")
    print(f"  {len(ordered_objs)} Objects, {total_vert_count} Vertices, {total_idx_count//3} Triangles")
    print(f"  {event_count} Blueprint Events, {len(instructions)} Bytecode Instructions")
    print(f"=======================================================")
    return full_blob

if __name__ == '__main__':
    addon_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "blender_addon"))
    if addon_dir not in sys.path:
        sys.path.insert(0, addon_dir)
    try:
        import io_scene_dcui
        io_scene_dcui.register()
    except Exception as e:
        pass

    export_dcui()
