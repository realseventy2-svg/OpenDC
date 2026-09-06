# OpenDC Bootloader (Stage 1)

This directory contains the freestanding SH-4 Stage 1 bootloader for the Sega Dreamcast. It runs directly from the ROM reset vector (`0xA0000000`), initializes basic hardware, plays the boot intro scene, parses storage media, and hands off execution to the game or payload.

It is written in freestanding C99 and SH-4 assembly. It does not link KallistiOS, libc, `libm`, or any dynamic heap allocator.

---

## Directory Layout

```text
bootloader/
├── assets/
│   ├── scene.blend                  # Blender 3D scene (swirl, camera, font objects)
│   ├── boot_scene.bin               # Exported binary scene container (DCBS v3)
│   └── boot.mp3 / boot_11k.pcm      # Audio source and downsampled 11kHz raw PCM
├── src/
│   ├── core/
│   │   ├── crt0.s                   # Reset vector, SDRAM/cache init, stack setup
│   │   ├── payload.c                # Main entrypoint, scene playback loop, boot router
│   │   ├── syscalls.c / .h          # BIOS syscall tables (sysinfo, font, flashrom, gdrom)
│   │   └── wince.c / .h             # Windows CE header (ROMHDR) and checksum parser
│   ├── video/
│   │   ├── video.c / .h             # PVR2 hardware registers, 640x480 video modes, double buffering
│   │   └── screen.c / .h            # Splash screen orchestration and theme handling
│   ├── audio/
│   │   └── sound.c / .h             # Yamaha AICA register control and PCM channel streaming
│   ├── scene/
│   │   ├── boot_scene.h             # DCBS container structures and playback API
│   │   ├── scene_loader.c / .h      # Container mounting and validation
│   │   ├── rasterizer.c / .h        # 16.16 fixed-point software span rasterizer & Gouraud shading
│   │   ├── postprocess.c / .h       # Sub-pixel edge smoothing filter
│   │   ├── sprite.c / .h            # ARGB4444 2D glyph blitter
│   │   └── scene_render.c           # Per-frame 3D matrix math and draw loop
│   ├── storage/
│   │   ├── ata.c / .h               # G1-ATA register transport (PIO)
│   │   ├── gdrom.c / .h             # GD-ROM packet commands, TOC reading, sector reads
│   │   ├── iso9660.c / .h           # ISO9660 directory and track extent resolver
│   │   ├── scramble.c / .h          # Katana 2MB binary descrambler
│   │   ├── gdi_loader.c             # Multi-track high-density GDI parser
│   │   ├── selfboot_cdi.c           # Multisession CDI parser (Session 2 LBA 45000)
│   │   ├── homebrew_cdi.c           # Raw homebrew disc loader
│   │   └── boot.c / .h              # Unified binary loader and CPU handoff
│   └── fallback_anim/
│       └── boot_anim.c / .h         # Procedural fallback 2D animation (if DCBS is missing)
├── tools/
│   ├── dcbs-tool/                   # Universal Boot Scene Exporter suite
│   │   ├── export_dcbs.py           # Blender Python exporter script
│   │   └── res/                     # Audio, letters, and scene assets
│   └── build_scene.ps1              # 1-Click compiler for Blender -> Bootloader -> BIOS
├── linker.ld                        # Section placement rules for ROM and RAM
└── Makefile                         # Stage 1 build rules
```

---

## 3D Boot Scene Engine (`DCBS` Container)

The boot intro uses a custom binary container format called `DCBS` (Dreamcast Boot Scene, version 3). The scene is constructed in Blender and exported using `tools/dcbs-tool/export_dcbs.py` (or automatically with `kos-buildscene`).

### Container Structure (64-byte Header)
```text
+-----------------------+-------------------------------------------------+
| Offset / Field        | Purpose                                         |
+-----------------------+-------------------------------------------------+
| 0x00: Magic (0x53424344)| "DCBS" identifier                             |
| 0x04: Version         | Version 3                                       |
| 0x06: Object Count    | Number of 3D mesh instances (swirl, dot, etc.)  |
| 0x08: Total Frames    | Animation duration in frames                    |
| 0x0C: Vertex Count    | Total shared vertex buffer count                |
| 0x0E: Index Count     | Total triangle index count                      |
| 0x14: Offsets         | File offsets to transforms, vertices, indices,  |
|                       | wavetable audio, sprites, and keyframe tracks   |
+-----------------------+-------------------------------------------------+
```

### Rendering Pipeline
1. **Camera & Projection**: 3D matrix math uses fixed-point 16.16 integer arithmetic with SH-4 FPU instructions for transformation matrices.
2. **Span Rasterizer**: Custom software span filler with a 1024-entry zero-division reciprocal table (`recip_1024`).
3. **Lighting**: Object-space Gouraud shading packed directly into 32-bit pixel pairs.
4. **Post-Processing**: Sub-pixel edge smoothing over the active bounding box to reduce aliasing without the cost of full-screen SSAA.
5. **2D Typography**: Text glyphs are pre-rendered into cropped ARGB4444 sprite sheets and blitted on unified baseline coordinates.
6. **Audio**: 11.025 kHz 8-bit unsigned PCM streamed directly into Yamaha AICA sound channels on cold boot.

