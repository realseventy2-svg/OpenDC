# OpenDC Firmware & Bootloader Development Guide

This document describes the internal architecture of OpenDC, including memory layouts, register configurations, the 3D graphics and audio pipeline, and media loading paths.

---

## Memory & Image Layout

The console starts execution from ROM address `0xA0000000` (uncached P2 area) upon hardware reset.

```text
2 MiB BIOS ROM Map:
0x000000 - 0x010000   Stage 1 Bootloader (freestanding kernel + 3D boot scene)
0x010000 - 0x070000   Stage 2 Custom BIOS / Dashboard payload (optional)
0x070000 - 0x1FFFFF   FlashROM padding, font tables, and BIOS syscall tables
```

### Low-Memory Vectors & Runtime ABI

```text
Address         Component / Role
0x8C000000      VBR Base: 2048-byte Sega Dreamcast exception dispatch engine
0x8C0000B0      Syscall Vector: sysinfo (system properties & region configuration)
0x8C0000B4      Syscall Vector: biofont (ROM font rendering routines)
0x8C0000B8      Syscall Vector: flashrom (partition info, block read, user settings)
0x8C0000BC      Syscall Vector: gdrom (G1-ATA packet commands & sector reads)
0x8C0000C0      Syscall Vector: gdrom2 (secondary GD-ROM entrypoint)
0x8C0000E0      Syscall Vector: system/misc (SYS_MISC initialization)
0x8C000100      General Exception Handler (decodes EXPEVT, routes via table)
0x8C0001C8      OS Exception Dispatch Table (registered by OS / WinCE)
0x8C000400      TLB Miss Handler (routes to page table handler)
0x8C000600      Interrupt Handler (routes Holly ASIC & TMU interrupts)
0x8C0010F0      Direct GD-ROM Trampoline (used by Windows CE & Midway games)
0x8C008000      IP.BIN load destination (16 sectors / 32 KiB)
0x8C008300      Sega License Screen loop entrypoint
0x8C010000      Primary game execution entrypoint (1ST_READ.BIN / 0WINCEOS.BIN)
0x8CE00000      BIOS runtime helper copy destination
0x8CE01000      Windows CE ROMHDR table mirror
0x8D000000      Initial stack pointer (Katana / Homebrew handoff)
```

---

## Source Subsystems

```text
bootloader/src/
├── core/
│   ├── crt0.s              # Reset handler, SDRAM/cache (CCR) init, VBR binding
│   ├── payload.c           # Main execution flow, scene playback loop, handoff
│   ├── syscalls.c / .h     # BIOS syscall dispatchers (SYSINFO, FONT, FLASHROM, GDROM)
│   └── wince.c / .h        # Windows CE ROMHDR parser, dynamic SB_GDSTARD, checksums
├── video/
│   ├── video.c / .h        # PVR2 video registers, 640x480 VGA/NTSC, double-buffering
│   └── screen.c / .h       # Splash screen orchestration and theme management
├── audio/
│   └── sound.c / .h        # Yamaha AICA register programming and PCM audio streaming
├── scene/
│   ├── boot_scene.h        # DCBS container structs and playback API
│   ├── scene_loader.c / .h # DCBS blob mounting, header validation, data offsets
│   ├── rasterizer.c / .h   # 16.16 fixed-point software span rasterizer, Gouraud lighting
│   ├── postprocess.c / .h  # Sub-pixel edge smoothing filter
│   ├── sprite.c / .h       # ARGB4444 glyph blitter
│   └── scene_render.c      # Per-frame 3D matrix transforms and draw loop
└── storage/
    ├── ata.c / .h          # G1-ATA register transport (PIO reads)
    ├── gdrom.c / .h        # GD-ROM packet state machine and TOC parser
    ├── iso9660.c / .h      # ISO9660 directory and multi-track extent parser
    ├── scramble.c / .h     # Katana 2MB binary descrambler
    ├── gdi_loader.c        # Multi-track GDI loader
    ├── selfboot_cdi.c      # Multisession CDI loader (Session 2 LBA 45000)
    ├── homebrew_cdi.c      # Raw homebrew disc loader
    └── boot.c / .h         # Unified game launcher and payload handoff
```

---

## 3D Boot Scene & Graphics Pipeline

