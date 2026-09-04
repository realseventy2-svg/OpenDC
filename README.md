# OpenDC - Open Source Sega Dreamcast Bootloader

OpenDC is an open-source Sega Dreamcast bootloader and custom BIOS project. It provides a clean, independent freestanding bootloader and firmware environment capable of booting retail GD-ROM games, CD-ROM media, and KallistiOS homebrew while preserving authentic Sega licensing, system calls, and hardware initialization sequences.

---

## Project Layout

```text
OpenDC/
├── bootloader/                     Freestanding SH-4 ROM stage (Stage 1)
│   ├── src/crt0.s                  Reset entry, SDRAM setup, cache setup, stack, and C handoff
│   ├── src/payload.c               Video splash, PVR2 initialization, and exception vector installation
│   ├── src/retail_vectors.h        Authentic Sega Dreamcast 2048-byte retail exception dispatcher table
│   ├── src/ata.c, ata.h            G1 ATA low-level register transport and packet interface
│   ├── src/gdrom.c, gdrom.h        GD-ROM command state machine, TOC parser, and sector reading
│   ├── src/iso9660.c, iso9660.h    ISO9660 Primary Volume Descriptor & directory extent resolver
│   ├── src/scramble.c, scramble.h  Dreamcast binary descrambling algorithm (reverses Katana interleaving)
│   ├── src/syscalls.c, syscalls.h  BIOS syscall dispatchers (sysinfo, font, flashrom, gdrom, misc)
│   ├── src/wince.c, wince.h        Isolated Windows CE subsystem (ROMHDR, SB_GDSTARD, checksums)
│   ├── src/boot.c, boot.h          Unified game launcher (Katana, WinCE, Homebrew)
│   └── linker.ld                   ROM and RAM placement rules
├── bios/                           KallistiOS custom BIOS payload (Stage 2)
│   ├── src/main.c                  Custom dashboard, disc detection, and fallback launcher
│   ├── src/bootloader_gdrom.h      Shared GD-ROM service-table definition
│   ├── res/                        Base BIOS images and audio assets
│   └── Makefile                    KOS payload compilation rules
├── Makefile                        Unified two-stage build coordinator
├── DEVELOPMENT_GUIDE.md            Detailed architectural documentation and technical roadmap
├── REFERENCE.md                    External research references and licensing boundaries
└── LICENSE                         MIT license for original OpenDC code
```

The final BIOS image is exactly **2,097,152 bytes (2 MiB)**:

```text
ROM offset 0x000000  Freestanding Bootloader (Stage 1)
ROM offset 0x010000  KallistiOS Custom BIOS Payload (Stage 2)
ROM offset 0x200000  End of 2 MiB ROM (Padded with authentic retail font/tables)
```

---

## Current Status & Milestones

### 1. Katana Retail GDI Games (100% Working)
* **Authentic License Screen**: Correctly executes the Sega license screen loop at `0x8C008300` with the 2-second hardware TMU timer, matching retail Dreamcast behavior before transitioning into Bootstrap 1 and Bootstrap 2.
* **ISO9660 & Multi-Track Resolution**: Traverses ISO9660 directory records from Track 3 and resolves data extents seamlessly across high-density tracks (e.g., Track 5).
* **Descrambling**: Automatically descrambles `1ST_READ.BIN` in RAM using the authentic Katana 2MB interleaving algorithm.
* **Full In-Game Execution**: Confirmed booting into full 3D gameplay on commercial titles such as *Sonic Adventure 2*.

### 2. Commercial Self-Boot CDI Games (100% Working)
* **Disc-Type Differentiated Loading**: Distinguishes GD-ROM high-density media (`disc_type = 0x80`) from multisession CD-ROM media (`disc_type = 0x10`), properly handling session 2 LBA 45000 offsets (`data_fad = 45150`).
* **Scrambled Binary Detection & Execution**: Identifies scrambled `1ST_READ.BIN` payloads on CD media and descrambles them directly into SDRAM at `0x8C010000`.
* **Direct Execution Bypassing Ripped IP.BIN Traps**: Routes execution cleanly to `0x8C010000`, bypassing GD-ROM-specific hardware security traps (`SYS_MISC 1`) in legacy ripped IP.BINs while preserving all BIOS syscalls and low-level vectors.
* **Confirmed Working**: Commercial self-boot CDIs (e.g. *Sonic Adventure* selfboot CDI) boot directly into gameplay without SH-4 exceptions.

