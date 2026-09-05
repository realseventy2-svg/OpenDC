"""
Export Dreamcast Boot Scene (boot_scene.bin)
Compatible with Blender 3.x / 4.x
Extracts:
  - 3D Animated Meshes (Swirl, Spheres, Cubes) with direct 3x4 camera matrices
  - Vibrant RGB565 material base colors
  - 2D Bleemcast Letter Sprites (ARGB4444) with frame-by-frame 2D trajectories
"""

import bpy
import bpy_extras
import struct
import math
import os

OUTPUT_BIN = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\boot_scene.bin"
LETTERS_DIR = os.path.expanduser(r"~\Desktop\bleemcast_letters")
START_FRAME = 1
END_FRAME = 374
FRAME_STEP = 2   # 30 fps playback

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

def get_material_color(obj):
    if obj.data and obj.data.materials:
        mat = obj.data.materials[0]
        if mat and mat.use_nodes and mat.node_tree:
            for node in mat.node_tree.nodes:
                if node.type == 'BSDF_PRINCIPLED':
                    c = node.inputs['Base Color'].default_value
                    return color_to_rgb565((c[0], c[1], c[2]))
                elif node.type == 'EMISSION':
                    c = node.inputs['Color'].default_value
                    return color_to_rgb565((c[0], c[1], c[2]))
        elif mat:
            return color_to_rgb565((mat.diffuse_color[0], mat.diffuse_color[1], mat.diffuse_color[2]))
    return color_to_rgb565((1.0, 0.43, 0.08)) # Default Dreamcast Orange

