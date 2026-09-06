"""
DCBS Tool - Universal Dreamcast Boot Scene Exporter (boot_scene.bin)
Version 3.0 (Dynamic 3D Geometry + 2D ARGB4444 Sprites + 11kHz AICA PCM Audio)

Directory Structure:
  tools/dcbs-tool/
    ├── export_dcbs.py
    └── res/
        ├── audio/
        ├── letters/
        └── scenes/
"""

import bpy
import bmesh
import bpy_extras
import struct
import math
import os
import subprocess
import ctypes
from ctypes import wintypes

def rasterize_glyph_gdi(char_text, font_name="Tahoma", font_size=64):
    """
    Rasterize a text character using Windows GDI with sub-pixel antialiasing.
    Falls back to Windows system fonts ('Tahoma' -> 'Arial') if custom font fails.
    """
    gdi32 = ctypes.windll.gdi32
    user32 = ctypes.windll.user32

    def render_attempt(fname):
        hdc = user32.GetDC(0)
        mem_dc = gdi32.CreateCompatibleDC(hdc)

        w = max(font_size * 2, 64)
        h = max(font_size * 2, 64)

        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [
                ("biSize", wintypes.DWORD),
                ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD),
                ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD),
                ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD),
            ]

        class BITMAPINFO(ctypes.Structure):
            _fields_ = [
                ("bmiHeader", BITMAPINFOHEADER),
                ("bmiColors", wintypes.DWORD * 3),
            ]

        bmi = BITMAPINFO()
        bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmi.bmiHeader.biWidth = w
        bmi.bmiHeader.biHeight = -h  # top-down
        bmi.bmiHeader.biPlanes = 1
        bmi.bmiHeader.biBitCount = 32
        bmi.bmiHeader.biCompression = 0

        p_bits = ctypes.c_void_p()
        hbitmap = gdi32.CreateDIBSection(mem_dc, ctypes.byref(bmi), 0, ctypes.byref(p_bits), None, 0)
        gdi32.SelectObject(mem_dc, hbitmap)

        rect = wintypes.RECT(0, 0, w, h)
        brush = gdi32.CreateSolidBrush(0x00000000)
        user32.FillRect(mem_dc, ctypes.byref(rect), brush)
        gdi32.DeleteObject(brush)

        hfont = gdi32.CreateFontW(
            -font_size, 0, 0, 0, 700,
            0, 0, 0, 1, 0, 0, 4, 0, fname
        )
        gdi32.SelectObject(mem_dc, hfont)
        gdi32.SetBkMode(mem_dc, 1) # TRANSPARENT
        gdi32.SetTextColor(mem_dc, 0x00FFFFFF)

        user32.DrawTextW(mem_dc, char_text, len(char_text), ctypes.byref(rect), 0x00000001 | 0x00000004) # DT_CENTER | DT_VCENTER

        buf = (ctypes.c_uint8 * (w * h * 4)).from_address(p_bits.value)
        raw_data = bytes(buf)

        gdi32.DeleteObject(hfont)
        gdi32.DeleteObject(hbitmap)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(0, hdc)

        pixels = []
        min_x, max_x, min_y, max_y = w, 0, h, 0
        for y in range(h):
            for x in range(w):
                idx = (y * w + x) * 4
                b, g, r, _ = raw_data[idx:idx+4]
                cov = (r * 299 + g * 587 + b * 114) / (1000.0 * 255.0)
                if cov > 0.05:
                    if x < min_x: min_x = x
                    if x > max_x: max_x = x
                    if y < min_y: min_y = y
                    if y > max_y: max_y = y
                pixels.append(cov)

        if min_x <= max_x and min_y <= max_y:
            return w, h, pixels, min_x, max_x, min_y, max_y
        return None

    # Try custom font first, then fallback to system fonts (Tahoma, Arial)
    res = render_attempt(font_name)
    if not res and font_name.lower() != "tahoma":
        res = render_attempt("Tahoma")
    if not res and font_name.lower() != "arial":
        res = render_attempt("Arial")
    return res

