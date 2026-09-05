"""
Export Dreamcast Boot Scene (boot_scene.bin)
Version 3: High-Performance Solid Mesh + Progressive Spiral Swirl + ARGB4444 2D Sprites + 11kHz AICA Boot Audio
"""

import bpy
import bmesh
import bpy_extras
import struct
import math
import os
import subprocess

OUTPUT_BIN = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\boot_scene.bin"
LETTERS_DIR = os.path.expanduser(r"~\Desktop\bleemcast_letters")
AUDIO_MP3 = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\res\boot.mp3"
AUDIO_PCM = r"d:\Github\Personal\KallistiOS\projects\OpenDC\bootloader\res\boot_11k.pcm"
FRAME_STEP = 2   # 30 fps keyframe stepping (1/2 rate of 60 Hz timeline)

def linear_to_srgb(c):
    c = max(c, 0.0)
    if c <= 0.0031308:
        return c * 12.92
    return 1.055 * math.pow(c, 1.0 / 2.4) - 0.055

def color_to_rgb565(col):
    # Apply standard studio/filmic exposure mapping to match Blender rendered viewport tone
    r_srgb = linear_to_srgb(col[0]) * 0.85
    g_srgb = linear_to_srgb(col[1]) * 0.83
    b_srgb = linear_to_srgb(col[2]) * 0.90
    r = int(min(max(r_srgb, 0.0), 1.0) * 31.0)
    g = int(min(max(g_srgb, 0.0), 1.0) * 63.0)
    b = int(min(max(b_srgb, 0.0), 1.0) * 31.0)
    return (r << 11) | (g << 5) | b

def get_object_color(obj, fallback=(0.973, 0.109, 0.053)):
    if obj and obj.data and hasattr(obj.data, 'materials') and len(obj.data.materials) > 0:
        mat = obj.data.materials[0]
        if mat:
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
    if obj and hasattr(obj, 'color') and any(c < 0.99 for c in obj.color[:3]):
        return (obj.color[0], obj.color[1], obj.color[2])
    return fallback

