import sys
import os
import bpy

addon_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "blender_addon"))
if addon_dir not in sys.path:
    sys.path.insert(0, addon_dir)

import io_scene_dcui
io_scene_dcui.register()

# Clear existing scene objects
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

for m in list(bpy.data.materials):
    bpy.data.materials.remove(m)
for g in list(bpy.data.node_groups):
    bpy.data.node_groups.remove(g)

scene = bpy.context.scene

# Main Camera
cam_data = bpy.data.cameras.new("Main_Camera")
cam_data.lens = 35
cam_obj = bpy.data.objects.new("Main_Camera", cam_data)
scene.collection.objects.link(cam_obj)
cam_obj.location = (0.0, -4.6, 3.2)
cam_obj.rotation_euler = (0.96, 0.0, 0.0) # Look down at ~55 deg
scene.camera = cam_obj

# Key Light
light_data = bpy.data.lights.new(name="Key_Light", type='POINT')
light_data.energy = 1500
light_obj = bpy.data.objects.new(name="Key_Light", object_data=light_data)
scene.collection.objects.link(light_obj)
light_obj.location = (3.0, -3.0, 4.0)

# Materials
def create_colored_mat(name, rgba, roughness=0.3):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    mat.diffuse_color = rgba
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs['Base Color'].default_value = rgba
        bsdf.inputs['Roughness'].default_value = roughness
    return mat

mat_dc_orange = create_colored_mat("Mat_DC_Orange",  (1.0, 0.35, 0.0, 1.0), 0.2)
mat_dc_blue   = create_colored_mat("Mat_DC_Blue",    (0.05, 0.45, 0.95, 1.0), 0.2)
mat_dc_gray   = create_colored_mat("Mat_DC_Console", (0.85, 0.85, 0.88, 1.0), 0.4)
mat_dc_dark   = create_colored_mat("Mat_DC_Dark",    (0.18, 0.18, 0.22, 1.0), 0.5)
mat_dc_white  = create_colored_mat("Mat_DC_White",   (0.95, 0.95, 0.95, 1.0), 0.2)
mat_text_white= create_colored_mat("Mat_Text_White", (1.0, 1.0, 1.0, 1.0), 0.1)
mat_text_dark = create_colored_mat("Mat_Text_Dark",  (0.1, 0.1, 0.1, 1.0), 0.1)

# 3D Dreamcast Console Base
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, -0.15))
console_obj = bpy.context.active_object
console_obj.name = "Dreamcast_Console"
console_obj.scale = (2.4, 2.2, 0.45)
console_obj.data.materials.append(mat_dc_gray)
console_obj.opendc_node_type = 'MESH'

# Disc Lid Cylinder
bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.75, depth=0.1, location=(0.0, 0.1, 0.12))
lid_obj = bpy.context.active_object
lid_obj.name = "Disc_Lid"
lid_obj.scale = (1.5, 1.5, 0.08)
lid_obj.data.materials.append(mat_dc_white)
lid_obj.opendc_node_type = 'MESH'

# Swirl / LED Indicator
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.1, 0.18))
swirl_obj = bpy.context.active_object
swirl_obj.name = "Power_Indicator"
swirl_obj.scale = (0.25, 0.25, 0.06)
swirl_obj.data.materials.append(mat_dc_orange)
swirl_obj.opendc_node_type = 'MESH'

# Controller Ports Block
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, -1.05, -0.15))
ports_obj = bpy.context.active_object
ports_obj.name = "Controller_Ports"
ports_obj.scale = (1.6, 0.15, 0.20)
ports_obj.data.materials.append(mat_dc_dark)
ports_obj.opendc_node_type = 'MESH'

# Function to create 3D Text Mesh
def add_3d_text(name, body, loc, rot, size, mat):
    bpy.ops.object.text_add(location=loc, rotation=rot)
    t_obj = bpy.context.active_object
    t_obj.name = name
    t_obj.data.body = body
    t_obj.data.size = size
    t_obj.data.extrude = 0.03
    t_obj.data.align_x = 'CENTER'
    t_obj.data.align_y = 'CENTER'
    t_obj.data.materials.append(mat)
    # Convert curve/font to polygon mesh
    bpy.ops.object.convert(target='MESH')
    t_obj.opendc_node_type = 'MESH'
    return t_obj

