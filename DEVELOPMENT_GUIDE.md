# Custom Dreamcast Firmware Development Guide

## Purpose & Scope

OpenDC integrates two essential Dreamcast execution layers:
1. **Freestanding SH-4 Stage 1 Bootloader**: A bare-metal ROM stage executing from `0xA0000000`, containing low-level G1/ATA hardware drivers, GD-ROM command state machines, ISO9660 multi-track parsers, binary descramblers, and authentic Dreamcast retail BIOS exception and syscall engines.
2. **KallistiOS Custom BIOS Payload (Stage 2)**: An extensible dashboard, recovery interface, and secondary application payload loaded at `0x8C010000`.

OpenDC has evolved beyond a dashboard into a **complete standalone bootloader** capable of booting retail Katana GD-ROM titles and KallistiOS homebrew without relying on the Sega retail boot ROM.

---

## Memory & Image Layout

The console starts execution from ROM address `0xA0000000`.

The combined 2 MiB BIOS image is structured as follows:

```text
ROM offset 0x000000  Bootloader Stage 1 (SH-4 bare-metal kernel & drivers)
ROM offset 0x010000  KallistiOS Custom BIOS Payload (Dashboard / Apps)
ROM offset 0x070000  End of custom code / Retail font tables & BIOS services
ROM offset 0x200000  2 MiB total ROM boundary
```

### Low-Memory Vectors & Runtime ABI

```text
Address         Component / Role
0x8C000000      VBR Base: 2048-byte Authentic Sega Dreamcast Exception Engine
0x8C0000B0      BIOS Syscall Vector: sysinfo (system props & region data)
0x8C0000B4      BIOS Syscall Vector: biofont (ROM font renderer)
0x8C0000B8      BIOS Syscall Vector: flashrom (partition & user settings)
0x8C0000BC      BIOS Syscall Vector: gdrom (G1 ATA packet commands)
0x8C0000C0      BIOS Syscall Vector: gdrom2 (secondary GD-ROM entrypoint)
0x8C0000E0      BIOS Syscall Vector: system/misc (SYS_MISC initialization)
0x8C000100      General Exception Entrypoint (routes via EXPEVT table)
0x8C0001C8      OS Exception Dispatch Table (registered by OS / WinCE)
0x8C000400      TLB Miss Exception Entrypoint (routes to page table handler)
0x8C000600      Interrupt Entrypoint (routes Holly ASIC & TMU interrupts)
0x8C0010F0      Direct GD-ROM Entrypoint Trampoline (used by WinCE & Midway)
0x8C008000      IP.BIN staging destination (16 sectors / 32 KiB)
0x8C008300      Authentic Sega License screen entrypoint
0x8C010000      Primary game binary (1ST_READ.BIN / 0WINCEOS.BIN)
0x8CE00000      BIOS runtime helper copy destination (4 KiB)
0x8CE01000      Windows CE Sector 0 ROMHDR header table mirror
0x8D000000      Initial stack pointer (Katana / Homebrew handoff)
```

---

## Source Architecture

```text
projects/OpenDC/bootloader/src/
├── crt0.s           Bare-metal reset handler, SDRAM initialization, cache setup (CCR), VBR binding
├── payload.c        PVR2 display engine, splash presentation, and exception table installation
├── retail_vectors.h Authentic 2048-byte Sega Dreamcast retail BIOS exception dispatcher
├── ata.c / ata.h    G1 ATA bus hardware register transport (PIO & register access)
├── gdrom.c / .h     GD-ROM packet command state machine, TOC parser, sector streaming
├── iso9660.c / .h   ISO9660 PVD parser, multi-track high-density extent resolution
├── scramble.c / .h  Dreamcast 2MB sector interleaving reversal algorithm
├── syscalls.c / .h  BIOS syscall dispatchers (SYSINFO, BIOFONT, FLASHROM, GD-ROM, MISC)
├── wince.c / .h     Modular Windows CE subsystem (ROMHDR parser, SB_GDSTARD, checksums)
├── boot.c / .h      Unified game launch controller (Katana, WinCE, Homebrew)
└── linker.ld        Linker script defining ROM and RAM sections
```

---

## Subsystem Details

### 1. Unified Game Launcher (`boot.c`)
- **Direct Multi-Format Detection**:
  1. Inspects disc structure via `iso9660.c`.
  2. Resolves boot binary (`1ST_READ.BIN` or `0WINCEOS.BIN`) across single-session CDI or multi-track high-density GDI tracks (e.g. Track 3 PVD directing extents into Track 5).
  3. Reverses Dreamcast proprietary byte scrambling in RAM.
  4. Checks for Windows CE signature (`wince_detect`). If detected, activates the Windows CE subsystem.
  5. Loads 16 sectors of `IP.BIN` into `0x8C008000`.
  6. Installs syscall vectors, exception tables, and hardware access registers.
  7. Transfers control to `0xAC008300` (Sega License Screen) or `0x8C010000` (Homebrew direct boot).

