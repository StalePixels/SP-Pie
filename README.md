# SP-Pie

Alternative CPLD logic and Raspberry Pi firmware for the TK-Pie board.

The TK-Pie board plugs into a ZX Spectrum and gives it an HDMI output. On the board, a CPLD chip watches what the Spectrum does, and a Raspberry Pi makes the picture and sound from it. SP-Pie replaces the software on both.

> [!WARNING]
> Do not use the SP-Pie Pi firmware with the original TK-Pie CPLD firmware. This combination can damage the Raspberry Pi or the board. Install the SP-Pie CPLD logic from [`cpld/`](cpld/README.md) first.

## Requirements

- A Raspberry Pi Zero 2 W only.
- The SP-Pie CPLD logic from [`cpld/`](cpld/README.md) in the board's CPLD.

## Contents

| Folder | What it holds |
| --- | --- |
| [`cpld/`](cpld/README.md) | The CPLD logic, in VHDL. |
| [`pi/`](pi/README.md) | The Raspberry Pi firmware. |
| [`docs/`](docs/) | The [CPLD to Pi protocol](docs/protocol.md) and the [control ports](docs/control-ports.md). |
| [`tests/`](tests/) | Spectrum programs: [display tests](tests/spectrum/README.md), [sound tests](tests/sound/README.md) and a [Gigascreen and ULAplus demo](tests/demo/README.md). |
| `fuse/`, `libspectrum/` | Fuse and libspectrum, as submodules. |
| `circle-libsdl2/` | Circle and circle-stdlib for the Pi build, as a submodule. |

Clone with the submodules:

```
git clone --recursive https://github.com/StalePixels/SP-Pie.git
```

The README in each folder tells how to build it.

## Credits

- **TK-Pie**, the board, its schematics and its original firmware, by Victor Trucco, copyright 2016-2017, BSD-style licence. <https://gitlab.com/victor.trucco/TK-Pie>. SP-Pie uses the board's pin assignments.
- **Fuse**, the Free Unix Spectrum Emulator, copyright Philip Kendall and others, GPL-2.0-or-later. [`fuse/AUTHORS`](fuse/AUTHORS) lists the contributors.
- **libspectrum**, copyright Philip Kendall and others, GPL-2.0-or-later. [`libspectrum/AUTHORS`](libspectrum/AUTHORS) lists the contributors.
- **Blip_Buffer**, which turns Fuse's sound into samples, copyright Shay Green, LGPL-2.1-or-later. Gergely Szasz rewrote it in C for Fuse.
- **Circle**, the bare-metal C++ environment for the Raspberry Pi, copyright Rene Stange, GPL-3.0-or-later. <https://github.com/rsta2/circle>.
- **circle-stdlib**, the C and C++ standard libraries for Circle, by Stephan Mühlstrasser, GPL-3.0. <https://github.com/smuehlst/circle-stdlib>.
- **circle-libsdl2**, an SDL2-compatible library for Circle, copyright D. Rimron-Soutter, zlib licence. <https://github.com/Xalior/circle-libsdl2>. SP-Pie uses only the Circle and circle-stdlib build inside it.
- **Pentagon Gigascreen**, the hardware design by Dmitry Pugachev (SchemeMan, Dark Souls Group). [Interlace mixing](pi/README.md#interlace-mixing) cites the sources.
- **Macaw photograph** in the demo, "Ara ararauna portrait Rhodes" by Jebulon, CC0 1.0, from Wikimedia Commons.

## Licence

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
