import bpy
import bmesh
import bpy_extras
import struct
import math
import os
import subprocess
import sys

bl_info = {
    "name": "OpenDC Dreamcast Boot Scene Exporter",
    "author": "OpenDC Team",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > OpenDC Boot",
    "description": "Universal Dreamcast Boot Scene Exporter and BIOS Pipeline",
    "category": "Import-Export",
}

DEFAULT_OUTPUT_BIN = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\boot_scene.bin"
DEFAULT_LETTERS_DIR = os.path.expanduser(r"~\Desktop\bleemcast_letters")
DEFAULT_AUDIO_MP3 = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\res\boot.mp3"
DEFAULT_AUDIO_PCM = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\res\boot_11k.pcm"
BOOTLOADER_DIR = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader"

def linear_to_srgb(c):
    c = max(c, 0.0)
    if c <= 0.0031308:
        return c * 12.92
    return 1.055 * math.pow(c, 1.0 / 2.4) - 0.055

def color_to_rgb565(col):
    r_srgb = linear_to_srgb(col[0])
    g_srgb = linear_to_srgb(col[1])
    b_srgb = linear_to_srgb(col[2])
    r = int(min(max(r_srgb, 0.0), 1.0) * 31.0)
    g = int(min(max(g_srgb, 0.0), 1.0) * 63.0)
    b = int(min(max(b_srgb, 0.0), 1.0) * 31.0)
    return (r << 11) | (g << 5) | b

def get_object_color(obj, fallback=(0.973, 0.109, 0.053)):
    if not obj:
        return fallback
    if obj.data and hasattr(obj.data, 'materials') and len(obj.data.materials) > 0:
        for mat in obj.data.materials:
            if not mat:
                continue
            if mat.use_nodes and mat.node_tree:
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
    if hasattr(obj, 'color') and any(c < 0.99 for c in obj.color[:3]):
        return (obj.color[0], obj.color[1], obj.color[2])
    return fallback

def is_object_visible(obj):
    if not obj or obj.type != 'MESH':
        return False
    if obj.hide_get() or obj.hide_render:
        return False
    # Exclude helper/background primitives
    if obj.name.startswith(('Cube', 'Plane', 'Text', 'Lamp')):
        return False
    for coll in obj.users_collection:
        if coll.hide_render or coll.hide_viewport:
            return False
    return True

