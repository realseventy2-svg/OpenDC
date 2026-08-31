# Custom Dreamcast firmware

This directory is the single build tree for the experimental custom Dreamcast
firmware. It contains two stages:

- `bootloader/` - freestanding SH-4 code placed at ROM offset `0x000000`.
- `bios/` - a KallistiOS custom BIOS payload placed at ROM offset `0x010000`.

The unified build creates one 2 MiB image:

```text
projects/OpenDC/boot_loader_custom.bios
```

## Build

Run the following from PowerShell. `kos-exec.sh` supplies the KallistiOS
environment and SH-4 cross-compiler inside WSL:

```powershell
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make clean
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make
wsl -d <DISTRO> -e /path/to/KallistiOS/scripts/kos-exec.sh /path/to/KallistiOS/projects/OpenDC make check
```

The top-level Makefile automatically builds the bootloader, uses its padded
`dc_boot.bin` as the BIOS base, builds the KOS payload, injects that payload at
`0x10000`, and writes the final image. No manual `Copy-Item` step is required.

The expected final size is exactly `2097152` bytes. `make clean` removes build
outputs from both stages and the generated final image. Build products are
ignored by Git; source files, Makefiles, and documentation are tracked.

## Emulator test

After building, load the KallistiOS helper functions and boot a CDI in Flycast:

```powershell
Set-Location C:\path\to\KallistiOS
. .\kos-env.ps1
kos-bootcustom D:\path\to\game.cdi
```

This exercises Flycast's virtual GD-ROM and the KOS-compatible launch path. It
does not prove that the experimental standalone GD-ROM transport works on a
physical Dreamcast drive.

## GD-ROM status

The bootloader currently installs an experimental fixed-address GD-ROM service
table at `0x8C00F000`. It contains basic G1/ATA packet transport, status
polling, PIO data reads, and experimental TOC/CD-read commands. The BIOS can
probe that table, but the normal game-launch path still uses KOS's established
`rungd.bin` launcher.

This is not yet a complete replacement GD-ROM driver. Initialization, error
recovery, reliable TOC parsing, ISO9660 lookup, game loading, descrambling, and
hardware validation remain unfinished. See
[`DEVELOPMENT_GUIDE.md`](DEVELOPMENT_GUIDE.md) for the architecture, ABI,
limitations, and recommended development order.

## License and references

Original OpenDC source code is released under the [MIT License](LICENSE).
This license applies only to code and documentation contributed to OpenDC.

OpenDC is built alongside or may reference other projects, including
KallistiOS, DreamDash, Libronin, and iceGDROM. Those projects, their source
files, libraries, tools, firmware, and any BIOS/base images remain under their
respective licenses. They are not relicensed under MIT by this repository.

`REFERENCE.md` records the external KallistiOS, Libronin, and iceGDROM sources
used for research. The reference checkouts are not part of the Git source
tree. Review their licenses before copying code.

The project also uses KallistiOS and may package a BIOS base image. Verify the
licenses and redistribution rights for every dependency and generated image
before publishing releases.
