# External References & Credits

OpenDC references and builds upon research, specifications, and code patterns from several open-source projects in the Dreamcast community:

- **[KallistiOS](https://github.com/KallistiOS/KallistiOS)** — The standard open-source Dreamcast SDK and runtime environment. Used for register mappings, build tools, video clock tables, and the Stage 2 BIOS environment.
- **[iceGDROM](https://github.com/zeldin/iceGDROM)** — FPGA implementation and documentation of the Dreamcast G1 IDE bus and Sega Packet Interface (SPI).
- **[Libronin](https://github.com/sega-dreamcast/libronin)** — Low-level Dreamcast library by Marcus Comstedt and Peter Borsodi, providing reference implementations for ATA PIO timing, GD-ROM packet transport, and font routines.
- **[DreamShell](https://github.com/DC-SWAT/DreamShell)** — Modular Dreamcast operating system by DC-SWAT, pioneering G1-ATA filesystem handling and dynamic disc syscall hooking (`isoldr`).
- **[DreamBoot](https://github.com/Cpasjuste/dreamboot)** & **[DreamDash](https://github.com/darcagn/dreamdash)** — Open-source BIOS chainloaders by Cpasjuste and darcagn, establishing storage auto-detection patterns and BIOS ROM replacement structure.
- **[Flycast](https://github.com/flyinghead/flycast)** — Multi-platform Sega Dreamcast emulator by flyinghead and contributors. Essential for execution verification, register state testing, and live GDB debugging.
- **Marcus Comstedt's Hardware Documentation** — Foundation reverse engineering notes on the SH-4 bus state controller, cable sensing, and boot ROM execution.

---

## Licensing Boundaries

- Original OpenDC source code and documentation are released under the [MIT License](LICENSE).
- External reference projects retain their respective original licenses (BSD, GPL, LGPL, or MIT).
- No proprietary Sega BIOS binaries, Katana SDK objects, or copyrighted retail game assets are distributed as part of the OpenDC source repository.
