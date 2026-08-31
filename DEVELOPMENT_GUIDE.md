# Custom Dreamcast Firmware Development Guide

## Purpose

This project combines two different Dreamcast software stages:

1. A freestanding SH-4 bootloader located at the beginning of the BIOS image.
2. A KallistiOS-based custom BIOS payload loaded by the bootloader.

The intended long-term goal is to let the bootloader provide GD-ROM services
to the custom BIOS. The custom BIOS should then be able to detect a disc and
boot a game without depending on the retail BIOS GD-ROM implementation.

The current project is not yet a replacement GD-ROM BIOS. It is a working
custom-BIOS experiment with an experimental raw GD-ROM transport layer and a
build system that combines the two stages.

## Memory and image layout

The Dreamcast starts execution from the ROM address `0xA0000000`.

The combined BIOS image is 2 MiB and is organized as follows:

```text
ROM offset 0x000000  Bootloader ROM code
ROM offset 0x010000  Custom KOS BIOS payload
ROM offset 0x200000  End of BIOS image
```

The bootloader copies the payload from ROM address `0xA0010000` to RAM address
`0x8C010000`, initializes the stack/cache state, and jumps to the KOS entry
point.

The bootloader publishes its optional GD-ROM service table in RAM at:

```text
0x8C00F000
```

This address is below the KOS payload destination and is not part of the
payload copy range.

## Source layout

The two source trees are physically contained in this unified project because
they use different execution models. They are built by separate Makefiles and
then combined by the top-level Makefile.

### `bootloader`

Location:

```text
projects/OpenDC/bootloader
```

This is freestanding SH-4 code. It does not use KallistiOS, the C runtime,
threads, filesystem services, or BIOS syscalls.

Important files:

- `src/crt0.s` — reset entry, SDRAM setup, cache setup, stack setup, and C handoff.
- `src/payload.c` — video splash and stage-2 chainload logic.
- `src/gdrom.c` — experimental direct G1/ATA transport.
- `src/gdrom.h` — transport API and service-table definition.
- `linker.ld` — ROM/RAM placement and entry-point layout.
- `Makefile` — standalone bootloader build.

### `bios`

Location:

```text
projects/OpenDC/bios
```

This is a normal KallistiOS application linked as a BIOS payload. Its custom
`crt0.s` and `src/entry.s` are not used by the current Makefile. KallistiOS
provides the startup and runtime environment.

Important files:

- `src/main.c` — custom dashboard, disc probe, and game-launch path.
- `src/bootloader_gdrom.h` — matching definition of the bootloader service ABI.
- `Makefile` — KOS payload build and BIOS injection.
- `res/boot_loader_custom.bios` — temporary base image used during packaging.

The current game launch path still loads `rungd.bin` and transfers control to
the established BIOS-compatible launcher. This is intentional fallback code.

### `OpenDC`

Location:

```text
projects/OpenDC
```

This directory is the unified build coordinator and contains both source
trees. Its Makefile builds the bootloader, copies the resulting 2 MiB image as
the BIOS base, builds the KOS payload, injects that payload at the 64 KiB
offset, and produces:

```text
projects/OpenDC/boot_loader_custom.bios
```

## Building

The project uses the KallistiOS environment inside WSL. Run the commands from
PowerShell:

```powershell
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make clean
```

```powershell
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make
```

Validate all generated image sizes:

```powershell
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make check
```

Expected image size:

```text
2097152 bytes
```

The unified build performs these operations automatically; no manual copy
command is required:

```text
build bootloader/dc_boot.bin
copy dc_boot.bin to bios/res/boot_loader_custom.bios
build bios/custom_bios.bin
inject custom_bios.bin at offset 0x10000
copy final image to OpenDC/boot_loader_custom.bios
```

The build must be performed in WSL because the Makefiles use the SH-4
cross-compiler, GNU make, `dd`, and KallistiOS utility paths under `/opt`.

## Emulator testing

Flycast does not provide a host filesystem API to the Dreamcast bootloader. A
CDI file is exposed to the emulated machine as a virtual GD-ROM drive.

After building, load the environment:

```powershell
cd C:\path\to\KallistiOS
. .\kos-env.ps1
```

Then boot a CDI with the unified BIOS:

```powershell
kos-bootcustom D:\path\to\game.cdi
```

This tests the emulator's virtual GD-ROM together with the KOS/BIOS launch
path. It does not prove that the standalone raw driver works on a physical
drive.

## Current GD-ROM design

The bootloader's GD-ROM module uses the Dreamcast G1 ATA register block. It
currently provides:

- PIO timing setup;
- device selection;
- bounded status polling;
- ATA PACKET transaction setup;
- 12-byte packet transmission;
- PIO data-phase reads;
- experimental Sega `GET_TOC` (`0x14`) support;
- experimental Sega `CD_READ` (`0x30`) support;
- a fixed-address service table for the custom BIOS.

The custom BIOS validates the service-table magic/version and can attempt a raw
TOC probe. If the table is absent or the probe fails, it falls back to the KOS
filesystem path.

The service table is an ABI, not a KallistiOS driver. KOS functions such as
`cdrom_read_sectors()` do not automatically use it.

## Service-table ABI

The bootloader publishes this structure at `0x8C00F000`:

```c
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    int (*init)(void);
    uint8_t (*status)(void);
    int (*drive_ready)(void);
    int (*read_toc)(void *buffer, uint8_t session);
    int (*read_fad)(void *buffer, uint32_t fad, uint16_t sectors);
} gdrom_service_table_t;
```

The custom BIOS must check all of the following before calling the table:

```text
magic   == 0x4744524F
version == 1
size    >= sizeof(gdrom_service_table_t)
```

Function pointers refer to code in the bootloader ROM. The ROM remains mapped
after the KOS payload starts, so the custom BIOS can call those functions.

## What is still missing

### 1. Verified GD-ROM initialization

The current packet transport is not a complete drive initialization routine.
The final implementation must establish the correct sequence for:

- drive reset/wakeup;
- repeated initialization/status commands;
- sector/data mode selection;
- drive readiness and media status;
- command completion and error recovery.

Calling the raw driver during reset is unsafe. All waits must remain bounded.

### 2. Complete PIO and DMA behavior

The current implementation only provides a basic PIO path. A production
implementation needs to handle:

- multiple data phases;
- byte-count changes between phases;
- command and data interrupts;
- DMA alignment and protection registers;
- DMA completion and overrun errors;
- cache maintenance;
- abort and reset recovery.

### 3. Reliable TOC parsing

The custom BIOS currently treats a successful TOC response as sufficient to
identify a data track. It still needs robust parsing for:

- session count;
- first and last track;
- lead-out;
- audio/data control flags;
- CD and GD media differences;
- invalid or incomplete TOC responses.

### 4. ISO9660 access

The custom BIOS does not yet use the raw service to browse the disc. It needs a
small read-only ISO9660 implementation capable of:

1. reading the primary volume descriptor;
2. locating the root directory record;
3. finding `1ST_READ.BIN`;
4. reading its extent and length;
5. handling sector boundaries and file sizes.

### 5. Game loading and descrambling

A raw game loader must load the binary into a safe staging area, apply the
Dreamcast scrambling reversal, copy it to its required address, configure the
handoff registers/stack, invalidate caches, and jump to the game entry point.

The current BIOS still uses the proven `rungd.bin` launcher for this purpose.

### 6. Stage-2 packaging validation

The bootloader expects a payload at ROM offset `0x10000`. Every final image
must be checked to ensure that:

- the bootloader code is present at the beginning;
- the KOS payload is present at offset `0x10000`;
- the payload does not exceed its available ROM window;
- the final image is exactly 2 MiB.

The KOS payload window is limited. Excessive audio, assets, or debugging code
can produce a BIOS that builds successfully but cannot boot correctly.

## Current limitations

- The raw GD-ROM path is experimental and has not been validated on physical
  hardware.
- Flycast CDI testing can hide hardware timing and initialization problems.
- A successful bootloader splash does not prove that the GD-ROM service works.
- A successful TOC read does not prove that game sectors can be read reliably.
- The custom BIOS still depends on `rungd.bin` for actual game launching.
- The service table is not compatible with KOS `cdrom_*()` calls automatically.
- No interrupt-driven GD-ROM service is currently exposed.
- No DMA-backed service is currently exposed.
- The bootloader has no filesystem, allocator, or general-purpose runtime.
- The bootloader must not overwrite a physical BIOS without a recoverable
  replacement method and a verified image.

## Reference sources

Original OpenDC code and documentation are released under the MIT License in
[`LICENSE`](LICENSE). This does not change the licenses of KallistiOS,
DreamDash, Libronin, iceGDROM, or any BIOS and firmware images used during
development.

### KallistiOS

Use these files for local hardware definitions and API behavior:

- [`g1ata.c`](../../kos/kernel/arch/dreamcast/hardware/g1ata.c)
- [`cdrom.c`](../../kos/kernel/arch/dreamcast/hardware/cdrom.c)
- [`syscalls.c`](../../kos/kernel/arch/dreamcast/hardware/syscalls.c)

KOS `cdrom.c` is not a freestanding replacement. It depends on KOS runtime
services and BIOS GD-ROM syscalls.

### Libronin

The checked-in reference is:

```text
projects/OpenDC/bootloader/reference/libronin
```

Its `gddrive.s` provides BIOS syscall wrappers at `0x8C0000BC`; it is useful
for command names and legacy behavior, but it is not itself a raw G1 driver.

### iceGDROM

The checked-in reference is:

```text
projects/OpenDC/bootloader/reference/iceGDROM
```

The most relevant file is:

```text
rv32/source/ide.c
```

It documents the Sega Packet Interface commands used by the FPGA emulator,
including TOC and CD read packet formats. Its FPGA and RISC-V code cannot be
compiled directly for the Dreamcast SH-4.

## Recommended development order

The safest development sequence is:

1. Keep the working KOS/`rungd.bin` launcher enabled.
2. Validate the combined image layout with `make check`.
3. Add a visible raw-driver diagnostic showing status and return codes.
4. Validate drive initialization without a disc.
5. Validate TOC reading with a CDI in Flycast.
6. Validate one known FAD sector against expected bytes.
7. Read and validate `IP.BIN`.
8. Implement the minimal ISO9660 lookup for `1ST_READ.BIN`.
9. Load and descramble one game binary into a staging buffer.
10. Add the raw game handoff behind a build-time or menu option.
11. Test on multiple CDI images and, only afterward, physical hardware.

Do not remove the fallback launcher until the raw path can recover cleanly from
an empty drive, an invalid disc, a changed disc, a read error, and a reset.