### 3. KallistiOS & Homebrew CDI Images (100% Working)
* Detects unstructured homebrew images (zeroed `IP.BIN` entry) and transfers control cleanly to `0x8C010000`.
* Confirmed running homebrew suites such as *240p Test Suite* (`240pSuite.cdi`).

### 4. BIOS Syscall Subsystem & Exception Handling
* **Syscall Vectors**: Emulates the indirect vector table at cached `0x8C0000B0..0x8C0000E0` and uncached `0xAC0000B0..0xAC0000E0`:
  - `0x8C0000B0`: `sysinfo` (system properties, region configuration)
  - `0x8C0000B4`: `biofont` (ROM font rendering)
  - `0x8C0000B8`: `flashrom` (partition info, block reading, user settings)
  - `0x8C0000BC` & `0x8C0000C0`: `gdrom` (packet commands, DMA/PIO reads)
  - `0x8C0000E0`: `system/misc` (`SYS_MISC` mode init, border color)
* **Authentic Exception Dispatch Engine**: Installs the genuine 2048-byte Sega Dreamcast retail exception dispatch engine (`0x0000..0x0800`) at `0x8C000000`. Handles General Exceptions (`0x100`), TLB Miss Exceptions (`0x400`), and ASIC/Holly Interrupts (`0x600`), routing through the OS exception dispatch table at `0x8C0001C8`.
* **Direct GD-ROM Entrypoint Trampoline**: Provides an executable SH-4 machine code trampoline at `0x8C0010F0` for titles that bypass vector tables.