def get_paths():
    tool_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    res_dir = os.path.join(tool_dir, "res")
    # Bootloader root is parent of 'tools'
    tools_dir = os.path.dirname(tool_dir)
    bootloader_dir = os.path.dirname(tools_dir)
    output_bin = os.path.join(bootloader_dir, "boot_scene.bin")
    return tool_dir, res_dir, bootloader_dir, output_bin

TOOL_DIR, TOOL_RES_DIR, BOOTLOADER_DIR, OUTPUT_BIN = get_paths()
FRAME_STEP = 2  # 30 fps keyframe stepping (1/2 rate of 60 Hz timeline)

def fix_system_fonts():
    """Remap relative Windows font paths to C:\\Windows\\Fonts so CLI background mode renders identical to GUI."""
    win_fonts_dir = r"C:\Windows\Fonts"
    if not os.path.exists(win_fonts_dir):
        return
    for fnt in bpy.data.fonts:
        if not os.path.exists(bpy.path.abspath(fnt.filepath)):
            fn = os.path.basename(fnt.filepath)
            win_font = os.path.join(win_fonts_dir, fn)
            if os.path.exists(win_font):
                fnt.filepath = win_font

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

def get_object_color(obj, fallback=(0.10, 0.75, 0.20)):
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

def find_audio_pcm(blend_dir):
    """Detect or convert audio file (.wav / .mp3 / .pcm) into 11,025 Hz 8-bit signed PCM."""
    candidates = [
        os.path.join(blend_dir, "boot.pcm"),
        os.path.join(blend_dir, "audio.pcm"),
        os.path.join(blend_dir, "boot.wav"),
        os.path.join(blend_dir, "audio.wav"),
        os.path.join(blend_dir, "boot.mp3"),
        os.path.join(blend_dir, "audio.mp3"),
        os.path.join(TOOL_RES_DIR, "audio", "boot_11k.pcm"),
        os.path.join(TOOL_RES_DIR, "audio", "boot.pcm"),
        os.path.join(TOOL_RES_DIR, "audio", "boot.wav"),
        os.path.join(TOOL_RES_DIR, "audio", "boot.mp3"),
        os.path.join(BOOTLOADER_DIR, "res", "boot_11k.pcm"),
        os.path.join(BOOTLOADER_DIR, "res", "boot.mp3"),
    ]

    for p in candidates:
        if os.path.exists(p):
            if p.endswith(".pcm"):
                with open(p, 'rb') as f:
                    return bytearray(f.read())
            elif p.endswith(".mp3") or p.endswith(".wav"):
                # Convert using ffmpeg if available
                out_pcm = os.path.join(blend_dir, "_temp_boot_11k.pcm")
                ffmpeg_bin = r"C:\ffmpeg\bin\ffmpeg.exe"
                if not os.path.exists(ffmpeg_bin):
                    ffmpeg_bin = "ffmpeg"
                try:
                    subprocess.run([ffmpeg_bin, "-y", "-i", p, "-ac", "1", "-ar", "11025", "-f", "s8", out_pcm],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
                    if os.path.exists(out_pcm):
                        with open(out_pcm, 'rb') as f:
                            data = bytearray(f.read())
                        try: os.remove(out_pcm)
                        except: pass
                        return data
                except Exception:
                    pass

    return bytearray()

def export_dcbs():
    fix_system_fonts()

    scene = bpy.context.scene
    cam = scene.camera
    if not cam:
        for o in scene.objects:
            if o.type == 'CAMERA':
                cam = o
                break
    if not cam:
        raise Exception("No active Camera found in Blender scene!")

    blend_filepath = bpy.data.filepath
    blend_dir = os.path.dirname(blend_filepath) if blend_filepath else TOOL_DIR

    print(f"=======================================================")
    print(f"  DCBS Universal Exporter (Blender -> Dreamcast)")
    print(f"  Scene: '{scene.name}', Timeline: [{scene.frame_start}..{scene.frame_end}]")
    print(f"=======================================================")

    # 1. Discover 3D Objects & 2D Sprites
    all_meshes = [o for o in scene.objects if o.type == 'MESH' and not o.name.startswith('Plane') and not o.name.startswith('Cube')]
    
    sprite_objs = [o for o in scene.objects if o.name.startswith('Sprite_')]
    sprite_objs.sort(key=lambda o: o.name)
    if not sprite_objs:
        font_objs = [o for o in scene.objects if o.type == 'FONT']
        if font_objs:
            sprite_objs = sorted(font_objs, key=lambda o: o.name)

    geom_objs = []
    geom_col = bpy.data.collections.get('3D_Objects') or bpy.data.collections.get('Geometry') or bpy.data.collections.get('Boot_Objects')
    if geom_col:
        geom_objs = [o for o in geom_col.objects if o.type == 'MESH']
    else:
        sprite_names = set(o.name for o in sprite_objs)
        has_sphere2 = any(o.name == 'Sphere.002' for o in all_meshes)
        for o in all_meshes:
            if o.name in sprite_names: continue
            if has_sphere2 and o.name == 'Sphere.001': continue
            geom_objs.append(o)
        
        def geom_sort_key(o):
            if 'Swirl' in o.name: return (0, o.name)
            if 'Sphere' in o.name: return (1, o.name)
            if 'Logo' in o.name: return (2, o.name)
            return (3, o.name)
        geom_objs.sort(key=geom_sort_key)

    if not geom_objs and all_meshes:
        geom_objs = all_meshes

    print(f"[1/4] 3D Objects ({len(geom_objs)}): {[o.name for o in geom_objs]}")
    print(f"[2/4] 2D Sprites ({len(sprite_objs)}): {[o.name for o in sprite_objs]}")

    # 2. Extract 3D Geometry
    vert_bytes = bytearray()
    idx_bytes = bytearray()
    obj_defs = []
    total_vert_count = 0
    total_tri_count = 0
    progressive_faces_map = {}
    swirl_col_565 = None

    for o_idx, obj in enumerate(geom_objs):
        bm = bmesh.new()
        mod_dec = None
        if len(obj.data.vertices) > 2000 and 'Sphere' in obj.name:
            mod_dec = obj.modifiers.new('Decimate_Temp', 'DECIMATE')
            mod_dec.ratio = 0.16

        depsgraph = bpy.context.evaluated_depsgraph_get()
        eval_obj = obj.evaluated_get(depsgraph)
        me = eval_obj.to_mesh()
        if mod_dec:
            obj.modifiers.remove(mod_dec)

        bm.from_mesh(me)
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')

        is_progressive = (o_idx == 0 and ('Swirl' in obj.name or obj.get('progressive_reveal', False)))
        faces_sorted = []

        if is_progressive:
            sphere_guide = bpy.data.objects.get('Sphere.002') or bpy.data.objects.get('Sphere.001')
            if sphere_guide:
                obj_mat_inv = obj.matrix_world.inverted()
                ball_pts = []
                for f in range(316, 375):
                    scene.frame_set(f)
                    p_local = obj_mat_inv @ sphere_guide.matrix_world.translation
                    ball_pts.append((f, p_local.x, p_local.y))

                for f in bm.faces:
                    c = f.calc_center_median()
                    best_dist = 1e9
                    best_frame = 316
                    for frame, bx, by in ball_pts:
                        d = (c.x - bx)**2 + (c.y - by)**2
                        if d < best_dist:
                            best_dist = d
                            best_frame = frame
                    faces_sorted.append((best_frame, f))
                faces_sorted.sort(key=lambda item: item[0])
            else:
                faces_sorted = [(0, f) for f in bm.faces]
            progressive_faces_map[o_idx] = faces_sorted
        else:
            faces_sorted = [(0, f) for f in bm.faces]

        obj_verts = []
        obj_vert_map = {}
        obj_indices = []

        for rev_frame, f in faces_sorted:
            tri_idx = []
            for v in f.verts:
                if v not in obj_vert_map:
                    obj_vert_map[v] = len(obj_verts)
                    obj_verts.append((v.co.x, v.co.y, v.co.z, v.normal.x, v.normal.y, v.normal.z))
                tri_idx.append(obj_vert_map[v])
            obj_indices.extend(tri_idx)

        bm.free()
        eval_obj.to_mesh_clear()

        start_v = total_vert_count
        v_count = len(obj_verts)
        start_t = total_tri_count
        t_count = len(obj_indices) // 3

        for vx, vy, vz, nx, ny, nz in obj_verts:
            vert_bytes.extend(struct.pack('<6f', vx, vy, vz, nx, ny, nz))
        for idx in obj_indices:
            idx_bytes.extend(struct.pack('<H', idx))

        raw_color = get_object_color(obj)
        color_565 = color_to_rgb565(raw_color)
        if 'Swirl' in obj.name:
            swirl_col_565 = color_565
        elif 'Sphere' in obj.name and swirl_col_565 is not None:
            color_565 = swirl_col_565

        flags = 1 if is_progressive else 0
        obj_defs.append((start_v, v_count, start_t, t_count, color_565, flags))
        total_vert_count += v_count
        total_tri_count += t_count

        print(f"      3D Mesh {o_idx} ('{obj.name}'): {v_count} verts, {t_count} tris, color=0x{color_565:04X}, flags={flags}")

    # 3. Process 2D Sprites (Pure Dynamic UV Proportional Mapping)
    sprite_data = []
    letters_search_dirs = [
        os.path.join(blend_dir, "letters"),
        os.path.join(blend_dir, "sprites"),
        os.path.join(TOOL_RES_DIR, "letters"),
        os.path.join(TOOL_RES_DIR, "sprites"),
        os.path.join(BOOTLOADER_DIR, "res", "bleemcast_letters"),
        os.path.expanduser(r"~\Desktop\bleemcast_letters"),
    ]

    scene.frame_set(250)
    dg = bpy.context.evaluated_depsgraph_get()

    for i, s_obj in enumerate(sprite_objs):
        eval_obj = s_obj.evaluated_get(dg)
        me = eval_obj.to_mesh()
        coords_2d = []
        for v in me.vertices:
            wco = eval_obj.matrix_world @ v.co
            p2d = bpy_extras.object_utils.world_to_camera_view(scene, cam, wco)
            coords_2d.append((p2d.x * 640.0, (1.0 - p2d.y) * 480.0))
        eval_obj.to_mesh_clear()

        card_h = max(1, int(round(max(c[1] for c in coords_2d) - min(c[1] for c in coords_2d)))) if coords_2d else 32

        # Find image texture in nodes or search directories
        src_img = None
        if s_obj.data and hasattr(s_obj.data, 'materials') and len(s_obj.data.materials) > 0:
            mat = s_obj.data.materials[0]
            if mat and mat.use_nodes and mat.node_tree:
                for n in mat.node_tree.nodes:
                    if n.type == 'TEX_IMAGE' and n.image:
                        src_img = n.image
                        break

        if not src_img:
            for l_dir in letters_search_dirs:
                if os.path.exists(l_dir):
                    candidates = [
                        os.path.join(l_dir, f"letter_{i:02d}_{s_obj.name}.png"),
                        os.path.join(l_dir, f"letter_{i:02d}_{s_obj.name.replace('Sprite_', '')}.png"),
                        os.path.join(l_dir, f"letter_{i:02d}_Text{s_obj.name.replace('Sprite_Text', '').replace('Text', '')}.png"),
                        os.path.join(l_dir, f"sprite_{i:02d}.png"),
                        os.path.join(l_dir, f"{s_obj.name}.png"),
                    ]
                    for cand in candidates:
                        if os.path.exists(cand):
                            src_img = bpy.data.images.load(cand)
                            break
                    if not src_img:
                        matched = sorted([f for f in os.listdir(l_dir) if f.startswith(f"letter_{i:02d}") and f.endswith(".png")])
                        if matched:
                            src_img = bpy.data.images.load(os.path.join(l_dir, matched[0]))
                if src_img:
                    break

        if src_img:
            src_w, src_h = src_img.size[0], src_img.size[1]
            src_pixels = list(src_img.pixels)
            if src_img.filepath:
                bpy.data.images.remove(src_img)

            min_px, max_px, min_py, max_py = src_w, 0, src_h, 0
            for py in range(src_h):
                for px in range(src_w):
                    a = src_pixels[(py * src_w + px) * 4 + 3]
                    if a > 0.05:
                        if px < min_px: min_px = px
                        if px > max_px: max_px = px
                        if py < min_py: min_py = py
                        if py > max_py: max_py = py

            if min_px > max_px or min_py > max_py:
                min_px, max_px, min_py, max_py = 0, src_w - 1, 0, src_h - 1

            crop_w = max_px - min_px + 1
            crop_h = max_py - min_py + 1

            w = max(1, int(round(card_h * (crop_w / float(src_h)))))
            h = max(1, int(round(card_h * (crop_h / float(src_h)))))

            raw_pixels = bytearray()
            scale_x = crop_w / float(w)
            scale_y = crop_h / float(h)

            for dy in range(h - 1, -1, -1):
                sy_start = min_py + dy * scale_y
                sy_end = min_py + (dy + 1) * scale_y
                iy_start = int(math.floor(sy_start))
                iy_end = min(int(math.ceil(sy_end)), src_h)

                for dx in range(w):
                    sx_start = min_px + dx * scale_x
                    sx_end = min_px + (dx + 1) * scale_x
                    ix_start = int(math.floor(sx_start))
                    ix_end = min(int(math.ceil(sx_end)), src_w)

                    tot_weight, sum_r, sum_g, sum_b, sum_a = 0.0, 0.0, 0.0, 0.0, 0.0
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
                    raw_pixels.extend(struct.pack('<H', (a << 12) | (r << 8) | (g << 4) | b))

            sprite_data.append((w, h, raw_pixels, crop_w, crop_h, src_w, src_h, min_px, max_py))
        if not src_img:
            # Fallback 1: Dynamically rasterize from Blender FONT object with custom/system font fallback
            font_obj = None
            if s_obj.type == 'FONT':
                font_obj = s_obj
            else:
                text_name_candidates = [
                    s_obj.name.replace('Sprite_', ''),
                    s_obj.name.replace('Sprite_Text', 'Text'),
                    f"Text.{i:03d}" if i > 0 else "Text",
                ]
                for tc in text_name_candidates:
                    fo = bpy.data.objects.get(tc)
                    if fo and fo.type == 'FONT':
                        font_obj = fo
                        break

            if font_obj and font_obj.data and hasattr(font_obj.data, 'body'):
                char_text = font_obj.data.body.strip()
                if not char_text:
                    char_text = font_obj.name
                font_name = font_obj.data.font.name if font_obj.data.font else "Tahoma"
                col_rgb = get_object_color(font_obj, fallback=(0.20, 0.20, 0.20))

                gdi_res = rasterize_glyph_gdi(char_text, font_name, max(card_h * 2, 64))
                if gdi_res:
                    src_w, src_h, gdi_cov_list, min_px, max_px, min_py, max_py = gdi_res
                    crop_w = max_px - min_px + 1
                    crop_h = max_py - min_py + 1

                    w = max(1, int(round(card_h * (crop_w / float(src_h)))))
                    h = max(1, int(round(card_h * (crop_h / float(src_h)))))

                    raw_pixels = bytearray()
                    scale_x = crop_w / float(w)
                    scale_y = crop_h / float(h)

                    for dy in range(h - 1, -1, -1):
                        sy_start = min_py + dy * scale_y
                        sy_end = min_py + (dy + 1) * scale_y
                        iy_start = int(math.floor(sy_start))
                        iy_end = min(int(math.ceil(sy_end)), src_h)

                        for dx in range(w):
                            sx_start = min_px + dx * scale_x
                            sx_end = min_px + (dx + 1) * scale_x
                            ix_start = int(math.floor(sx_start))
                            ix_end = min(int(math.ceil(sx_end)), src_w)

                            tot_weight, sum_cov = 0.0, 0.0
                            for iy in range(iy_start, iy_end):
                                wy = min(sy_end, iy + 1.0) - max(sy_start, float(iy))
                                if wy <= 0: continue
                                for ix in range(ix_start, ix_end):
                                    wx = min(sx_end, ix + 1.0) - max(sx_start, float(ix))
                                    if wx <= 0: continue
                                    weight = wx * wy
                                    sum_cov += gdi_cov_list[iy * src_w + ix] * weight
                                    tot_weight += weight

                            cov = sum_cov / tot_weight if tot_weight > 0 else 0.0
                            a = int(min(max(cov, 0.0), 1.0) * 15.0 + 0.5)
                            r = int(min(max(col_rgb[0], 0.0), 1.0) * 15.0 + 0.5)
                            g = int(min(max(col_rgb[1], 0.0), 1.0) * 15.0 + 0.5)
                            b = int(min(max(col_rgb[2], 0.0), 1.0) * 15.0 + 0.5)
                            raw_pixels.extend(struct.pack('<H', (a << 12) | (r << 8) | (g << 4) | b))

                    sprite_data.append((w, h, raw_pixels, crop_w, crop_h, src_w, src_h, min_px, max_py))
                    src_img = True

        if not src_img:
            w, h = 32, 32
            raw_pixels = bytearray(struct.pack('<H', 0xFFFF) * (w * h))
            sprite_data.append((w, h, raw_pixels, 32, 32, 32, 32, 0, 31))

        print(f"      2D Sprite {i:02d} ('{s_obj.name}'): {w}x{h} px ({len(sprite_data[-1][2])} bytes)")

    # 4. Extract Animation Transforms & Keyframes
    start_f = scene.frame_start
    end_f = scene.frame_end
    last_motion_frame = end_f
    anim_frames = list(range(start_f, last_motion_frame, FRAME_STEP))
    if not anim_frames or anim_frames[-1] != last_motion_frame:
        anim_frames.append(last_motion_frame)

    stored_frames = len(anim_frames)
    total_playback_ticks = end_f - start_f + 1
    print(f"[3/4] Animation: {stored_frames} keyframes (total duration: {total_playback_ticks} ticks)...")

    tf_bytes = bytearray()
    spr_frame_bytes = bytearray()

    for f in anim_frames:
        scene.frame_set(f)
        cam_mat_inv = cam.matrix_world.inverted()
        dg = bpy.context.evaluated_depsgraph_get()

        for o_idx, obj in enumerate(geom_objs):
            if obj.hide_viewport or (f < 42 and 'Sphere' in obj.name):
                tf_bytes.extend(struct.pack('<12h', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
                continue

            m = cam_mat_inv @ obj.matrix_world
            r00 = int(round(m[0][0] * 8192.0))
            r01 = int(round(m[0][1] * 8192.0))
            t0  = int(round(m[0][3] * 128.0))

            r10 = int(round(m[1][0] * 8192.0))
            r11 = int(round(m[1][1] * 8192.0))
            r12 = int(round(m[1][2] * 8192.0))
            t1  = int(round(m[1][3] * 128.0))

            r20 = int(round(m[2][0] * 8192.0))
            r21 = int(round(m[2][1] * 8192.0))
            r22 = int(round(m[2][2] * 8192.0))
            t2  = int(round(m[2][3] * 128.0))

            if o_idx in progressive_faces_map:
                p_faces = progressive_faces_map[o_idx]
                t_count = len(p_faces)
                if f < 316:
                    visible_tris = 0
                elif f >= 374:
                    visible_tris = t_count
                else:
                    visible_tris = sum(1 for rev_frame, _ in p_faces if rev_frame <= f)
                r02 = visible_tris
            else:
                r02 = int(round(m[0][2] * 8192.0))

            tf_bytes.extend(struct.pack('<12h',
                r00, r01, r02, t0,
                r10, r11, r12, t1,
                r20, r21, r22, t2
            ))

        for i, s_obj in enumerate(sprite_objs):
            p2d_center = bpy_extras.object_utils.world_to_camera_view(scene, cam, s_obj.matrix_world.translation)
            eval_obj = s_obj.evaluated_get(dg)
            me = eval_obj.to_mesh()
            v_coords = []
            for v in me.vertices:
                wco = eval_obj.matrix_world @ v.co
                p2d_v = bpy_extras.object_utils.world_to_camera_view(scene, cam, wco)
                v_coords.append((p2d_v.x * 640.0, (1.0 - p2d_v.y) * 480.0))
            eval_obj.to_mesh_clear()

            w, h, _, crop_w, crop_h, src_w, src_h, min_px, max_py = sprite_data[i]
            if v_coords:
                card_min_x = min(c[0] for c in v_coords)
                card_max_x = max(c[0] for c in v_coords)
                card_min_y = min(c[1] for c in v_coords)
                card_max_y = max(c[1] for c in v_coords)
                card_w = card_max_x - card_min_x
                card_h = card_max_y - card_min_y
                dest_x = int(round(card_min_x + card_w * (min_px / float(src_w))))
                dest_y = int(round(card_min_y + card_h * ((src_h - 1 - max_py) / float(src_h))))
            else:
                dest_x = int(round(p2d_center.x * 640.0 - (w / 2.0)))
                dest_y = int(round((1.0 - p2d_center.y) * 480.0 - (h / 2.0)))

            alpha = 255 if (p2d_center.z > 0 and dest_y < 460) else 0
            spr_frame_bytes.extend(struct.pack('<hhBBH', dest_x, dest_y, alpha, 100, 0))

    # 5. Extract Boot Audio
    audio_pcm = find_audio_pcm(blend_dir)
    while len(audio_pcm) % 4 != 0:
        audio_pcm.append(0)

    print(f"[4/4] Audio: {len(audio_pcm):,} bytes (11,025 Hz 8-bit signed PCM)")

    # 6. Assemble DCBS Version 3 Binary Container
    HEADER_SIZE = 64
    off_tf = HEADER_SIZE
    off_v = off_tf + len(tf_bytes)
    off_idx = off_v + len(vert_bytes)
    off_cues = off_idx + len(idx_bytes)
    off_wav = off_cues
    off_objs = off_wav + len(audio_pcm)

    obj_table_bytes = bytearray()
    for obj_d in obj_defs:
        obj_table_bytes.extend(struct.pack('<IIIIHH', *obj_d))

    off_sprites = off_objs + len(obj_table_bytes)
    sprite_header_bytes = bytearray()
    off_spr_frames = off_sprites + len(sprite_objs) * 8
    off_pixels_base = off_spr_frames + len(spr_frame_bytes)

    pixel_data_bytes = bytearray()
    current_px_off = off_pixels_base
    for item in sprite_data:
        w, h, px = item[0], item[1], item[2]
        sprite_header_bytes.extend(struct.pack('<HHI', w, h, current_px_off))
        pixel_data_bytes.extend(px)
        current_px_off += len(px)

    header = struct.pack('<IHHIIIIIIIIIIIHHII',
        0x53424344,          # magic "DCBS"
        3,                   # version 3
        len(obj_defs),       # object_count
        total_playback_ticks,# total_frames
        total_vert_count,    # vertex_count
        total_tri_count * 3, # index_count
        0,                   # audio_cue_count
        off_tf,              # off_transforms
        off_v,               # off_vertices
        off_idx,             # off_indices
        off_cues,            # off_audio_cues
        off_wav,             # off_wavetables (PCM audio data)
        off_objs,            # off_objects
        off_sprites,         # off_sprites
        len(sprite_objs),    # sprite_count
        stored_frames,       # stored_frames
        off_spr_frames,      # off_sprite_frames
        len(audio_pcm)       # audio_sample_bytes
    )

    full_blob = (
        header +
        tf_bytes +
        vert_bytes +
        idx_bytes +
        audio_pcm +
        obj_table_bytes +
        sprite_header_bytes +
        spr_frame_bytes +
        pixel_data_bytes
    )

    os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)
    with open(OUTPUT_BIN, 'wb') as f:
        f.write(full_blob)

    print(f"=======================================================")
    print(f"  Container exported successfully ({len(full_blob):,} bytes)")
    print(f"  Output: {OUTPUT_BIN}")
    print(f"=======================================================")

if __name__ == '__main__':
    export_dcbs()