### 2. Syscall & Exception Engine (`syscalls.c`, `retail_vectors.h`)
- **Authentic Exception Dispatcher**: Installs the genuine 2048-byte Sega Dreamcast retail exception dispatch engine (`0x0000..0x0800`) directly from the retail BIOS.
- **Hardware Routing**:
  - Vector `0x100` (General Exception) and Vector `0x400` (TLB Miss) decode `EXPEVT`, lookup the OS handler registered at `0x8C0001C8`, preserve full CPU state, and restore via `rte`.
  - Vector `0x600` acknowledges and dispatches Holly ASIC and TMU interrupts.
- **Direct Trampoline (`0x8C0010F0`)**: Windows CE and legacy Midway titles execute direct function calls to `0x8C0010F0`. An SH-4 machine code trampoline (`mov.l @(4, PC), r0; jmp @r0; nop; nop; &gdrom_syscall_dispatch`) guarantees valid execution.
- **FlashROM Emulation**: Emulates factory partition 0 (`0x1A000` and `0x1A0A0`) with authentic region code `"00110"` and serial `"Dreamcast  "`. Partition 2 user settings are emulated with verified CRC-16 checksums (`0x0340`).

### 3. Modular Windows CE Subsystem (`wince.c`, `wince.h`)
- **Isolation**: Keeps all Windows CE specific requirements decoupled from standard Katana and Homebrew paths.
- **ROMHDR / TOC**: Parses Microsoft `ROMHDR` structures (`0x43454345` `"ECEC"`), determining `physfirst`, `physlast`, `ulRAMStart`, and `ulRAMFree`.
- **Dynamic `SB_GDSTARD`**: Dynamically calculates `physfirst + ulRAMFree` required by WinCE `IP.BIN` verification while preserving default `0x0C110000` for Katana.
- **Security Checksum**: Computes the 98-word balancing checksum across `0x8C0010F0` and writes the balancing values at `0x8C003174`.

---

## Current Roadmap: What's Achieved vs What's Left

### Core Bootloader & Architecture

| Feature / Subsystem | Status | Category | Details |
| :--- | :--- | :--- | :--- |
| **Freestanding 2MB BIOS Build** | **Complete** | Core | Assembles cleanly with `make check`; verified exactly 2,097,152 bytes. |
| **Low-Level SDRAM & Cache Setup** | **Complete** | Core | Proper SH-4 CCR configuration (0x092B enables OCRAM at 0x7E001000 for IP.BIN). |
| **Retail Exception Dispatch Engine** | **Complete** | Core | Full 2048-byte retail dispatcher handling `0x100`, `0x400`, and `0x600`. |
| **BIOS Syscall Vectors** | **Complete** | Core | Complete vector table at `0x8C0000B0..0x8C0000E0` and `0xAC0000B0..0xAC0000E0`. |
| **Direct GD-ROM Trampoline** | **Complete** | Core | Executable SH-4 machine code trampoline at `0x8C0010F0`. |
| **Soft Reset (ABXY+Start)** | *In Progress* | Core | Intercepting controller soft-reset combo to return to dashboard/bootloader cleanly. |
| **Dual-BIOS / Flash Chip Flashing** | *Planned* | Hardware | Pinout verification and timing for MX29LV160T / MX29F1610 EEPROMs. |
| **SCIF / Serial Port GDB Stub** | *Planned* | Debug | Real-time interactive kernel debugging via the Dreamcast serial port. |

### Disc, Transport & File Systems

| Feature / Subsystem | Status | Category | Details |
| :--- | :--- | :--- | :--- |
| **G1 ATA Low-Level PIO Driver** | **Complete** | Storage | High-reliability PIO sector reading and command submission. |
| **GD-ROM Packet State Machine** | **Complete** | Storage | SPI packet commands: Request Sense, Req Mode, Read CD, Read TOC. |
| **ISO9660 PVD & Extent Parser** | **Complete** | Filesystem | Traverses PVD, directory records, and multi-track high-density extents (Track 3 $\to$ Track 5). |
| **Dreamcast Binary Descrambler** | **Complete** | Security | 2MB Katana sector interleaving reversal algorithm. |
| **Region-Free Disc Patching** | **Complete** | Feature | Bypasses region lock (USA/JAP/PAL) for all retail GD-ROM and CDI discs. |
| **GD-ROM Drive Tray / Media Change** | *In Progress* | Storage | Polling drive status for disc insertion, lid open/close, and spin-up ready states. |
| **GD-ROM Asynchronous DMA Mode** | *Planned* | Storage | `GDCC_DMAREAD` via G1 Bus DMAC for background streaming without CPU polling. |
| **CDDA Digital Audio Playback** | *Not Implemented* | Audio | Optical drive CDDA playback commands via SPI audio packets for games with audio tracks. |
| **Joliet & Rock Ridge Extensions** | *Not Implemented* | Filesystem | Long filename support and deep nested directory traversal for homebrew CDIs. |
| **Multi-Disc Swapping Support** | *Planned* | Feature | Real-time prompt and re-initialization sequence for multi-disc RPGs and adventures. |