def export_scene():
    scene = bpy.context.scene
    cam = scene.camera
    if not cam:
        for o in scene.objects:
            if o.type == 'CAMERA':
                cam = o
                break
    if not cam:
        raise Exception("No camera found in scene!")

    # 1. Collect 3D meshes (exclude sprite plane objects)
    mesh_objs = []
    for o in scene.objects:
        if o.type == 'MESH' and not o.name.startswith('Sprite_Text'):
            if len(o.data.vertices) > 0 and len(o.data.polygons) > 0:
                mesh_objs.append(o)
    
    # Sort with Swirl and Spheres first for consistent ordering
    mesh_objs.sort(key=lambda o: (0 if 'Swirl' in o.name else (1 if 'Sphere' in o.name else 2), o.name))
    print(f"Found {len(mesh_objs)} 3D mesh objects")

    # 2. Collect 2D sprite objects
    sprite_objs = [o for o in scene.objects if o.name.startswith('Sprite_Text')]
    sprite_objs.sort(key=lambda o: o.name)
    print(f"Found {len(sprite_objs)} 2D sprite objects")

    # 3. Load letter PNGs and determine native sprite dimensions
    # Sprite resting dimensions at 640x480 screen resolution
    scene.frame_set(250)
    sprite_data = [] # (width, height, argb4444_bytes)

    for i, spr_obj in enumerate(sprite_objs):
        # Calculate screen bounding box at frame 250
        mat = spr_obj.matrix_world
        coords_2d = []
        for v in spr_obj.data.vertices:
            wco = mat @ v.co
            p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, wco)
            coords_2d.append((p2d.x * 640.0, (1.0 - p2d.y) * 480.0))
        w = max(1, int(round(max(c[0] for c in coords_2d) - min(c[0] for c in coords_2d))))
        h = max(1, int(round(max(c[1] for c in coords_2d) - min(c[1] for c in coords_2d))))

        png_candidates = [
            os.path.join(LETTERS_DIR, f"letter_{i:02d}_{spr_obj.name.replace('Sprite_', '')}.png"),
            os.path.join(LETTERS_DIR, f"letter_{i:02d}_{spr_obj.name}.png"),
        ]
        png_path = None
        for p in png_candidates:
            if os.path.exists(p):
                png_path = p
                break
        if not png_path and os.path.exists(LETTERS_DIR):
            png_files = sorted([f for f in os.listdir(LETTERS_DIR) if f.startswith(f"letter_{i:02d}") and f.endswith(".png")])
            if png_files:
                png_path = os.path.join(LETTERS_DIR, png_files[0])

        if not png_path:
            raise Exception(f"Letter PNG for {spr_obj.name} not found in {LETTERS_DIR}")

        img = bpy.data.images.load(png_path)
        img.scale(w, h)
        pixels = list(img.pixels) # floats 0..1, (r, g, b, a) bottom-to-top
        
        # Convert to ARGB4444 row-by-row (top-to-bottom for framebuffer blit)
        raw_pixels = bytearray()
        for y in range(h - 1, -1, -1):
            for x in range(w):
                idx = (y * w + x) * 4
                r = int(min(max(pixels[idx + 0], 0.0), 1.0) * 15.0)
                g = int(min(max(pixels[idx + 1], 0.0), 1.0) * 15.0)
                b = int(min(max(pixels[idx + 2], 0.0), 1.0) * 15.0)
                a = int(min(max(pixels[idx + 3], 0.0), 1.0) * 15.0)
                val16 = (a << 12) | (r << 8) | (g << 4) | b
                raw_pixels.extend(struct.pack('<H', val16))
        
        bpy.data.images.remove(img)
        sprite_data.append((w, h, raw_pixels))
        print(f"  Sprite {i} ({spr_obj.name}): {w}x{h} ({len(raw_pixels)} bytes)")

    # 4. Extract 3D geometry
    vert_bytes = bytearray()
    idx_bytes = bytearray()
    obj_defs = [] # (start_vert, vert_count, start_tri, tri_count, color, flags)

    total_verts = 0
    total_tris = 0

    for o in mesh_objs:
        me = o.data
        start_v = total_verts
        v_count = len(me.vertices)
        for v in me.vertices:
            vert_bytes.extend(struct.pack('<3f', v.co.x, v.co.y, v.co.z))
        total_verts += v_count

        start_t = total_tris
        t_count = 0
        for p in me.polygons:
            if len(p.vertices) == 3:
                idx_bytes.extend(struct.pack('<3H', p.vertices[0], p.vertices[1], p.vertices[2]))
                t_count += 1
            elif len(p.vertices) == 4:
                idx_bytes.extend(struct.pack('<3H', p.vertices[0], p.vertices[1], p.vertices[2]))
                idx_bytes.extend(struct.pack('<3H', p.vertices[0], p.vertices[2], p.vertices[3]))
                t_count += 2
        total_tris += t_count

        color = get_material_color(o)
        obj_defs.append((start_v, v_count, start_t, t_count, color, 0))

    # 5. Extract animation frames
    frames = list(range(START_FRAME, END_FRAME + 1, FRAME_STEP))
    num_frames = len(frames)
    print(f"Exporting {num_frames} frames ({START_FRAME}..{END_FRAME}, step {FRAME_STEP})...")

    tf_bytes = bytearray()
    spr_frame_bytes = bytearray()

    for f in frames:
        scene.frame_set(f)
        cam_mat_inv = cam.matrix_world.inverted()

        # 3D Transforms (12 floats per object = 3x4 row-major local-to-camera matrix)
        for o in mesh_objs:
            local_to_cam = cam_mat_inv @ o.matrix_world
            # Extract 3x4: [row0, row1, row2]
            # row 0: (m00, m01, m02, tx)
            # row 1: (m10, m11, m12, ty)
            # row 2: (m20, m21, m22, tz)
            tf_bytes.extend(struct.pack('<12f',
                local_to_cam[0][0], local_to_cam[0][1], local_to_cam[0][2], local_to_cam[0][3],
                local_to_cam[1][0], local_to_cam[1][1], local_to_cam[1][2], local_to_cam[1][3],
                local_to_cam[2][0], local_to_cam[2][1], local_to_cam[2][2], local_to_cam[2][3]
            ))

        # 2D Sprite Transforms (x, y, alpha, scale, flags)
        for i, spr_obj in enumerate(sprite_objs):
            p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, spr_obj.matrix_world.translation)
            w, h, _ = sprite_data[i]
            # Center sprite on projected point
            dest_x = int(round(p2d.x * 640.0 - (w / 2.0)))
            dest_y = int(round((1.0 - p2d.y) * 480.0 - (h / 2.0)))
            alpha = 255 if p2d.z > 0 else 0
            spr_frame_bytes.extend(struct.pack('<hhBBH', dest_x, dest_y, alpha, 100, 0))

    # 6. Audio cues & Wavetables
    cue_bytes = bytearray() # Empty for now
    wav_bytes = bytearray(2048) # 4 wavetables × 512 bytes

    # 7. Assemble binary layout
    # Header: 64 bytes
    HEADER_SIZE = 64
    off_tf = HEADER_SIZE
    off_v = off_tf + len(tf_bytes)
    off_idx = off_v + len(vert_bytes)
    off_cues = off_idx + len(idx_bytes)
    off_wav = off_cues + len(cue_bytes)
    off_objs = off_wav + len(wav_bytes)
    
    obj_table_bytes = bytearray()
    for obj_d in obj_defs:
        obj_table_bytes.extend(struct.pack('<IIIIHH', *obj_d))
    
    off_sprites = off_objs + len(obj_table_bytes)
    
    # Sprite headers table (8 bytes each)
    sprite_header_bytes = bytearray()
    # Pixel data will be placed after sprite frames table
    off_spr_frames = off_sprites + len(sprite_objs) * 8
    off_pixels_base = off_spr_frames + len(spr_frame_bytes)

    pixel_data_bytes = bytearray()
    current_px_off = off_pixels_base
    for w, h, px in sprite_data:
        sprite_header_bytes.extend(struct.pack('<HHI', w, h, current_px_off))
        pixel_data_bytes.extend(px)
        current_px_off += len(px)

    header = struct.pack('<IHHIIIIIIIIIIIHHII',
        0x53424344,          # magic "DCBS"
        2,                   # version 2
        len(mesh_objs),      # object_count
        num_frames,          # total_frames
        total_verts,         # vertex_count
        total_tris * 3,      # index_count
        0,                   # audio_cue_count
        off_tf,              # off_transforms
        off_v,               # off_vertices
        off_idx,             # off_indices
        off_cues,            # off_audio_cues
        off_wav,             # off_wavetables
        off_objs,            # off_colors (points to BootSceneObject array)
        off_sprites,         # off_sprites
        len(sprite_objs),    # sprite_count
        0,                   # reserved_h
        off_spr_frames,      # off_sprite_frames
        0                    # reserved
    )

    assert len(header) == 64, f"Header size mismatch: {len(header)} != 64"

    os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)
    with open(OUTPUT_BIN, 'wb') as f:
        f.write(header)
        f.write(tf_bytes)
        f.write(vert_bytes)
        f.write(idx_bytes)
        f.write(cue_bytes)
        f.write(wav_bytes)
        f.write(obj_table_bytes)
        f.write(sprite_header_bytes)
        f.write(spr_frame_bytes)
        f.write(pixel_data_bytes)

    total_sz = os.path.getsize(OUTPUT_BIN)
    print(f"SUCCESS: Exported {total_sz:,} bytes to {OUTPUT_BIN}")

if __name__ == '__main__':
    export_scene()
