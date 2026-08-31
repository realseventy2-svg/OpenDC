# OpenDC

OpenDC is an open-source Dreamcast bootloader and custom BIOS project. It is
designed as a research and development platform for replacing parts of the
retail BIOS boot flow while keeping a reliable KallistiOS-based fallback during
development.

The project is currently experimental. It boots a custom BIOS dashboard in
Flycast, but it is not yet a complete standalone GD-ROM BIOS or a replacement
for the retail game loader.

## Project layout

```text
OpenDC/
├── bootloader/             Freestanding SH-4 ROM stage
│   ├── src/crt0.s          Reset, memory, cache, and stack setup
│   ├── src/payload.c       Splash screen and BIOS payload handoff
│   ├── src/gdrom.c         Experimental G1/ATA GD-ROM transport
│   ├── src/gdrom.h         Transport API and service-table ABI
│   └── linker.ld           ROM and RAM layout
├── bios/                   KallistiOS custom BIOS payload
│   ├── src/main.c          Dashboard, disc detection, and launcher path
│   ├── src/bootloader_gdrom.h  Shared GD-ROM service-table definition
│   ├── res/                Generated BIOS base and runtime resources
│   └── Makefile             KOS payload build rules
├── Makefile                Unified two-stage build
├── DEVELOPMENT_GUIDE.md    Detailed architecture and roadmap
├── REFERENCE.md            External research projects and license boundary
└── LICENSE                 MIT license for original OpenDC work
```

The final BIOS image is organized as follows:

```text
ROM offset 0x000000  Bootloader
ROM offset 0x010000  KallistiOS BIOS payload
ROM size    0x200000 2 MiB total image
```

The bootloader copies the payload to RAM and starts it. It also publishes an
experimental GD-ROM service table at `0x8C00F000`. The table is an OpenDC ABI;
it is not automatically integrated with KallistiOS's `cdrom_*` functions.

## Current status

### Working or usable today

- The two stages build into a single 2 MiB BIOS image.
- The bootloader can display its splash and transfer control to the KOS BIOS.
- The custom BIOS dashboard can run in Flycast.
- CDI images can be supplied to Flycast as a virtual GD-ROM.
- The existing KOS-compatible `rungd.bin` launcher can be used for the current
  game-launch path.
- The bootloader contains an experimental G1/ATA packet transport and service
  table for future BIOS integration.

### Not complete yet

- The raw GD-ROM service is not a complete drive driver.
- The BIOS does not yet use the raw service as its normal KOS CD-ROM backend.
- Drive reset, wake-up, media-change handling, and initialization need more
  validation.
- PIO transfer handling is basic; interrupt and DMA paths are not implemented.
- TOC responses are not yet parsed robustly for all CD/GD-ROM media types.
- There is no complete raw ISO9660 reader using the bootloader service.
- There is no production-ready `IP.BIN`/`1ST_READ.BIN` loader and handoff.
- Dreamcast scrambling reversal and game entry-point setup remain unfinished.
- The raw path has not been validated on physical Dreamcast hardware.

The current BIOS deliberately uses `rungd.bin` for game launching. This keeps
the project testable while the standalone GD-ROM and game-loader work is
developed. Do not remove that fallback until the raw path has equivalent error
handling and has been tested with empty, invalid, changed, and working discs.

## What must be implemented

The following work is needed before OpenDC can claim standalone game boot
support.

### 1. Establish a reliable GD-ROM state machine

Implement and verify the complete sequence for drive selection, reset or
wake-up, status polling, packet submission, data transfer, command completion,
timeouts, and recovery. Every polling loop must have a bounded timeout so a
missing drive or empty tray cannot hang the console.

The implementation must distinguish at least these states:

```text
uninitialized -> initializing -> ready
                         └──> error / retry
ready -> reading -> ready
ready -> media changed / no disc
```

### 2. Finish PIO and add DMA support

The current transport is a starting point for PIO packet reads. Contributors
need to verify data-phase byte counts, status transitions, alignment, cache
maintenance, and error conditions. DMA should be added only after PIO reads are
correct, with proper G1 protection registers, alignment rules, completion
checks, and abort/reset handling.