---

## Video & Display Subsystem

The video driver programs the PowerVR2 display registers directly for 640x480 60Hz VGA/NTSC mode:
- **Page Flipping**: Dual 1MB framebuffers allocated at `0xA5000000` (Page 0) and `0xA5100000` (Page 1) in 8MB VRAM. Buffer flips are synchronized to hardware VBlank (`PVR_SYNC_STATUS`) to prevent tearing.
- **Border Color**: `PVR_BORDER_COLOR` (`0xA05F8040`) is set to match the background color (`#D2D5D9` / `0x00D2D5D9`), ensuring no black border gaps are visible on active scanout edges.
- **Clean Handoff**: When handing off to a game or secondary payload, the bootloader purges all 8MB of VRAM using SH-4 Store Queue bursts (`0xE0000000`) and resets PVR tile accelerator registers.

---

## Comparison: OpenDC vs DreamShell vs DreamBoot / DreamDash

| Feature | DreamShell | DreamBoot / DreamDash | OpenDC Bootloader |
| :--- | :--- | :--- | :--- |
| **Type** | Full OS / Desktop Environment | BIOS Chainloader | Bare-Metal BIOS Firmware |
| **Execution Location** | Main RAM (`0x8C010000+`) | 2MB Flash ROM | 2MB Flash ROM |
| **Dependencies** | Full KOS, libc, Lua, threads | Full KOS, libfat, VFS | Freestanding C99 & ASM (No KOS) |
| **Binary Size** | 10 MB – 30+ MB | 500 KB – 1.2 MB | < 64 KB core code |
| **Boot Animation** | Static splash / 2D GUI | None or basic splash | 3D baked 60 FPS mesh animation |
| **Renderer** | KOS PVR tile driver | KOS video output | 16.16 fixed-point software rasterizer |
| **Audio** | KOS sound driver | None or basic chime | Direct AICA hardware PCM registers |
---

## Current Limitations

- **Windows CE Titles**: Windows CE GDI games do not boot yet (MMU page table translation / microkernel handoff in progress).
- **Scrambled & Selfboot CDIs**: Commercial selfboot CDIs and scrambled CD-R images are currently unsupported.
- **Direct ISO Streaming**: Does not provide in-RAM disc virtualization (`isoldr`); relies on launching binaries or chainloading an OS like DreamShell.

---

## Building & Exporting

### Prerequisites
- KallistiOS toolchain (`sh-elf-gcc`, `sh-elf-objcopy`, GNU Make).
- Blender 4.x (if modifying the 3D scene).
- Python 3.

### 1. Export 3D Scene from Blender
You can build the scene and full BIOS with 1 command:
```powershell
kos-buildscene
```
Or manually run the exporter through `blender.exe`:
```powershell
& "C:\Program Files\Blender Foundation\Blender 4.5\blender.exe" "d:\path\to\scene.blend" -b -P tools\dcbs-tool\export_dcbs.py
```

### 2. Compile Bootloader Binary
From WSL or Linux shell:

```bash
make
```

This compiles all source modules, wraps `boot_scene.bin` into `boot_scene_blob.o`, and builds `boot.bin` and the final 2MB `dc_boot.bin` image.

### 3. All-in-One Build Command (PowerShell)
```powershell
wsl make -C /mnt/d/Github/Personal/KallistiOS/projects/OpenDC/bootloader; wsl cp /mnt/d/Github/Personal/KallistiOS/projects/OpenDC/bootloader/dc_boot.bin /mnt/d/Github/Personal/KallistiOS/bios/boot_loader_custom.bios
```

---

## Testing

Run the compiled BIOS in Flycast using PowerShell:

```powershell
. .\kos-env.ps1

# Boot directly to custom BIOS
kos-bootcustom

# Boot a game disc image through the custom BIOS
kos-bootcustom "D:\Games\Dreamcast\Sonic Adventure 2.gdi"
kos-bootcustom "D:\Games\Dreamcast\game.cdi"
```

---

## Acknowledgements & Credits

- **KallistiOS**: Hardware registers, base toolchain support, and video clock definitions.
- **Marcus Comstedt & libronin**: Low-level G1 transport reference, packet structures, and biosfont routines.
- **iceGDROM**: Dreamcast IDE and packet interface register research.
- **DC-SWAT (DreamShell)**: Pioneer work on G1-ATA storage, disc syscall patching, and ISO streaming.
- **Cpasjuste & darcagn (DreamBoot / DreamDash)**: BIOS replacement architecture and storage probing patterns.
- **Flycast Team**: Emulator core, cycle-accurate timing, and integrated GDB server.
