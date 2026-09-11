# OpenDC - Open Source Dreamcast Bootloader

OpenDC is an open-source bootloader and custom BIOS for the Sega Dreamcast. It pairs a freestanding Stage 1 bootloader with an optional Stage 2 KallistiOS dashboard payload.

It boots retail GD-ROM games and KallistiOS homebrew, featuring a 60 FPS 3D boot scene exported from Blender.

---

## Project Structure

- `bootloader/`: Stage 1 bare-metal ROM code (`0xA0000000`), 3D scene engine (`DCBS` container), software rasterizer, ATA/GD-ROM drivers, and boot parsers.
- `bios/`: Stage 2 KallistiOS custom BIOS payload (`0x8C010000`).
- `DEVELOPMENT_GUIDE.md`: Detailed hardware registers, memory maps, and implementation notes.
- `REFERENCE.md`: External references, prior art, and licensing details.

---

## Functionality

- Launch retail Katana GD-ROM games (GDI) directly without swap discs
- Launch raw KallistiOS homebrew binaries directly
- Automatic region-free playback for USA, Japanese (NTSC-J), and European (PAL) releases
- Customizable boot animation using Blender with audio support
- Direct hardware 480p VGA (31kHz) and 15kHz CRT RGB/Composite video support with zero screen tearing
- Instant cold boot from hardware reset with zero OS spin-up delay
- Low-level G1-ATA transport with fast PIO sector reading for IDE hard drives and GD-ROM drives
- Can enable/disable Sega License Splash Screen
- Ultra-compact footprint (< 64 KB core engine) leaving over 95% of 2MB flash ROMs free
- Build as a physical 2MB flash BIOS (`.bios`/`.bin`), a standalone binary, or an emulator firmware

---

## Current Limitations

- **Windows CE Games**: Windows CE GDI titles (*Midway Arcade*, *Resident Evil 2*, etc.) cannot boot yet (MMU page table walker and microkernel handoff in progress).
- **Scrambled & Selfboot CDIs**: Commercial selfboot CDIs and scrambled CD-R images cannot boot yet.
- **No ISO Loader**: Does not stream unpacked GDI/ISO disc images from SD/IDE hard drives directly (requires chainloading an OS like DreamShell).

---

## Build & Run

### 1. Build Custom BIOS
From the OpenDC project root:
```bash
make clean
make
```
This builds both the Stage 1 bootloader and Stage 2 BIOS payload, generating `boot_loader_custom.bios` (2 MB).

### 2. Export Blender Scene (Optional)
```bash
kos-buildscene
# Or manually with Python:
blender "path/to/scene.blend" -b -P bootloader/tools/dcbs-tool/export_dcbs.py
```

### 3. Run in Flycast
In DreamSDK bash (or PowerShell via `. .\kos-env.ps1`):
```bash
# Boot into OpenDC 3D Boot Scene & Dashboard
kos-bootcustom

# Or boot directly with a game disc inserted
kos-bootcustom "/path/to/game.gdi"
```

---

## Credits & References

- **KallistiOS**: Hardware registers, base toolchain, and video timing tables.
- **Marcus Comstedt & libronin**: Low-level G1 transport, packet protocols, and BIOS structures.
- **iceGDROM**: G1 IDE bus and Sega Packet Interface documentation.
- **DC-SWAT (DreamShell)**: G1-ATA storage drivers, filesystem handling, and ISO loader research.
- **Cpasjuste & darcagn (DreamBoot / DreamDash)**: Custom BIOS ROM structure and storage probing patterns.
- **Flycast Team**: Dreamcast emulation core and GDB debugging support.

---

## License

MIT License. See [LICENSE](LICENSE). External references retain their respective licenses.