### Game Compatibility & OS Engines

| Feature / Subsystem | Status | Category | Details |
| :--- | :--- | :--- | :--- |
| **Authentic Sega License Screen** | **Complete** | Boot Flow | Authentic 2-second TMU timer and transition into Bootstrap 1 & Bootstrap 2. |
| **Katana Retail GDI Games** | **Complete** | Compatibility | Fully boots commercial titles (*Sonic Adventure 2*) into full 3D gameplay. |
| **KallistiOS Homebrew CDI Discs** | **Complete** | Compatibility | Direct boot to `0x8C010000` fully working (*240p Test Suite*). |
| **MIL-CD Exploit Compatibility** | **Complete** | Compatibility | Compatible with standard multisession CD-ROM homebrew discs. |
| **Windows CE Detection & Headers** | **Complete** | Windows CE | Modular `wince.c` parsing `ROMHDR` and dynamic `SB_GDSTARD`. |
| **Windows CE Security Checksum** | **Complete** | Windows CE | 98-word Midway / SF Rush checksum balancing verified = 0. |
| **Windows CE MMU Page-Table Binding** | *In Progress* | Windows CE | Microkernel (`nk.exe`) handoff and `CCN_TTB` virtual address translation. |
| **Windows CE Asynchronous Driver Hooks** | *Planned* | Windows CE | Handling async GD-ROM packet callbacks expected by WinCE device drivers. |
| **Uncompressed NK Kernel Relocation** | *Planned* | Windows CE | Support for non-standard homebrew CE builds with uncompressed `nk.exe` kernels. |

### Hardware, Peripherals & Misc

| Feature / Subsystem | Status | Category | Details |
| :--- | :--- | :--- | :--- |
| **FlashROM Factory Identity Emulation** | **Complete** | Hardware | Returns `"00110Dreamcast  "` at `0x1A000` and mirror `0x1A0A0`. |
| **FlashROM User Settings Emulation** | **Complete** | Hardware | System configuration block with valid CRC-16 checksum (`0x0340`). |
| **Video Cable / Output Sensing** | **Complete** | Hardware | Detects VGA (31kHz) vs RGB/Composite (15kHz) via `BSC_PDTRA`. |
| **FlashROM Physical Write Persistence** | *Not Implemented* | Hardware | Saving user preferences (language, audio, time) back to physical flash chip. |
| **Maple Bus Controller & Peripherals** | *In Progress* | Hardware | Initializing standard controllers, keyboards, mice, and jump packs. |
| **VMU LCD Icon & Screen Animations** | *Planned* | Peripheral | Displaying custom OpenDC boot logo on VMU LCD screens during startup. |
| **AICA ARM7 Sound CPU Firmcode** | *In Progress* | Audio | AICA sound chip reset and initial driver upload. |
| **RTC Calendar Clock Sync** | *In Progress* | Hardware | Reading and synchronizing RTC time with FlashROM timestamp. |
| **Broadband Adapter / LAN Init** | *Not Implemented* | Network | Initializing BBA (HIT-0400) or 56k dial-up modem for network-capable titles. |
| **16:9 Anamorphic Widescreen Forcing** | *Planned* | Misc | Optional boot-time anamorphic widescreen aspect ratio patch for 3D games. |

---

## Technical Focus: Windows CE Microkernel Completion

To bring Windows CE titles (*Midway Arcade Hits*, *Resident Evil 2*, *Rainbow Six*) to full in-game execution:

1. **MMU Page Table Walking (`CCN_TTB`)**:
   Windows CE activates the SH-4 MMU (`CCN_MMUCR.AT = 1`). User space (`0x00000000..0x7FFFFFFF`) is accessed through virtual memory pages mapped via `CCN_TTB`. When a TLB miss occurs, the CPU jumps to `0x8C000400`. The handler must look up the page directory and page tables indexed by `CCN_TTB`, populate `CCN_PTEH` and `CCN_PTEL`, and execute `ldtlb` to resume execution without faulting.

2. **Stage-0 to Microkernel Stack Handoff**:
   Ensure `r15` stack alignment and VBR register handoff strictly conform to the Microsoft CE kernel loader expectations (`r15 = 0x8C00F400` or `0x8C000000`).