### 3. Parse the TOC correctly

Implement a validated TOC parser that handles sessions, track numbers,
lead-out, control flags, audio tracks, data tracks, CD media, and GD-ROM media.
A successful command response alone must not be treated as proof that a usable
game disc is present.

### 4. Build a minimal ISO9660 reader

Use the raw sector service to read the primary volume descriptor, locate the
root directory, find the boot file, and read its extent and length. The reader
must handle sector boundaries, upper/lower-case names, malformed records, and
short reads without corrupting memory.

### 5. Implement a safe Dreamcast game handoff

A standalone loader must validate `IP.BIN`, locate the correct boot file, read it
into a safe staging buffer, reverse the disc scrambling correctly, place it at
the required address, prepare registers and cache state, and transfer control
using the correct Dreamcast convention. This code must be tested against known
images before it replaces `rungd.bin`.

### 6. Add diagnostics and regression tests

Expose status and error codes on screen or through serial output. Add tests for
no disc, open tray, disc insertion, disc change, invalid media, TOC failure,
short reads, bad ISO records, and launcher failure. A successful splash only
proves that the payload started; it does not prove GD-ROM functionality.

## Build requirements

You need:

- A KallistiOS checkout and configured Dreamcast SH-4 toolchain.
- WSL with a Linux distribution containing the KOS build environment.
- GNU Make, `dd`, Python, gzip, and the KallistiOS utilities.
- Flycast for emulator testing.

The build commands below use placeholders so they work regardless of where the
repositories are cloned. Replace `<DISTRO>` and `/path/to/KallistiOS` with your
own values.

```powershell
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make clean
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make check
```

The top-level Makefile builds the bootloader, generates the BIOS base, builds
the KOS payload, injects it at offset `0x10000`, and writes:

```text
projects/OpenDC/boot_loader_custom.bios
```

The final file must be exactly `2097152` bytes. Generated binaries, objects,
CDIs, and intermediate BIOS files are excluded by `.gitignore`.

## Flycast testing

Load the KallistiOS PowerShell helpers from the KallistiOS checkout, then boot
a CDI using the generated BIOS:

```powershell
Set-Location C:\path\to\KallistiOS
. .\kos-env.ps1
kos-bootcustom C:\path\to\game.cdi
```

Testing with a CDI exercises Flycast's virtual drive and the current KOS
launcher path. It cannot prove that register timing, drive initialization, or
DMA behavior will work on physical hardware.

When debugging a reset, test in this order:

1. Boot the BIOS with no CDI and confirm the dashboard remains running.
2. Boot with a known-good CDI and confirm the dashboard remains responsive.
3. Confirm the KOS launcher resource exists at `/rd/rungd.bin.gz`.
4. Add visible diagnostics around status, TOC, sector reads, and handoff.
5. Only then test the raw GD-ROM path.

## Contributing

Contributions are welcome. OpenDC needs hardware-oriented testing, careful
reverse engineering, documentation, emulator regression tests, and small
reviewable code changes.

Before contributing:

1. Read [`DEVELOPMENT_GUIDE.md`](DEVELOPMENT_GUIDE.md) and
   [`REFERENCE.md`](REFERENCE.md).
2. Keep the known-good `rungd.bin` fallback working while developing raw GD-ROM
   features.
3. Make one focused change per commit where possible.
4. Document hardware assumptions, register addresses, packet formats, timeout
   values, and test results.
5. Test both an empty-drive case and a known-good CDI when changing GD-ROM or
   startup code.

For pull requests, explain what changed, which target was tested, whether the
test used Flycast or physical hardware, and include logs or photos when they
help reproduce the result. If a change is experimental, label it clearly and
describe how to disable or recover from it.

## License and external projects

Original OpenDC code and documentation are released under the [MIT License](LICENSE).
KallistiOS, DreamDash, Libronin, iceGDROM, BIOS images, firmware, tools, and
other external assets retain their own licenses. OpenDC does not relicense
those materials. Review [`REFERENCE.md`](REFERENCE.md) before copying code.
