import bpy
import struct
import math
import mathutils
from bpy_extras.io_utils import ExportHelper

DCUI_MAGIC = 0x49554344
DCUI_VERSION = 1

# Node Types
NODE_CONTAINER = 0
NODE_MESH = 1
NODE_CAMERA = 2
NODE_LIGHT = 3
NODE_TEXT_ANCHOR = 4
NODE_TRIGGER = 5

# Events
EVENT_NONE = 0
EVENT_ON_INIT = 1
EVENT_ON_FOCUS = 2
EVENT_ON_BLUR = 3
EVENT_ON_PRESS_A = 4
EVENT_ON_PRESS_B = 5
EVENT_ON_PRESS_X = 6
EVENT_ON_PRESS_Y = 7
EVENT_ON_PRESS_START = 8
EVENT_ON_NAV_UP = 9
EVENT_ON_NAV_DOWN = 10
EVENT_ON_NAV_LEFT = 11
EVENT_ON_NAV_RIGHT = 12
EVENT_ON_MEDIA_CHANGE = 13

# Opcodes
OP_NOP = 0x00
OP_PLAY_ANIM = 0x01
OP_STOP_ANIM = 0x02
OP_CAMERA_GOTO = 0x03
OP_FOCUS_NODE = 0x04
OP_PLAY_SOUND = 0x05
OP_CALL_SERVICE = 0x06
OP_SET_PROPERTY = 0x07
OP_SET_VAR = 0x08
OP_BRANCH_IF = 0x09
OP_BRANCH_DISC = 0x0A
OP_DELAY = 0x0B
OP_HALT = 0xFF

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