def export_boot_scene():
    scene = bpy.context.scene
    cam = scene.camera
    if not cam:
        for o in scene.objects:
            if o.type == 'CAMERA':
                cam = o
                break
    if not cam:
        raise Exception("No camera found in scene!")

    swirl = bpy.data.objects.get('Swirl')
    sphere = bpy.data.objects.get('Sphere.002')
    if not sphere:
        sphere = bpy.data.objects.get('Sphere.001')
    if not swirl or not sphere:
        raise Exception("Required 3D objects 'Swirl' and 'Sphere.002' not found!")

    swirl_mat_inv = swirl.matrix_world.inverted()

    # 1. Record Ball trajectory in swirl local coordinate space (frames 316..374)
    ball_pts = []
    for f in range(316, 375):
        scene.frame_set(f)
        p_local = swirl_mat_inv @ sphere.matrix_world.translation
        ball_pts.append((f, p_local.x, p_local.y))

    # 2. Triangulate Swirl and sort faces along spiral curve from inner center to outer tail
    bm_swirl = bmesh.new()
    bm_swirl.from_mesh(swirl.data)
    bmesh.ops.triangulate(bm_swirl, faces=bm_swirl.faces, quad_method='BEAUTY', ngon_method='BEAUTY')

    face_reveal = []
    for f in bm_swirl.faces:
        c = f.calc_center_median()
        best_dist = 1e9
        best_frame = 316
        for frame, bx, by in ball_pts:
            d = (c.x - bx)**2 + (c.y - by)**2
            if d < best_dist:
                best_dist = d
                best_frame = frame
        face_reveal.append((best_frame, f))

    # Sort faces chronologically by reveal frame
    face_reveal.sort(key=lambda item: item[0])

    swirl_verts = []
    swirl_vert_map = {}
    swirl_indices = []

    for rev_frame, f in face_reveal:
        tri_idx = []
        for v in f.verts:
            if v not in swirl_vert_map:
                swirl_vert_map[v] = len(swirl_verts)
                swirl_verts.append((v.co.x, v.co.y, v.co.z, v.normal.x, v.normal.y, v.normal.z))
            tri_idx.append(swirl_vert_map[v])
        swirl_indices.extend(tri_idx)

    bm_swirl.free()
    print(f"Swirl: {len(swirl_verts)} vertices, {len(face_reveal)} triangles.")

    # 3. High-quality smooth sphere geometry optimized for 60 FPS SSAA
    mod_dec = sphere.modifiers.new('OpenDC_Decimate', 'DECIMATE')
    mod_dec.ratio = 0.16
    depsgraph = bpy.context.evaluated_depsgraph_get()
    sphere_eval = sphere.evaluated_get(depsgraph)
    me_sphere = sphere_eval.to_mesh()
    sphere.modifiers.remove(mod_dec)
    me_sphere.calc_loop_triangles()

    sphere_verts = [(v.co.x, v.co.y, v.co.z, v.normal.x, v.normal.y, v.normal.z) for v in me_sphere.vertices]
    sphere_indices = []
    for tri in me_sphere.loop_triangles:
        sphere_indices.extend([tri.vertices[0], tri.vertices[1], tri.vertices[2]])

    print(f"Sphere: {len(sphere_verts)} vertices, {len(me_sphere.loop_triangles)} triangles.")

    # 4. Collect 2D sprite letter planes
    sprite_objs = [o for o in scene.objects if o.name.startswith('Sprite_Text')]
    sprite_objs.sort(key=lambda o: o.name)
    print(f"Found {len(sprite_objs)} 2D sprite objects")

    # Load letter PNGs and rasterize to ARGB4444
    scene.frame_set(250)
    sprite_data = [] # (width, height, argb4444_bytes)

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
        src_w, src_h = img.size[0], img.size[1]
        src_pixels = list(img.pixels)
        bpy.data.images.remove(img)

        # High-quality sub-pixel area-averaging downsampler (anti-aliased vector text edges)
        raw_pixels = bytearray()
        scale_x = src_w / float(w)
        scale_y = src_h / float(h)

        for dy in range(h - 1, -1, -1):
            sy_start = dy * scale_y
            sy_end = (dy + 1) * scale_y
            iy_start = int(math.floor(sy_start))
            iy_end = min(int(math.ceil(sy_end)), src_h)

            for dx in range(w):
                sx_start = dx * scale_x
                sx_end = (dx + 1) * scale_x
                ix_start = int(math.floor(sx_start))
                ix_end = min(int(math.ceil(sx_end)), src_w)

                tot_weight = 0.0
                sum_r = 0.0; sum_g = 0.0; sum_b = 0.0; sum_a = 0.0

                for iy in range(iy_start, iy_end):
                    wy = min(sy_end, iy + 1.0) - max(sy_start, float(iy))
                    if wy <= 0: continue
                    for ix in range(ix_start, ix_end):
                        wx = min(sx_end, ix + 1.0) - max(sx_start, float(ix))
                        if wx <= 0: continue
                        weight = wx * wy
                        idx = (iy * src_w + ix) * 4
                        sum_r += src_pixels[idx + 0] * weight
                        sum_g += src_pixels[idx + 1] * weight
                        sum_b += src_pixels[idx + 2] * weight
                        sum_a += src_pixels[idx + 3] * weight
                        tot_weight += weight

                inv_w = 1.0 / tot_weight if tot_weight > 0 else 0.0
                r = int(min(max(sum_r * inv_w, 0.0), 1.0) * 15.0 + 0.5)
                g = int(min(max(sum_g * inv_w, 0.0), 1.0) * 15.0 + 0.5)
                b = int(min(max(sum_b * inv_w, 0.0), 1.0) * 15.0 + 0.5)
                a = int(min(max(sum_a * inv_w, 0.0), 1.0) * 15.0 + 0.5)
                val16 = (a << 12) | (r << 8) | (g << 4) | b
                raw_pixels.extend(struct.pack('<H', val16))

        sprite_data.append((w, h, raw_pixels))
        print(f"  Sprite {i:02d} ({spr_obj.name}): {w}x{h} px ({len(raw_pixels)} bytes)")

    # 5. Pack Geometry (Swirl = Object 0, Sphere = Object 1)
    vert_bytes = bytearray()
    idx_bytes = bytearray()
    
    # Swirl vertices & indices
    start_v_swirl = 0
    v_count_swirl = len(swirl_verts)
    for vx, vy, vz, nx, ny, nz in swirl_verts:
        vert_bytes.extend(struct.pack('<6f', vx, vy, vz, nx, ny, nz))
    start_t_swirl = 0
    t_count_swirl = len(face_reveal)
    for idx in swirl_indices:
        idx_bytes.extend(struct.pack('<H', idx))

    # Sphere vertices & indices
    start_v_sphere = v_count_swirl
    v_count_sphere = len(sphere_verts)
    for vx, vy, vz, nx, ny, nz in sphere_verts:
        vert_bytes.extend(struct.pack('<6f', vx, vy, vz, nx, ny, nz))
    start_t_sphere = t_count_swirl
    t_count_sphere = len(sphere_indices) // 3
    for idx in sphere_indices:
        idx_bytes.extend(struct.pack('<H', idx))

    swirl_col_raw = get_object_color(swirl, fallback=(0.973, 0.109, 0.053))
    sphere_col_raw = swirl_col_raw

    swirl_color = color_to_rgb565(swirl_col_raw)
    sphere_color = color_to_rgb565(sphere_col_raw)
    print(f"Material Colors: Swirl=0x{swirl_color:04X} (linear {swirl_col_raw[:3]}), Sphere=0x{sphere_color:04X}")

    # Object Table: (start_vert, vert_count, start_tri, tri_count, color, flags)
    # Swirl has flags=1 (progressive reveal), Sphere has flags=0
    obj_defs = [
        (start_v_swirl, v_count_swirl, start_t_swirl, t_count_swirl, swirl_color, 1),
        (start_v_sphere, v_count_sphere, start_t_sphere, t_count_sphere, sphere_color, 0),
    ]

    # 6. Extract Animation Keyframes (Ensure exact Frame 374 is the final hold keyframe)
    start_f = scene.frame_start
    end_f = scene.frame_end
    last_motion_frame = min(end_f, 374)
    anim_frames = list(range(start_f, last_motion_frame, FRAME_STEP))
    if not anim_frames or anim_frames[-1] != last_motion_frame:
        anim_frames.append(last_motion_frame)

    stored_frames = len(anim_frames)
    total_playback_ticks = end_f - start_f + 1  # 1:1 total duration from Blender scene
    print(f"Exporting {stored_frames} keyframes (duration: {total_playback_ticks} ticks / frames from Blender scene)...")

    tf_bytes = bytearray()
    spr_frame_bytes = bytearray()

    for f in anim_frames:
        scene.frame_set(f)
        cam_mat_inv = cam.matrix_world.inverted()

        # --- Object 0: Swirl ---
        m_swirl = cam_mat_inv @ swirl.matrix_world
        r00 = int(round(m_swirl[0][0] * 8192.0))
        r01 = int(round(m_swirl[0][1] * 8192.0))
        t0  = int(round(m_swirl[0][3] * 128.0))

        r10 = int(round(m_swirl[1][0] * 8192.0))
        r11 = int(round(m_swirl[1][1] * 8192.0))
        r12 = int(round(m_swirl[1][2] * 8192.0))
        t1  = int(round(m_swirl[1][3] * 128.0))

        r20 = int(round(m_swirl[2][0] * 8192.0))
        r21 = int(round(m_swirl[2][1] * 8192.0))
        r22 = int(round(m_swirl[2][2] * 8192.0))
        t2  = int(round(m_swirl[2][3] * 128.0))

        # Dynamic Reveal Count for Swirl
        if f < 316:
            visible_tris = 0
        elif f >= 374:
            visible_tris = t_count_swirl
        else:
            visible_tris = sum(1 for rev_frame, _ in face_reveal if rev_frame <= f)

        r02 = visible_tris  # Stored in tf[2] for progressive reveal

        tf_bytes.extend(struct.pack('<12h',
            r00, r01, r02, t0,
            r10, r11, r12, t1,
            r20, r21, r22, t2
        ))

        # --- Object 1: Sphere (Rolling Ball & Rounded Tail Cap) ---
        if f < 42:
            # Sphere is hidden before dropping
            tf_bytes.extend(struct.pack('<12h', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
        else:
            # Sphere is active during bounce, spiral trace, and remains as the rounded tail cap
            m_sph = cam_mat_inv @ sphere.matrix_world
            sr00 = int(round(m_sph[0][0] * 8192.0))
            sr01 = int(round(m_sph[0][1] * 8192.0))
            sr02 = int(round(m_sph[0][2] * 8192.0))
            st0  = int(round(m_sph[0][3] * 128.0))

            sr10 = int(round(m_sph[1][0] * 8192.0))
            sr11 = int(round(m_sph[1][1] * 8192.0))
            sr12 = int(round(m_sph[1][2] * 8192.0))
            st1  = int(round(m_sph[1][3] * 128.0))

            sr20 = int(round(m_sph[2][0] * 8192.0))
            sr21 = int(round(m_sph[2][1] * 8192.0))
            sr22 = int(round(m_sph[2][2] * 8192.0))
            st2  = int(round(m_sph[2][3] * 128.0))

            tf_bytes.extend(struct.pack('<12h',
                sr00, sr01, sr02, st0,
                sr10, sr11, sr12, st1,
                sr20, sr21, sr22, st2
            ))

        # --- 2D Sprites ---
        for i, spr_obj in enumerate(sprite_objs):
            p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, spr_obj.matrix_world.translation)
            w, h, _ = sprite_data[i]
            dest_x = int(round(p2d.x * 640.0 - (w / 2.0)))
            dest_y = int(round((1.0 - p2d.y) * 480.0 - (h / 2.0)))
            alpha = 255 if p2d.z > 0 else 0
            spr_frame_bytes.extend(struct.pack('<hhBBH', dest_x, dest_y, alpha, 100, 0))

    # 7. Load or Convert Audio Sample (11,025 Hz 8-bit Signed PCM)
    audio_pcm_bytes = bytearray()
    if os.path.exists(AUDIO_PCM):
        with open(AUDIO_PCM, 'rb') as af:
            audio_pcm_bytes = bytearray(af.read())
    elif os.path.exists(AUDIO_MP3):
        # Auto-convert with ffmpeg
        ffmpeg_bin = r"C:\ffmpeg\bin\ffmpeg.exe"
        if os.path.exists(ffmpeg_bin):
            subprocess.run([ffmpeg_bin, "-y", "-i", AUDIO_MP3, "-ac", "1", "-ar", "11025", "-f", "s8", AUDIO_PCM], check=True)
            with open(AUDIO_PCM, 'rb') as af:
                audio_pcm_bytes = bytearray(af.read())

    # Pad audio PCM to multiple of 4 bytes
    while len(audio_pcm_bytes) % 4 != 0:
        audio_pcm_bytes.append(0)

    print(f"Embedded Boot Audio: {len(audio_pcm_bytes):,} bytes (11,025 Hz 8-bit signed PCM)")

    # 8. Assemble binary container
    cue_bytes = bytearray()
    wav_bytes = audio_pcm_bytes

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
    
    sprite_header_bytes = bytearray()
    off_spr_frames = off_sprites + len(sprite_objs) * 8
    off_pixels_base = off_spr_frames + len(spr_frame_bytes)

    pixel_data_bytes = bytearray()
    current_px_off = off_pixels_base
    for w, h, px in sprite_data:
        sprite_header_bytes.extend(struct.pack('<HHI', w, h, current_px_off))
        pixel_data_bytes.extend(px)
        current_px_off += len(px)

    total_verts = len(swirl_verts) + len(sphere_verts)
    total_tris = t_count_swirl + t_count_sphere

    header = struct.pack('<IHHIIIIIIIIIIIHHII',
        0x53424344,          # magic "DCBS"
        3,                   # version 3
        len(obj_defs),       # object_count = 2
        total_playback_ticks,# total_frames (300 ticks)
        total_verts,         # vertex_count
        total_tris * 3,      # index_count
        0,                   # audio_cue_count
        off_tf,              # off_transforms
        off_v,               # off_vertices
        off_idx,             # off_indices
        off_cues,            # off_audio_cues
        off_wav,             # off_wavetables (PCM audio data)
        off_objs,            # off_objects
        off_sprites,         # off_sprites
        len(sprite_objs),    # sprite_count
        stored_frames,       # stored_frames (188)
        off_spr_frames,      # off_sprite_frames
        len(audio_pcm_bytes) # audio_sample_bytes
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

    os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)
    with open(OUTPUT_BIN, 'wb') as f:
        f.write(full_blob)

    print(f"Successfully exported {len(full_blob):,} bytes to {OUTPUT_BIN}")

if __name__ == '__main__':
    export_boot_scene()