### 5. Modular Windows CE Subsystem (`wince.h`, `wince.c`)
* Fully isolated Windows CE detection and environment configuration module:
  - **`ROMHDR` & TOC Parsing**: Identifies Microsoft `"ECEC"` header signatures (`0x43454345`) and table of contents structures.
  - **Dynamic `SB_GDSTARD` Calculation**: Dynamically computes `physfirst + ulRAMFree` required by WinCE `IP.BIN` verification while preserving default `0x0C110000` for Katana.
  - **Security Checksum Engine**: Implements the 98-word checksum balancer across `0x8C0010F0` required by Midway games (*Midway's Greatest Arcade Hits*, *San Francisco Rush*).
  - **FlashROM Factory Identity**: Emulates factory identity block `"00110Dreamcast  "` required by Windows CE hardware verification.

### 6. Interactive Flycast GDB Debugging Suite
* Integrated developer debugging environment built from source (`flycast-debug`):
  - **Live GDB Server**: Embedded GDB remote server on port `3263` (`-DCMAKE_BUILD_TYPE=Debug -DENABLE_GDB_SERVER=ON`).
  - **Exception Trapping**: Catch and inspect SH-4 exceptions (`EXPEVT`), register states (`PC`, `PR`, `R15`, `VBR`), and memory addresses live with `gdb-multiarch`.
  - **Fast Diagnosis**: Eliminates speculative debugging by inspecting instruction-level faults and hardware register accesses directly in the emulator core.

---

## Current Roadmap: What's Achieved vs What's Left

| Feature / Subsystem | Status | Category | Details |
| :--- | :--- | :--- | :--- |
| **Freestanding 2MB BIOS Build** | **Complete** | Core | Assembles cleanly with `make check`; verified exactly 2,097,152 bytes. |
| **G1 ATA / GD-ROM Low-Level Driver** | **Complete** | Storage | Robust PIO sector reading, SPI packet commands, and multi-track streaming. |
| **ISO9660 PVD & Directory Parser** | **Complete** | Filesystem | PVD traversal, directory records, and multi-track high-density extents (Track 3 $\to$ Track 5). |
| **Dreamcast Binary Descrambler** | **Complete** | Security | 2MB Katana sector interleaving reversal algorithm for `1ST_READ.BIN`. |
| **Authentic Sega License Screen** | **Complete** | Boot Flow | Authentic 2-second TMU timer and transition into Bootstrap 1 & Bootstrap 2. |
| **Katana Retail GDI Games (SA2)** | **Complete** | Compatibility | Boots through license screen into full 3D gameplay (*Sonic Adventure 2*). |
| **Commercial Self-Boot CDI Games** | **Complete** | Compatibility | Multisession CD-ROM support with Session 2 LBA 45000 offset and on-the-fly descrambling (*Sonic Adventure CDI*). |
| **KallistiOS Homebrew CDI Discs** | **Complete** | Compatibility | Direct boot to `0x8C010000` fully working (*240p Test Suite*). |
| **Flycast GDB Debugging Suite** | **Complete** | Tooling | Live GDB server support (port 3263) for instruction-level CPU register and exception diagnosis. |
| **Region-Free Disc Patching** | **Complete** | Feature | Bypasses region lock (USA/JAP/PAL) for all retail GD-ROM and CDI discs. |
| **BIOS Syscall Vectors** | **Complete** | Core | Full vector table at `0x8C0000B0..0x8C0000E0` and `0xAC0000B0..0xAC0000E0`. |
| **Retail Exception Dispatch Engine** | **Complete** | Core | Full 2048-byte retail dispatcher handling `0x100`, `0x400`, `0x600`. |
| **Direct GD-ROM Trampoline** | **Complete** | Core | Executable SH-4 machine code trampoline at `0x8C0010F0`. |
| **Windows CE Detection & Headers** | **Complete** | Windows CE | Modular `wince.c` parsing `ROMHDR` and dynamic `SB_GDSTARD`. |
| **Windows CE Security Checksum** | **Complete** | Windows CE | 98-word Midway / SF Rush checksum balancing verified = 0. |
| **FlashROM Factory Identity & CRC** | **Complete** | Hardware | `"00110Dreamcast  "` and sysconfig block with CRC-16 `0x0340`. |
| **Video Cable / Output Sensing** | **Complete** | Hardware | Detects VGA (31kHz) vs RGB/Composite (15kHz) via `BSC_PDTRA`. |
| **Windows CE MMU Page-Table Binding** | *In Progress* | Windows CE | Microkernel (`nk.exe`) handoff and `CCN_TTB` virtual address translation. |
| **GD-ROM Drive Tray / Media Change** | *In Progress* | Storage | Polling drive status for disc insertion, lid open/close, and spin-up ready states. |
| **Soft Reset (ABXY+Start)** | *In Progress* | Core | Intercepting controller soft-reset combo to return to dashboard/bootloader cleanly. |
| **Maple Bus Controller & Peripherals** | *In Progress* | Hardware | Initializing standard controllers, keyboards, mice, and jump packs. |
| **GD-ROM Asynchronous DMA Mode** | *Planned* | Storage | `GDCC_DMAREAD` via G1 Bus DMAC for background streaming without CPU polling. |
| **Multi-Disc Swapping Support** | *Planned* | Feature | Real-time prompt and re-initialization sequence for multi-disc RPGs and adventures. |
| **VMU LCD Icon & Screen Animations** | *Planned* | Peripheral | Displaying custom OpenDC boot logo on VMU LCD screens during startup. |
| **Dual-BIOS / Flash Chip Flashing** | *Planned* | Hardware | Pinout verification and timing for MX29LV160T / MX29F1610 EEPROMs. |
| **SCIF / Serial Port GDB Stub** | *Planned* | Debug | Real-time interactive kernel debugging via the Dreamcast serial port. |
| **16:9 Anamorphic Widescreen Forcing** | *Planned* | Misc | Optional boot-time anamorphic widescreen aspect ratio patch for 3D games. |
| **CDDA Digital Audio Playback** | *Not Implemented* | Audio | Optical drive CDDA playback commands via SPI audio packets for games with audio tracks. |
| **FlashROM Physical Write Persistence** | *Not Implemented* | Hardware | Saving user preferences (language, audio, time) back to physical flash chip. |
| **Broadband Adapter / LAN Init** | *Not Implemented* | Network | Initializing BBA (HIT-0400) or 56k dial-up modem for network-capable titles. |

---

## What's Next & What's Left

### 1. Windows CE Microkernel MMU Page-Table Binding
* **Status**: Windows CE Stage-0 loader (`0WINCEOS.BIN`) initializes, verifies the license screen, and passes security checks, but halts during the microkernel (`nk.exe`) handoff.
* **Next Steps**:
  - Implement full SH-4 MMU Translation Table Base (`CCN_TTB`) page table walker support so that virtual address translations (`0x00000000..0x7FFFFFFF`) resolve correctly when the Windows CE kernel activates the MMU.
  - Ensure any page fault handler registered into `0x8C0001C8` populates `PTEH`, `PTEL`, and executes `ldtlb` without entering an unhandled TLB miss loop.

### 2. GD-ROM Asynchronous DMA & Packet Extensions
* Verify asynchronous packet read command extensions issued by Windows CE drivers via `0x8C0010F0` (e.g. `GDCC_DMAREAD` vs `GDCC_PIOREAD` status polling).

### 3. Physical Hardware Validation
* Flash the generated 2 MiB BIOS (`boot_loader_custom.bios`) onto physical flash ROM chips (e.g. Macronix `MX29LV160T` / `MX29F1610`) on dual-BIOS Dreamcast consoles.
* Validate real G1 bus ATA timings, drive spin-up delays, and tray open/close detection across original Yamaha GD-ROM drives, GDEMU, and Terraonion MODE.

---

## Build Instructions

You need:
- KallistiOS toolchain (`sh-elf-gcc`, `sh-elf-objcopy`, `kos-cc`).
- WSL with Ubuntu/Debian containing the KOS environment.
- GNU Make, Python 3, `dd`, and gzip.

```powershell
# Clean build artifacts
wsl -d Ubuntu-26.04 -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make clean

# Build full 2 MiB custom BIOS image
wsl -d Ubuntu-26.04 -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make check
```

The output BIOS is written to:
```text
projects/OpenDC/boot_loader_custom.bios
```

---

## Testing with Flycast

### 1. Standard Testing (PowerShell Helper)
Load the environment helpers in PowerShell and boot directly with your custom BIOS:

```powershell
. .\kos-env.ps1

# Test Katana GDI (e.g. Sonic Adventure 2)
kos-bootcustom "D:\Games\Dreamcast\Sonic Adventure 2\Sonic Adventure 2.gdi"

# Test Commercial Self-Boot CDI (e.g. Sonic Adventure CDI)
kos-bootcustom "D:\Github\Personal\KallistiOS\projects\dreamcast-starter\sonic-selfboot.cdi"

# Test Homebrew CDI (e.g. 240p Test Suite)
kos-bootcustom "D:\Games\Dreamcast\240pSuite.cdi"
```

### 2. Interactive GDB Debugging (Flycast Debug Build)
Flycast Debug includes an integrated GDB server on port `3263` for live SH-4 register and exception debugging:

```bash
# Launch Flycast Debug with GDB Server enabled
/mnt/d/Github/Personal/KallistiOS/flycast-debug/build/flycast \
  -config Debug.GDBEnabled=yes \
  -config Debug.GDBPort=3263 \
  -config Debug.GDBWaitForConnection=yes \
  /mnt/d/Github/Personal/KallistiOS/projects/dreamcast-starter/sonic-selfboot.cdi

# In another terminal, connect with GDB Multiarch:
gdb-multiarch -ex "target remote localhost:3263"
```
Useful GDB commands while debugging Dreamcast BIOS execution:
- `info registers`: Print SH-4 general registers (`R0`..`R15`), `PC`, `PR`, `SR`, `VBR`.
- `x/10i $pc`: Disassemble the current instruction sequence.
- `x/4xw 0xFF000024`: Inspect `EXPEVT` (SH-4 exception code).
- `b *0x8C010000`: Set breakpoint at primary game entrypoint.

---

## License

Original OpenDC source code and documentation are released under the [MIT License](LICENSE).
External projects and toolchains (KallistiOS, Flycast, retail Dreamcast assets) retain their respective licenses.