# Interactive Buttons with 3D Text
buttons_info = [
    ("Btn_Boot_Disc",   (-2.0,  0.8, 0.45), "BOOT DISC",   mat_dc_orange, mat_text_white),
    ("Btn_Diagnostics", ( 2.0,  0.8, 0.45), "DIAGNOSTICS", mat_dc_blue,   mat_text_white),
    ("Btn_VMU",         (-2.0, -0.6, 0.45), "VMU MANAGER", mat_dc_blue,   mat_text_white),
    ("Btn_Settings",    ( 2.0, -0.6, 0.45), "SETTINGS",    mat_dc_orange, mat_text_white),
    ("Btn_Reboot",      ( 0.0, -1.6, 0.45), "REBOOT",      mat_dc_white,  mat_text_dark),
]

created_buttons = {}

for name, loc, label, btn_mat, txt_mat in buttons_info:
    # 1. Base Button Box
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    btn_obj = bpy.context.active_object
    btn_obj.name = name
    btn_obj.scale = (1.5, 0.6, 0.25)
    btn_obj.data.materials.append(btn_mat)
    btn_obj.opendc_node_type = 'INTERACTABLE'
    btn_obj.opendc_text_binding = label
    created_buttons[name] = btn_obj

    # 2. 3D Text directly on top of the button
    txt_z = loc[2] + 0.13
    txt_obj = add_3d_text(name + "_Text", label, (loc[0], loc[1], txt_z), (0.0, 0.0, 0.0), 0.18, txt_mat)

# Spatial Navigation Links
created_buttons["Btn_Boot_Disc"].opendc_nav_down  = created_buttons["Btn_VMU"]
created_buttons["Btn_Boot_Disc"].opendc_nav_right = created_buttons["Btn_Diagnostics"]

created_buttons["Btn_Diagnostics"].opendc_nav_down = created_buttons["Btn_Settings"]
created_buttons["Btn_Diagnostics"].opendc_nav_left = created_buttons["Btn_Boot_Disc"]

created_buttons["Btn_VMU"].opendc_nav_up    = created_buttons["Btn_Boot_Disc"]
created_buttons["Btn_VMU"].opendc_nav_down  = created_buttons["Btn_Reboot"]
created_buttons["Btn_VMU"].opendc_nav_right = created_buttons["Btn_Settings"]

created_buttons["Btn_Settings"].opendc_nav_up   = created_buttons["Btn_Diagnostics"]
created_buttons["Btn_Settings"].opendc_nav_down = created_buttons["Btn_Reboot"]
created_buttons["Btn_Settings"].opendc_nav_left = created_buttons["Btn_VMU"]

created_buttons["Btn_Reboot"].opendc_nav_up = created_buttons["Btn_VMU"]

# OpenDC Visual Logic Tree ("Blueprint")
logic_tree = bpy.data.node_groups.new("OpenDC_Main_Menu_Logic", "OpenDCLogicTreeType")
logic_tree.use_fake_user = True

for btn in created_buttons.values():
    btn.opendc_logic_tree = logic_tree

# A. Boot Disc Blueprint
ev_boot = logic_tree.nodes.new("OpenDCEventButtonNodeType")
ev_boot.location = (-400, 300)
ev_boot.button = 'PRESS_A'
ev_boot.target_object = created_buttons["Btn_Boot_Disc"]

act_boot_snd = logic_tree.nodes.new("OpenDCActionPlaySoundNodeType")
act_boot_snd.location = (-150, 300)
act_boot_snd.sound_id = 'CONFIRM'

srv_boot = logic_tree.nodes.new("OpenDCServiceBootDiscNodeType")
srv_boot.location = (100, 300)

logic_tree.links.new(ev_boot.outputs['Execute'], act_boot_snd.inputs['In'])
logic_tree.links.new(act_boot_snd.outputs['Out'], srv_boot.inputs['In'])