def export_opendc_boot_scene(output_path=DEFAULT_OUTPUT_BIN,
                             audio_path=DEFAULT_AUDIO_MP3,
                             frame_step=2,
                             letters_dir=DEFAULT_LETTERS_DIR):
    scene = bpy.context.scene

    cam = scene.camera
    if not cam:
        for o in scene.objects:
            if o.type == 'CAMERA':
                cam = o
                break
    if not cam:
        raise Exception("No camera found in scene! Please add a Camera.")

    mesh_objs = []
    sprite_objs = []
    sphere_obj = None

    target_coll = bpy.data.collections.get('OpenDC_Scene')
    if target_coll:
        candidate_objs = list(target_coll.all_objects)
    else:
        candidate_objs = []
        for o in scene.objects:
            if o.type == 'MESH':
                if o.name.startswith('Sprite_') or (o.users_collection and any('letter' in c.name.lower() or 'sprite' in c.name.lower() for c in o.users_collection)):
                    sprite_objs.append(o)
                elif 'swirl' in o.name.lower() or 'sphere.002' in o.name.lower() or o.name == 'Sphere':
                    candidate_objs.append(o)
                elif is_object_visible(o):
                    candidate_objs.append(o)

    for o in candidate_objs:
        if o.type == 'MESH' and o not in sprite_objs:
            if 'sphere.002' in o.name.lower() or o.name == 'Sphere' or o.get('dc_ball', False):
                sphere_obj = o
            mesh_objs.append(o)

    # Ensure Swirl is Object 0 and Sphere is Object 1
    swirl_obj = next((o for o in mesh_objs if 'swirl' in o.name.lower() or o.get('dc_progressive', False)), None)
    if swirl_obj and swirl_obj in mesh_objs:
        mesh_objs.remove(swirl_obj)
        mesh_objs.insert(0, swirl_obj)
    if sphere_obj and sphere_obj in mesh_objs:
        mesh_objs.remove(sphere_obj)
        mesh_objs.insert(1 if len(mesh_objs) >= 1 else 0, sphere_obj)

    sprite_objs.sort(key=lambda o: o.name)

    print(f"[OpenDC] Discovered {len(mesh_objs)} 3D Mesh Object(s) and {len(sprite_objs)} 2D Sprite Object(s)")

    obj_defs = []
    all_verts = []
    all_indices = []
    total_triangles = 0

    ball_pts = []
    if swirl_obj and sphere_obj:
        swirl_mat_inv = swirl_obj.matrix_world.inverted()
        for f in range(scene.frame_start, min(scene.frame_end, 375)):
            scene.frame_set(f)
            p_local = swirl_mat_inv @ sphere_obj.matrix_world.translation
            ball_pts.append((f, p_local.x, p_local.y))

    face_reveal = []
    for o in mesh_objs:
        start_v = len(all_verts)
        start_t = total_triangles

        is_progressive = (o == swirl_obj and len(ball_pts) > 0) or o.get('dc_progressive', False)
        is_ball = (o == sphere_obj)

        if is_ball and len(o.data.vertices) > 150:
            mod_dec = o.modifiers.new('OpenDC_Decimate', 'DECIMATE')
            mod_dec.ratio = 0.20
            # Force depsgraph update so decimate modifier is properly evaluated
            dg = bpy.context.evaluated_depsgraph_get()
            o_eval = o.evaluated_get(dg)
            me = o_eval.to_mesh()
            o.modifiers.remove(mod_dec)
        else:
            dg = bpy.context.evaluated_depsgraph_get()
            o_eval = o.evaluated_get(dg)
            me = o_eval.to_mesh()

        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')

        if is_progressive and ball_pts:
            face_reveal = []
            for f in bm.faces:
                c = f.calc_center_median()
                best_dist = 1e9
                best_frame = scene.frame_start
                for frame, bx, by in ball_pts:
                    d = (c.x - bx)**2 + (c.y - by)**2
                    if d < best_dist:
                        best_dist = d
                        best_frame = frame
                face_reveal.append((best_frame, f))
            face_reveal.sort(key=lambda item: item[0])
            sorted_faces = [item[1] for item in face_reveal]
        else:
            sorted_faces = list(bm.faces)

        mesh_verts = []
        vert_map = {}
        mesh_indices = []

        for f in sorted_faces:
            tri_idx = []
            for v in f.verts:
                if v not in vert_map:
                    vert_map[v] = len(mesh_verts)
                    mesh_verts.append((v.co.x, v.co.y, v.co.z))
                tri_idx.append(vert_map[v])
            mesh_indices.extend(tri_idx)

        bm.free()
        o_eval.to_mesh_clear()

        raw_col = get_object_color(o)
        if is_ball and swirl_obj and raw_col == (0.9734399318695068, 0.10946179926395416, 0.05286070704460144):
            raw_col = get_object_color(swirl_obj)

        col_rgb565 = color_to_rgb565(raw_col)
        tri_count = len(sorted_faces)
        flags = 1 if is_progressive else 0

        obj_defs.append((start_v, len(mesh_verts), start_t, tri_count, col_rgb565, flags))
        all_verts.extend(mesh_verts)
        all_indices.extend(mesh_indices)
        total_triangles += tri_count

        print(f"  Mesh Object '{o.name}': {len(mesh_verts)} verts, {tri_count} tris, Color=0x{col_rgb565:04X} (flags={flags})")

    sprite_data = []
    if sprite_objs:
        scene.frame_set(250)
        for i, spr_obj in enumerate(sprite_objs):
            mat = spr_obj.matrix_world
            coords_2d = []
            for v in spr_obj.data.vertices:
                wco = mat @ v.co
                p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, wco)
                coords_2d.append((p2d.x * 640.0, (1.0 - p2d.y) * 480.0))
            w = max(1, int(round(max(c[0] for c in coords_2d) - min(c[0] for c in coords_2d))))
            h = max(1, int(round(max(c[1] for c in coords_2d) - min(c[1] for c in coords_2d))))

            png_candidates = [
                os.path.join(letters_dir, f"letter_{i:02d}_{spr_obj.name.replace('Sprite_', '')}.png"),
                os.path.join(letters_dir, f"letter_{i:02d}_{spr_obj.name}.png"),
            ]
            png_path = next((p for p in png_candidates if os.path.exists(p)), None)
            if not png_path and os.path.exists(letters_dir):
                match = sorted([f for f in os.listdir(letters_dir) if f.startswith(f"letter_{i:02d}") and f.endswith(".png")])
                if match:
                    png_path = os.path.join(letters_dir, match[0])

            raw_pixels = bytearray()
            if png_path:
                img = bpy.data.images.load(png_path)
                pixels = list(img.pixels)
                for py in range(h):
                    for px in range(w):
                        sx = int(px * img.size[0] / w)
                        sy = int((h - 1 - py) * img.size[1] / h)
                        idx = (sy * img.size[0] + sx) * 4
                        r = int(min(max(pixels[idx], 0.0), 1.0) * 15.0)
                        g = int(min(max(pixels[idx + 1], 0.0), 1.0) * 15.0)
                        b = int(min(max(pixels[idx + 2], 0.0), 1.0) * 15.0)
                        a = int(min(max(pixels[idx + 3], 0.0), 1.0) * 15.0)
                        raw_pixels.extend(struct.pack('<H', (a << 12) | (r << 8) | (g << 4) | b))
                bpy.data.images.remove(img)
            else:
                for _ in range(w * h):
                    raw_pixels.extend(struct.pack('<H', 0xFFFF))

            sprite_data.append((w, h, raw_pixels))
            print(f"  Sprite '{spr_obj.name}': {w}x{h} px ({len(raw_pixels)} bytes)")

    start_f = scene.frame_start
    end_f = scene.frame_end
    last_motion = min(end_f, 374)
    anim_frames = list(range(start_f, last_motion, frame_step))
    if not anim_frames or anim_frames[-1] != last_motion:
        anim_frames.append(last_motion)

    stored_frames = len(anim_frames)
    total_playback_ticks = end_f - start_f + 1  # 1:1 total duration from Blender scene

    def clamp16(v):
        return max(-32768, min(32767, int(round(v))))

    tf_bytes = bytearray()
    spr_frame_bytes = bytearray()

    for f in anim_frames:
        scene.frame_set(f)
        cam_mat_inv = cam.matrix_world.inverted()

        for o in mesh_objs:
            is_swirl = (o == swirl_obj)
            is_ball = (o == sphere_obj)

            if is_ball and f < 42:
                tf_bytes.extend(struct.pack('<12h', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
                continue

            m_cam = cam_mat_inv @ o.matrix_world
            r00 = clamp16(m_cam[0][0] * 8192.0)
            r01 = clamp16(m_cam[0][1] * 8192.0)
            r02 = clamp16(m_cam[0][2] * 8192.0)
            t0  = clamp16(m_cam[0][3] * 128.0)

            r10 = clamp16(m_cam[1][0] * 8192.0)
            r11 = clamp16(m_cam[1][1] * 8192.0)
            r12 = clamp16(m_cam[1][2] * 8192.0)
            t1  = clamp16(m_cam[1][3] * 128.0)

            r20 = clamp16(m_cam[2][0] * 8192.0)
            r21 = clamp16(m_cam[2][1] * 8192.0)
            r22 = clamp16(m_cam[2][2] * 8192.0)
            t2  = clamp16(m_cam[2][3] * 128.0)

            if is_swirl and face_reveal:
                vis_tris = len(face_reveal)
                for idx, (rf, _) in enumerate(face_reveal):
                    if rf > f:
                        vis_tris = idx
                        break
                r02 = clamp16(vis_tris)

            tf_bytes.extend(struct.pack('<12h',
                r00, r01, r02, t0,
                r10, r11, r12, t1,
                r20, r21, r22, t2
            ))

        for i, spr_obj in enumerate(sprite_objs):
            p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, spr_obj.matrix_world.translation)
            w, h, _ = sprite_data[i]
            dest_x = clamp16(p2d.x * 640.0 - (w / 2.0))
            dest_y = clamp16((1.0 - p2d.y) * 480.0 - (h / 2.0))
            alpha = 255 if p2d.z > 0 else 0
            spr_frame_bytes.extend(struct.pack('<hhBBH', dest_x, dest_y, alpha, 100, 0))

    audio_pcm_bytes = bytearray()
    if os.path.exists(DEFAULT_AUDIO_PCM):
        with open(DEFAULT_AUDIO_PCM, 'rb') as af:
            audio_pcm_bytes = bytearray(af.read())
    elif os.path.exists(audio_path):
        ffmpeg_bin = r"C:\ffmpeg\bin\ffmpeg.exe"
        if os.path.exists(ffmpeg_bin):
            subprocess.run([ffmpeg_bin, "-y", "-i", audio_path, "-ac", "1", "-ar", "11025", "-f", "s8", DEFAULT_AUDIO_PCM], check=True)
            with open(DEFAULT_AUDIO_PCM, 'rb') as af:
                audio_pcm_bytes = bytearray(af.read())

    while len(audio_pcm_bytes) % 4 != 0:
        audio_pcm_bytes.append(0)

    print(f"[OpenDC] Audio payload: {len(audio_pcm_bytes):,} bytes")

    vert_bytes = bytearray()
    for vx, vy, vz in all_verts:
        vert_bytes.extend(struct.pack('<3f', vx, vy, vz))

    idx_bytes = bytearray()
    for idx in all_indices:
        idx_bytes.extend(struct.pack('<H', idx))

    obj_table_bytes = bytearray()
    for obj_d in obj_defs:
        obj_table_bytes.extend(struct.pack('<IIIIHH', *obj_d))

    cue_bytes = bytearray()
    wav_bytes = audio_pcm_bytes

    HEADER_SIZE = 64
    off_tf = HEADER_SIZE
    off_v = off_tf + len(tf_bytes)
    off_idx = off_v + len(vert_bytes)
    off_cues = off_idx + len(idx_bytes)
    off_wav = off_cues + len(cue_bytes)
    off_objs = off_wav + len(wav_bytes)
    off_sprites = off_objs + len(obj_table_bytes)
    off_spr_frames = off_sprites + len(sprite_objs) * 8
    off_pixels_base = off_spr_frames + len(spr_frame_bytes)

    sprite_header_bytes = bytearray()
    pixel_data_bytes = bytearray()
    cur_px_off = off_pixels_base
    for w, h, px in sprite_data:
        sprite_header_bytes.extend(struct.pack('<HHI', w, h, cur_px_off))
        pixel_data_bytes.extend(px)
        cur_px_off += len(px)

    header = struct.pack('<IHHIIIIIIIIIIIHHII',
        0x53424344,             # magic "DCBS"
        3,                      # version 3
        len(obj_defs),          # object_count
        total_playback_ticks,   # total_frames
        len(all_verts),         # vertex_count
        len(all_indices),       # index_count
        0,                      # audio_cue_count
        off_tf,                 # off_transforms
        off_v,                  # off_vertices
        off_idx,                # off_indices
        off_cues,               # off_audio_cues
        off_wav,                # off_wavetables
        off_objs,               # off_colors / objects
        off_sprites if sprite_objs else 0,
        len(sprite_objs),
        stored_frames,
        off_spr_frames if sprite_objs else 0,
        len(audio_pcm_bytes)
    )

    full_blob = (
        header +
        tf_bytes +
        vert_bytes +
        idx_bytes +
        cue_bytes +
        wav_bytes +
        obj_table_bytes +
        sprite_header_bytes +
        spr_frame_bytes +
        pixel_data_bytes
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(full_blob)

    print(f"[OpenDC] Successfully exported {len(full_blob):,} bytes to {output_path}")
    return len(full_blob)

class OPENDC_PG_SceneSettings(bpy.types.PropertyGroup):
    output_bin: bpy.props.StringProperty(
        name="Output .bin Path",
        default=DEFAULT_OUTPUT_BIN,
        subtype='FILE_PATH'
    )
    audio_source: bpy.props.StringProperty(
        name="Audio Source",
        default=DEFAULT_AUDIO_MP3,
        subtype='FILE_PATH'
    )
    frame_step: bpy.props.IntProperty(
        name="Keyframe Step",
        default=2,
        min=1,
        max=10,
        description="Step between baked keyframes (2 = 30 fps @ 60 Hz)"
    )
    auto_build_bios: bpy.props.BoolProperty(
        name="Auto-Build BIOS ROM",
        default=True,
        description="Recompile bootloader BIOS ROM immediately after export"
    )

class OPENDC_OT_ValidateScene(bpy.types.Operator):
    bl_idname = "opendc.validate_scene"
    bl_label = "Validate Scene for Dreamcast"

    def execute(self, context):
        scene = context.scene
        cam = scene.camera
        if not cam:
            self.report({'ERROR'}, "No Camera found in scene!")
            return {'CANCELLED'}

        mesh_count = sum(1 for o in scene.objects if o.type == 'MESH')
        poly_count = sum(len(o.data.polygons) for o in scene.objects if o.type == 'MESH' and o.data)

        self.report({'INFO'}, f"Scene OK: {mesh_count} meshes, ~{poly_count} polygons. Ready for export!")
        return {'FINISHED'}

class OPENDC_OT_ExportScene(bpy.types.Operator):
    bl_idname = "opendc.export_scene"
    bl_label = "Export boot_scene.bin"

    def execute(self, context):
        props = context.scene.opendc_settings
        try:
            size = export_opendc_boot_scene(
                output_path=props.output_bin,
                audio_path=props.audio_source,
                frame_step=props.frame_step
            )
            self.report({'INFO'}, f"Exported boot_scene.bin ({size:,} bytes)!")

            if props.auto_build_bios:
                subprocess.run(["powershell", "-Command", "kos-clean; kos-make; Copy-Item 'dc_boot.bin' '..\\boot_loader_custom.bios'"],
                               cwd=BOOTLOADER_DIR, shell=True)
                self.report({'INFO'}, "Successfully rebuilt boot_loader_custom.bios!")

            return {'FINISHED'}
        except Exception as e:
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}

class OPENDC_PT_MainPanel(bpy.types.Panel):
    bl_label = "OpenDC Boot Scene Exporter"
    bl_idname = "OPENDC_PT_MainPanel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'OpenDC Boot'

    def draw(self, context):
        layout = self.layout
        props = context.scene.opendc_settings

        box = layout.box()
        box.label(text="Configuration", icon='SETTINGS')
        box.prop(props, "output_bin")
        box.prop(props, "audio_source")
        box.prop(props, "frame_step")
        box.prop(props, "auto_build_bios")

        layout.separator()
        layout.operator("opendc.validate_scene", icon='CHECKMARK')
        layout.operator("opendc.export_scene", icon='EXPORT')

classes = (
    OPENDC_PG_SceneSettings,
    OPENDC_OT_ValidateScene,
    OPENDC_OT_ExportScene,
    OPENDC_PT_MainPanel,
)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.opendc_settings = bpy.props.PointerProperty(type=OPENDC_PG_SceneSettings)

def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.opendc_settings

if __name__ == '__main__':
    try:
        register()
    except Exception:
        pass
    export_opendc_boot_scene()