### 1. `DCBS` Container Format (v3)
The 3D boot scene is packaged into a compact binary blob generated from Blender (`tools/dcbs-tool/export_dcbs.py` or via `kos-buildscene`):
- **64-byte Header**: Defines magic (`0x53424344`), version (3), object count, vertex count, triangle index count, audio size, and file offsets.
- **Mesh Data**: Shared vertex positions (1,136 triangles for swirl mesh), normals, and index tables.
- **Transform Curves**: Pre-baked 60 FPS translation, rotation (quaternion), and scale matrices for the camera and animated objects.
- **2D Glyph Sheets**: ARGB4444 pre-rendered typography cropped tightly and placed on unified baseline coordinates (`y = 325`).
- **Audio Table**: 11.025 kHz unsigned 8-bit PCM audio stream.

### 2. Software Span Rasterizer
- Uses fixed-point 16.16 arithmetic for edge interpolation.
- Precomputed 1024-entry reciprocal table (`recip_1024`) avoids SH-4 integer division stalls.
- Direct 32-bit packed Gouraud span writes to VRAM back buffer (`0xA5100000`).
- Edge smoothing filter evaluates color transitions along geometry silhouettes within the active bounding box.

### 3. Display Timing & Video Registers
- Mode: 640x480 60Hz VGA (`PVR_SCAN_CLK = 0x020C0359`, `PVR_BORDER_X = 0x007E0345`, `PVR_BITMAP_X = 0x000000AC`).
- Border Color: `PVR_BORDER_COLOR` (`0xA05F8040`) is initialized to `#D2D5D9` (`0x00D2D5D9`) and synchronized with `boot_scene_get_bg_color()` to eliminate black border gaps on scanout edges.
- Double-buffering flips between `0xA5000000` (Page 0) and `0xA5100000` (Page 1) on hardware VBlank (`PVR_SYNC_STATUS & 0x03FF`).

---

## Disc & Payload Boot Flow (`boot.c`)

1. **Media Identification**:
   - Queries `gdrom_get_cached_disc_type()`: `0x80` for GD-ROM, `0x10` for CD-ROM / CDI.
2. **Binary Resolution**:
   - Traverses ISO9660 PVD to find `1ST_READ.BIN` or `0WINCEOS.BIN`.
   - Handles multi-track GDI extents where Track 3 points to high-density Track 5.
   - Handles multisession CD-ROM / CDI images where Session 2 starts at LBA 45000 (`data_fad = 45150`).
3. **Descrambling**:
   - If loading from CD-ROM media (`cached_disc_type != 0x80`) and the binary is scrambled, executes the 2MB Katana descrambler in RAM.
4. **Syscall & Exception Installation**:
   - Copies 2048-byte retail exception table to `0x8C000000`.
   - Installs jump tables at `0x8C0000B0..0x8C0000E0` and `0xAC0000B0..0xAC0000E0`.
   - Configures `0x8C0010F0` trampoline.
5. **Handoff**:
   - **GD-ROM Games**: Branches to `0xAC008300` (Sega License Screen).
   - **CDI / Homebrew**: Branches directly to `0x8C010000` to bypass legacy GD-ROM traps in ripped IP.BINs.
   - Cleans up VRAM using Store Queue burst purges (`0xE0000000`) before jumping to the game.

---

## Windows CE Integration Details

1. **Header Parsing**: Evaluates Microsoft `ROMHDR` (`0x43454345` `"ECEC"`) structures and table of contents.
2. **Dynamic `SB_GDSTARD`**: Calculates `physfirst + ulRAMFree` required by WinCE `IP.BIN` validation.
3. **Checksum Balancing**: Balances the 98-word checksum across `0x8C0010F0` for Midway titles.
4. **Current Status**: Stage-0 loader initializes and validates headers cleanly. Working on MMU Translation Table Base (`CCN_TTB`) page table walker for `nk.exe` microkernel handoff.

---

## Build & Test Workflow

### Compile Bootloader & Custom BIOS
```bash
make clean
make
```

### Export Blender Scene
```bash
kos-buildscene
# Or manually with Python:
blender "path/to/scene.blend" -b -P bootloader/tools/dcbs-tool/export_dcbs.py
```

### Launch in Flycast
```bash
kos-bootcustom "/path/to/game.gdi"
```

