# DCBS Tool: Universal Dreamcast Boot Scene Exporter (v3.0)

The **DCBS Tool** is an automated pipeline that converts standard **Blender (`.blend`) 3D scenes** into optimized, bare-metal Sega Dreamcast boot sequences (`boot_scene.bin` -> `dc_boot.bin`).

It produces true **60 FPS locked playback** with 3D perspective depth, Gouraud shading, Blinn-Phong specular glints, 2x Super-Sampled Anti-Aliasing (SSAA), sub-pixel antialiased 2D typography, and 11 kHz PCM audio.

---

## 📁 Directory Structure

```
tools/dcbs-tool/
├── export_dcbs.py          # Universal Blender -> DCBS v3 Binary Exporter
├── check_rom_size.py       # Flash ROM partition memory & budget analyzer
├── README.md               # Tool guide and scene setup reference
└── res/
    ├── audio/              # Audio files (boot.mp3, boot.wav, boot_11k.pcm)
    ├── letters/            # Optional external 2D glyph PNG textures
    ├── sprites/            # Optional 2D sprite cards
    └── scenes/             # Blender project files (*.blend)
```

---

## 🎨 Blender Scene Setup Guide

The exporter dynamically discovers objects and roles based on Blender **Collections**. Organize your Outliner as follows:

```
📁 Scene Collection
   ├── 📷 Camera            # Active Scene Camera (640x480, 50mm, 60 FPS)
   ├── 💡 Lamp / Light      # Primary Sun or Point Light (top-left-front)
   ├── 📁 3D_Objects        # 3D meshes (Swirl, Sphere, Jewels, Mechanical parts)
   ├── 📁 Sprites           # 2D Typography / Logo cards / Lens flares
   └── 📁 Mask              # (Optional) Occluder cubes/wedges for progressive reveals
```

### 1. Collection Roles & Supported Synonyms

| Collection | Supported Synonyms (Case-Insensitive) | Description |
| :--- | :--- | :--- |
| **3D Geometry** | `3D_Objects`, `Geometry`, `Boot_Objects` | 3D meshes that will be Gouraud shaded and transformed at 60 FPS. |
| **2D Sprites** | `Sprites`, `Letters`, `Letters_2D_Planes`, `Typography` | Camera-facing planes or native Blender `FONT` objects rendered via the fast 2D alpha blitter. |
| **Mask / Occluders** | `Mask`, `Masks`, `Masked`, `Occluders`, `Reveal_Masks` | Helper meshes used to keyframe progressive reveals. **Automatically excluded from the final binary** to save ROM and CPU cycles. |

---

### 2. Camera Configuration

In **Output Properties** (Printer icon):
- **Resolution X**: `640 px`
- **Resolution Y**: `480 px` (Standard Dreamcast 4:3 Aspect Ratio)
- **Frame Rate**: `60 fps`

In **Camera Data Properties** (Green camera icon):
- **Type**: `Perspective`
- **Focal Length**: `50 mm` (Standard) or `35 mm` (Wide angle)
- **Clip Start**: `0.1 m`, **Clip End**: `100 m`

> **Camera Animation**: You can animate camera fly-throughs, pans, shakes, and orbital rotations. The camera's inverse world matrix is baked at every keyframe.

---

### 3. Automated Mask-Driven Progressive Reveal (Method A)

If you want a 3D mesh (e.g. Swirl or drawing tendril) to reveal progressively:
1. Create occluder cubes or curved wedges in Blender covering the mesh.
2. Put them inside the **`Mask`** collection.
3. Animate each occluder to move away or drop ($Z < 0$) when you want that part of the mesh to appear.
4. **The Exporter automatically**:
   - Samples when each occluder moves away.
   - Maps underlying mesh triangles to their covering occluder.
   - Re-orders the triangle index buffer by the drop timestamps.
   - **Discards all occluders from the exported container** (0 extra triangles & 0 extra matrix transforms on the Dreamcast!).

---

### 4. 2D Typography & Sprites

The exporter supports two ways to create 2D sprites:
- **Native Blender Text (`FONT` objects)**:
  - Add text in Blender (`Shift + A` -> **Text**).
  - Pick any system or custom `.ttf`/`.otf` font.
  - The exporter uses **Windows GDI sub-pixel antialiasing** to render crisp, high-DPI glyphs into the ROM.
- **Image Texture Planes (PNG with Alpha)**:
  - Add a Mesh Plane with a material using an `Image Texture` node.
  - Automatically packed into 16-level **RGBA4444** alpha textures.

---

### 5. Audio Pipeline

Drop any audio file into `tools/dcbs-tool/res/audio/` (or next to your `.blend` file):
- Supported formats: `.wav`, `.mp3`, or `.pcm`.
- Automatically converted using FFmpeg into **11,025 Hz 8-bit signed mono PCM** for the Sega AICA sound processor.

---

## ⚡ Build & Run Commands

From PowerShell in the KallistiOS environment:

```powershell
# Auto-compile Blender scene -> OpenDC Bootloader -> Custom BIOS
kos-buildscene [SceneName.blend]

# Launch and test the compiled Custom BIOS in Flycast
kos-bootcustom

# Check Flash ROM memory partition budget & headroom
kos-romsize
```

---

## 📊 Performance & Hardware Budget Guidelines

| Metric | Recommendation | Limit / Capacity |
| :--- | :--- | :--- |
| **Polygon Count** | **1,500 – 3,500 tris** | ~4,500 tris for locked 60 FPS on bare-metal SH-4 CPU. |
| **Scene ROM Budget** | **~240 KB – 500 KB** | **1.38 MB partition** (`0xA00A0000..0xA01FFFFF`) with ~1.14 MB free headroom. |
| **Frame Rate** | **60 FPS** (`FRAME_STEP = 1`) | 600 keyframes baked for a 10-second sequence. |
| **2D Sprites** | **1 – 16 sprites** | Up to 64x64 or 128x128 pixels per sprite card. |