# B. Diagnostics Blueprint
ev_diag = logic_tree.nodes.new("OpenDCEventButtonNodeType")
ev_diag.location = (-400, 100)
ev_diag.button = 'PRESS_A'
ev_diag.target_object = created_buttons["Btn_Diagnostics"]

act_diag_snd = logic_tree.nodes.new("OpenDCActionPlaySoundNodeType")
act_diag_snd.location = (-150, 100)
act_diag_snd.sound_id = 'CONFIRM'

srv_diag = logic_tree.nodes.new("OpenDCServiceGotoScreenNodeType")
srv_diag.location = (100, 100)
srv_diag.target_screen = 'SYSINFO'

logic_tree.links.new(ev_diag.outputs['Execute'], act_diag_snd.inputs['In'])
logic_tree.links.new(act_diag_snd.outputs['Out'], srv_diag.inputs['In'])

# C. VMU Manager Blueprint
ev_vmu = logic_tree.nodes.new("OpenDCEventButtonNodeType")
ev_vmu.location = (-400, -100)
ev_vmu.button = 'PRESS_A'
ev_vmu.target_object = created_buttons["Btn_VMU"]

act_vmu_snd = logic_tree.nodes.new("OpenDCActionPlaySoundNodeType")
act_vmu_snd.location = (-150, -100)
act_vmu_snd.sound_id = 'CONFIRM'

srv_vmu = logic_tree.nodes.new("OpenDCServiceGotoScreenNodeType")
srv_vmu.location = (100, -100)
srv_vmu.target_screen = 'MAPLE'

logic_tree.links.new(ev_vmu.outputs['Execute'], act_vmu_snd.inputs['In'])
logic_tree.links.new(act_vmu_snd.outputs['Out'], srv_vmu.inputs['In'])

# D. Settings Blueprint
ev_set = logic_tree.nodes.new("OpenDCEventButtonNodeType")
ev_set.location = (-400, -300)
ev_set.button = 'PRESS_A'
ev_set.target_object = created_buttons["Btn_Settings"]

act_set_snd = logic_tree.nodes.new("OpenDCActionPlaySoundNodeType")
act_set_snd.location = (-150, -300)
act_set_snd.sound_id = 'CONFIRM'

srv_set = logic_tree.nodes.new("OpenDCServiceGotoScreenNodeType")
srv_set.location = (100, -300)
srv_set.target_screen = 'FLASHROM'

logic_tree.links.new(ev_set.outputs['Execute'], act_set_snd.inputs['In'])
logic_tree.links.new(act_set_snd.outputs['Out'], srv_set.inputs['In'])

# E. Reboot Blueprint
ev_reb = logic_tree.nodes.new("OpenDCEventButtonNodeType")
ev_reb.location = (-400, -500)
ev_reb.button = 'PRESS_A'
ev_reb.target_object = created_buttons["Btn_Reboot"]

srv_reb = logic_tree.nodes.new("OpenDCServiceRebootNodeType")
srv_reb.location = (100, -500)

logic_tree.links.new(ev_reb.outputs['Execute'], srv_reb.inputs['In'])

# Configure Workspace UI Layout
for ws in bpy.data.workspaces:
    for scr in ws.screens:
        for area in scr.areas:
            if area.type in ('DOPESHEET_EDITOR', 'TIMELINE'):
                area.type = 'NODE_EDITOR'
                for space in area.spaces:
                    if space.type == 'NODE_EDITOR':
                        space.tree_type = 'OpenDCLogicTreeType'
                        space.node_tree = logic_tree
            elif area.type == 'VIEW_3D':
                for space in area.spaces:
                    if space.type == 'VIEW_3D':
                        space.shading.type = 'MATERIAL'

# Save .blend file to res/scenes/bios_menu.blend
tool_dir = os.path.dirname(os.path.abspath(__file__))
scenes_dir = os.path.join(tool_dir, "res", "scenes")
os.makedirs(scenes_dir, exist_ok=True)
blend_out = os.path.join(scenes_dir, "bios_menu.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_out)
print(f"Successfully saved bios_menu .blend file: {blend_out}")