def export_scene_to_dcui(filepath, scene=None):
    if scene is None:
        scene = bpy.context.scene

    objects = list(scene.objects)
    obj_to_idx = {obj: idx for idx, obj in enumerate(objects)}

    nodes_bytes = bytearray()
    meshes_bytes = bytearray()
    materials_bytes = bytearray()
    textures_bytes = bytearray()
    anim_clips_bytes = bytearray()
    events_bytes = bytearray()
    code_bytes = bytearray()

    event_count = 0
    instructions = []

    # 1. Extract Materials Table
    materials = []
    mat_to_idx = {}
    for obj in objects:
        if obj.data and hasattr(obj.data, "materials"):
            for m in obj.data.materials:
                if m and m not in mat_to_idx:
                    mat_to_idx[m] = len(materials)
                    materials.append(m)

    for m in materials:
        rgba = [0.8, 0.8, 0.8, 1.0]
        if hasattr(m, "diffuse_color"):
            rgba = list(m.diffuse_color)
        if getattr(m, "node_tree", None):
            bsdf = m.node_tree.nodes.get("Principled BSDF")
            if bsdf and 'Base Color' in bsdf.inputs:
                col = bsdf.inputs['Base Color'].default_value
                rgba = [col[0], col[1], col[2], col[3] if len(col) > 3 else 1.0]

        r = min(255, max(0, int(rgba[0] * 255)))
        g = min(255, max(0, int(rgba[1] * 255)))
        b = min(255, max(0, int(rgba[2] * 255)))
        a = min(255, max(0, int(rgba[3] * 255)))
        diffuse_argb = (a << 24) | (r << 16) | (g << 8) | b
        specular_argb = 0xFFFFFFFF
        texture_id = -1
        mat_flags = 0
        materials_bytes += struct.pack('<IIhh', diffuse_argb, specular_argb, texture_id, mat_flags)

    # 2. Compile Logic Node Trees into Bytecode with Full Chain Traversal
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
                if tid in ('OpenDCActionPlaySoundNodeType', 'OpenDCActionPlaySoundNode'):
                    s_id = SOUND_IDS.get(target_node.sound_id, 1)
                    inst_list.append((OP_PLAY_SOUND, 0, s_id, 255))
                elif tid in ('OpenDCServiceBootDiscNodeType', 'OpenDCServiceBootDiscNode'):
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['BOOT_DISC'], 0))
                elif tid in ('OpenDCServiceGotoScreenNodeType', 'OpenDCServiceGotoScreenNode'):
                    s_param = SCREEN_PARAMS.get(target_node.target_screen, 1)
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['GOTO_SCREEN'], s_param))
                elif tid in ('OpenDCServiceRebootNodeType', 'OpenDCServiceRebootNode'):
                    inst_list.append((OP_CALL_SERVICE, 0, SERVICE_IDS['REBOOT'], 0))
                elif tid in ('OpenDCActionCameraGotoNodeType', 'OpenDCActionCameraGotoNode'):
                    cam_idx = obj_to_idx.get(target_node.target_camera, 0)
                    inst_list.append((OP_CAMERA_GOTO, 0, cam_idx, target_node.duration_frames))

                compile_chain(ntree, target_node, inst_list, visited)


    unique_trees = set()
    for obj in objects:
        ntree = getattr(obj, "opendc_logic_tree", None)
        if ntree:
            unique_trees.add(ntree)
    for tree in bpy.data.node_groups:
        if getattr(tree, "bl_idname", "") == 'OpenDCLogicTreeType':
            unique_trees.add(tree)

    for ntree in unique_trees:
        for node in ntree.nodes:
            if node.bl_idname in ('OpenDCEventButtonNodeType', 'OpenDCEventButtonNode'):
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
                ev_type = btn_map.get(node.button, EVENT_ON_PRESS_A)
                src_idx = obj_to_idx.get(node.target_object, -1)
                entry_ip = len(instructions)

                compile_chain(ntree, node, instructions)
                instructions.append((OP_HALT, 0, 0, 0))

                events_bytes += struct.pack('<Hhi', ev_type, src_idx, entry_ip)
                event_count += 1

    for op, op1, op2, op3 in instructions:
        code_bytes += struct.pack('<BBHI', op, op1, op2, op3)

    # 3. Build Node Table
    mesh_count = 0
    first_interactable = -1

    for idx, obj in enumerate(objects):
        name_bytes = obj.name.encode('utf-8')[:31].ljust(32, b'\x00')
        node_type = NODE_MESH if obj.type == 'MESH' else (NODE_CAMERA if obj.type == 'CAMERA' else NODE_CONTAINER)
        flags = 1 # Visible

        dc_type = getattr(obj, "opendc_node_type", "")
        if dc_type == 'INTERACTABLE':
            node_type = NODE_MESH
            flags |= 2 # Interactable
            if first_interactable == -1:
                first_interactable = idx
        elif dc_type == 'TEXT_ANCHOR' or obj.type == 'FONT':
            node_type = NODE_TEXT_ANCHOR
            flags |= 1

        parent_idx = obj_to_idx.get(obj.parent, -1)

        # Compute exact World-Space transform
        loc = obj.matrix_world.to_translation()
        rot = obj.matrix_world.to_quaternion()
        scl = obj.matrix_world.to_scale()

        mesh_idx = -1
        if obj.type == 'MESH' and node_type != NODE_TEXT_ANCHOR:
            mesh_idx = mesh_count
            mesh_count += 1
            meshes_bytes += struct.pack('<II', 24, 36)


        material_idx = -1
        if obj.data and hasattr(obj.data, "materials") and len(obj.data.materials) > 0:
            m = obj.data.materials[0]
            material_idx = mat_to_idx.get(m, -1)

        camera_idx = 0 if obj.type == 'CAMERA' else -1

        text_binding = getattr(obj, "opendc_text_binding", "").encode('utf-8')[:31].ljust(32, b'\x00')
        nav_up = obj_to_idx.get(getattr(obj, "opendc_nav_up", None), -1)
        nav_down = obj_to_idx.get(getattr(obj, "opendc_nav_down", None), -1)
        nav_left = obj_to_idx.get(getattr(obj, "opendc_nav_left", None), -1)
        nav_right = obj_to_idx.get(getattr(obj, "opendc_nav_right", None), -1)

        node_entry = struct.pack(
            '<32sIIi'
            'fff'
            'ffff'
            'fff'
            'iii'
            '32s'
            'iiii',
            name_bytes, node_type, flags, parent_idx,
            loc.x, loc.y, loc.z,
            rot.x, rot.y, rot.z, rot.w,
            scl.x, scl.y, scl.z,
            mesh_idx, material_idx, camera_idx,
            text_binding,
            nav_up, nav_down, nav_left, nav_right
        )
        nodes_bytes += node_entry


    # 3. Assemble Header & Write Output
    hdr_fmt = '<III II II II II II II II ii'
    header_size = struct.calcsize(hdr_fmt)

    material_count = len(materials)
    node_offset = header_size
    mesh_offset = node_offset + len(nodes_bytes)
    material_offset = mesh_offset + len(meshes_bytes)

    texture_offset = material_offset + len(materials_bytes)
    anim_clip_offset = texture_offset + len(textures_bytes)
    event_offset = anim_clip_offset + len(anim_clips_bytes)
    code_offset = event_offset + len(events_bytes)
    total_file_size = code_offset + len(code_bytes)

    header = struct.pack(
        hdr_fmt,
        DCUI_MAGIC, DCUI_VERSION, total_file_size,
        len(objects), node_offset,
        mesh_count, mesh_offset,
        material_count, material_offset,
        0, texture_offset,
        0, anim_clip_offset,
        event_count, event_offset,
        len(instructions), code_offset,
        0, (first_interactable if first_interactable != -1 else 0)
    )

    with open(filepath, 'wb') as f:
        f.write(header)
        f.write(nodes_bytes)
        f.write(meshes_bytes)
        f.write(materials_bytes)
        f.write(textures_bytes)
        f.write(anim_clips_bytes)
        f.write(events_bytes)
        f.write(code_bytes)

    print(f"Exported OpenDC UI container: {filepath} ({total_file_size:,} bytes, header={header_size})")
    return {'FINISHED'}

class ExportDCUI(bpy.types.Operator, ExportHelper):
    bl_idname = "export_scene.dcui"
    bl_label = "Export OpenDC BIOS UI"
    filename_ext = ".dcui"

    def execute(self, context):
        return export_scene_to_dcui(self.filepath, context.scene)

def register():
    try:
        bpy.utils.register_class(ExportDCUI)
    except Exception:
        pass

def unregister():
    try:
        bpy.utils.unregister_class(ExportDCUI)
    except Exception:
        pass

