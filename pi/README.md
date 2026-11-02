# SP-Pie video service for the Raspberry Pi Zero 2 W

This is a bare-metal kernel for the Raspberry Pi Zero 2 W on the TK-Pie board. It reads Spectrum bus events from the CPLD over SMI (the Pi's Secondary Memory Interface) and shows the Spectrum picture on HDMI. It handles the normal and shadow screens, the border colour, FLASH and ULAplus, and it measures how much Pi time the work uses.

It is built on [Circle](https://github.com/rsta2/circle) for AArch64 (`RASPPI=3`, `kernel8.img`). The interface to the CPLD is in [../docs/protocol.md](../docs/protocol.md).

None of this has run on hardware yet. See [What is not verified](#what-is-not-verified).

## Architecture

### Cores

The Zero 2 W has four cores. The kernel uses two.

- **Core 0** starts Circle, the frame buffer and the SMI reader, then runs the renderer. It also takes Circle's interrupts (the system timer).
- **Core 1** runs event intake. It polls the DMA position, decodes new events into the state model, and restarts or resyncs SMI. It takes no interrupts and never waits on anything, so it reacts to new data within one loop pass.
- **Cores 2 and 3** halt.

The two working cores share two things. Core 1 writes the state model and core 0 reads it, with no lock. Every field is one byte, so the renderer sees each byte either before or after a write, and a frame may show part of a Spectrum frame's writes, as the real ULA does. Core 1 also publishes cumulative counters with atomic 64-bit stores, and core 0 reads them once per frame.

### SMI and the DMA ring

`src/smieventreader.cpp` owns the CPLD link.

- Circle's `CSMIMaster` switches GPIO 8 to 23 (SD0 to SD15) to SMI and sets the SMI clock and timing. It is built with `bUseAddressPins = FALSE`, so GPIO 0 to 5 stay untouched. The kernel switches GPIO 6 (SOE, the read strobe) to SMI itself, because `CSMIMaster` does not. GPIO 7, 24 and 25 are never touched. SMI only does read transfers, so the Pi never drives SD0 to SD15.
- SMI runs in 16-bit mode with pixel packing, so two reads fill one 32-bit FIFO word. One event pair from the CPLD lands as one word, half-word 0 in bits 15 to 0.
- Circle's SMI driver has DMA for writes only. The reader programs a DMA channel itself. One control block covers the whole ring (256 K words, 1 MiB) and names itself as its next block, so the DMA loops over the ring for ever, paced by the SMI DREQ.
- The consumer reads the producer position from the DMA channel's destination address register. Before it reads ring words, it invalidates their cache lines, because the DMA writes memory behind the CPU cache.

SMI timing is in `src/smieventreader.h`:

| Setting | Value |
|---|---|
| SMI clock | PLLD 500 MHz / 10 = 50 MHz, 20 ns |
| Setup / strobe / hold / pace | 3 / 4 / 3 / 0 clocks = 60 / 80 / 60 / 0 ns |
| One read | 200 ns, SOE low for 80 ns |
| One event pair | 400 ns, so 2.5 million pairs per second |

The Z80 writes memory no faster than PUSH does, about 640,000 bytes per second, so the link reads about four times faster than the fastest event stream.

An SMI transfer has a fixed count of reads. The count is 65,534 (even, so a transfer ends on a pair boundary), which is about 13.1 ms of reads. The width of the SMI length register is not documented, and this count fits in 16 bits. When a transfer ends, core 1 starts the next one. Reads stop for the time between the end of one transfer and the start of the next. Core 1 measures an upper bound of that gap (from the last poll that saw the transfer running to the new start) and shows the largest one on screen. The CPLD queue holds events during the gap.

### Pair check and SYNC

Each word must have H = 0 in half-word 0 and H = 1 in half-word 1. When one does not, core 1:

1. stops SMI reads,
2. waits 2 µs so the DMA takes what is already in the SMI FIFO,
3. holds SYNC (GPIO 26) high for 2 µs, then low for 2 µs before any read,
4. clears the SMI FIFO, skips every ring word up to the producer position, and starts reads again.

Each of these counts as one H mismatch. The kernel also pulses SYNC once at start-up, before the first read, and that pulse is not counted.

### Decoder

`src/event.h` sorts each word into one of four kinds.

- **Empty**: exactly 0x0000 then 0x8000.
- **Event**: V = 1 with valid fields. It is applied to the state.
- **Mismatch**: a wrong H bit. The resync above follows.
- **Malformed**: correct H bits, but the word is not the exact empty pair and not a valid event. This covers TYPE 0, a set reserved bit, a screen OFFSET at or above 0x1B00, or a non-zero OFFSET on a port event. The word is dropped and counted, with no resync.

An event with OVF = 1 is applied as normal and counted. The events dropped before it cannot be recovered.

### State model

`src/spectrumstate.cpp` holds what the picture depends on.

- Banks 5 and 7, 6,912 bytes each (pixels and attributes).
- The border colour (port 0xFE bits 2 to 0).
- Port 0x7FFD. Bit 3 selects the shown screen: bank 5 when clear, bank 7 when set.
- Port 0x1FFD. Stored for completeness, not used by the picture.
- The ULAplus register, the 64-entry palette and the mode byte. Data-port writes with register group 0 set palette entries and group 1 sets the mode. Groups 2 and 3 are ignored. Mode bit 0 turns ULAplus on. The optional greyscale bit is not used.

The Pi learns nothing from a Spectrum reset. After a reset, the state keeps its last values until new events arrive.

### Renderer

`src/renderer.cpp` draws a 320 x 240 picture: the 256 x 192 Spectrum screen with 32 pixels of border left and right and 24 top and bottom. The firmware scales it to the HDMI mode.

The frame buffer is 32 bits per pixel, 320 x 480: two 320 x 240 halves. Each frame:

1. The renderer draws into a buffer in RAM.
2. It copies the buffer into the hidden half of the frame buffer.
3. It moves the display to that half (`SetVirtualOffset`) and waits for vertical sync (`WaitForVerticalSync`).

The screen only ever shows a finished frame.

32 bits per pixel gives the exact colours in both modes with no palette changes. Normal mode uses the Spectrum colours (0xA0 per channel, 0xFF when BRIGHT). ULAplus mode converts each GGGRRRBB palette entry to 24-bit colour. Changing mode is a renderer flag, with no new frame buffer and no mailbox call. The cost over 8 bits per pixel is 300 KB of memory traffic per frame instead of 75 KB, which is small for this Pi.

FLASH counts renderer frames and changes phase every 16 frames. The renderer runs at the HDMI frame rate, which is usually 60 Hz, so FLASH is faster than on a 50 Hz Spectrum. In ULAplus mode FLASH selects a palette group and does not flash.

## Building

You need:

- the Arm GNU toolchain for `aarch64-none-elf` on `PATH`,
- GNU make 4 or later (`gmake` on macOS),
- bash 5 and GNU getopt to build the Circle world (see [../circle-libsdl2/docs/BUILDING.md](../circle-libsdl2/docs/BUILDING.md)).

### 1. Build the Circle world for the Pi 3 class

The kernel links against the rpi3 world inside `circle-libsdl2`. Build only that one:

```sh
cd ../circle-libsdl2
gmake world BOARD=rpi3
```

The first build compiles newlib and libc++ and takes a long time. It also clones a libc++ source tree, by default to `circle-llvm` beside `circle-libsdl2`. `CIRCLE_LLVM=/some/path` puts that clone somewhere else.

### 2. Build the kernel

```sh
gmake                # kernel8.img, with the on-screen counters
gmake OVERLAY=0      # kernel8.img without them
```

`CIRCLE_WORLD=/path/to/circle-stdlib` uses a different rpi3 world.

## SD card

Copy these files to the FAT partition of the SD card:

- `kernel8.img` from this directory,
- `config.txt` from this directory,
- the Raspberry Pi firmware files `bootcode.bin`, `start.elf`, `fixup.dat` and `bcm2710-rpi-zero-2-w.dtb`.

Circle's `boot/README` explains where the firmware files come from. In the rpi3 world it is at `../circle-libsdl2/circle-stdlib-rpi3/libs/circle/boot/README`.

The board has no serial console. When start-up fails, the ACT LED blinks a code in a loop: 1 for Circle start-up, 2 for the frame buffer, 3 for the SMI reader or its DMA channel, 4 for starting the second core.

## Instrumentation

With `OVERLAY=1` (the default), three lines of text in the bottom border show the counters. Each value is from the frame before, or since boot where marked.

| Label | Meaning |
|---|---|
| `EV` | Events applied in the last frame |
| `EMP` | Empty pairs read in the last frame |
| `OVF` | Events with OVF set, since boot. Each one means at least one lost event |
| `HS` | H mismatches since boot. Each one caused a SYNC pulse |
| `BAD` | Malformed words since boot |
| `IN` | Intake core time spent on ring words, restarts and resyncs, in µs per frame |
| `IDI` | Intake core time spent polling with nothing new, in µs per frame |
| `RN` | Renderer time to draw, overlay and copy the frame, in µs |
| `IDR` | Renderer time to request the flip and wait for vertical sync, in µs. This is the renderer's idle time |
| `BL` | Largest ring backlog seen, in words (one word is one pair) |
| `RS` | SMI transfer restarts since boot |
| `GAP` | Largest restart gap seen, in µs (an upper bound) |
| `PB` | Times the DMA position read back outside the ring |
| `NE` | NOT_EMPTY (GPIO 27) at the end of the frame |
| `C` | ARM clock in MHz, read from the firmware at start-up |
| `F` | Frames since boot |

Times come from the ARM generic timer's physical counter (`CNTPCT_EL0`). Every core reads the same counter, it does not change when the firmware changes the CPU clock, and reading it touches no device. It counts at the rate the firmware sets in `CNTFRQ_EL0`. That rate is expected to be 19.2 MHz (about 52 ns a tick), which is fine for per-frame totals, but it has not been read on this board. The PMU cycle counter was not used: it counts CPU cycles, so its numbers change with the CPU clock, and Circle does not set it up.

The Pi budget is the idle time per frame (`IDI` for the intake core, `IDR` for the renderer core) under the worst stress test, with `OVF`, `HS` and `BAD` staying at zero and `BL` well below the ring size of 262,144 words.

## Host tests

The decoder, the intake loop, the state model and the renderer build and run on the build machine with its own C++ compiler. The tests feed synthetic event streams through the same code the Pi runs and check the resulting pixels.

```sh
gmake test
```

The tests cover:

- event fields, the exact empty pair, H mismatches and malformed words,
- OVF counting, and the stop at an H mismatch,
- a known pixel and attribute pattern,
- the shadow screen switch,
- a border change, and a 0xFE plus 0x1FFD write arriving as two events,
- both FLASH phases,
- ULAplus colour conversion, palette upload, and mode on and off.

## What is not verified

No hardware was attached during development. Everything below is untested:

- SMI on the Zero 2 W, and whether the CPLD keeps up with the chosen timing.
- The SMI clock source. Circle assumes PLLD at 500 MHz. If that is wrong, every timing above scales with it.
- The width of the SMI length register, and so the largest transfer count.
- How pixel packing orders the two half-words. This code follows the protocol: half-word 0 in bits 15 to 0.
- Whether clearing the SMI enable bit stops a transfer at once.
- Whether SMI leaves SD0 to SD15 undriven at all times when it only does reads.
- Whether a word is in memory by the time the DMA destination register has moved past it. If it is not, the consumer can read a stale word.
- Whether the firmware applies the virtual offset at the next vertical sync. If it applies it at once, a flip can land mid-scan, between two complete frames.
- The real restart gaps, backlog and CPU times. They exist only once the kernel runs on the board.
