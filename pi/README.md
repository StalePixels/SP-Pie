# SP-Pie video service for the Raspberry Pi Zero 2 W

This is a bare-metal kernel for the Raspberry Pi Zero 2 W on the TK-Pie board. The CPLD on the board sends every Z80 memory write and I/O write to the Pi over SMI (the Pi's Secondary Memory Interface), each with its exact clock count. The Pi feeds every write, at its T-state, into the ULA display model and the memory paging of the [Fuse](https://fuse-emulator.sourceforge.net/) emulator, and shows the frame Fuse draws. Effects that depend on when the Spectrum writes memory or ports work: multicolour (attributes rewritten for each line), border changes inside a line, screen switching in the middle of a frame, pictures that alternate between frames, and ULAplus palette changes. The kernel also measures how much Pi time the work uses.

It is built on [Circle](https://github.com/rsta2/circle) for AArch64 (`RASPPI=3`, `kernel8.img`). The interface to the CPLD is in [../docs/protocol.md](../docs/protocol.md).

None of this has run on hardware yet. See [What is not verified](#what-is-not-verified).

## Architecture

### Cores

The Zero 2 W has four cores. The kernel uses two.

- **Core 0** starts Circle, the frame buffer, the SMI reader and Fuse, then runs the frames. Once per HDMI frame it runs the next complete Spectrum frame through Fuse and shows it. It also takes Circle's interrupts (the system timer).
- **Core 1** runs event intake. It polls the DMA position, decodes the new ring words into pairs, rebuilds each event's clock count, gives each event its frame and T-state, and writes the events to the event log. It also restarts or resyncs SMI. It takes no interrupts and never waits for core 0.
- **Cores 2 and 3** halt.

The two working cores share the event log (see [Event log](#event-log)) and a block of counters. Core 1 publishes the counters with atomic 64-bit stores, and core 0 reads them once per frame.

### SMI and the DMA ring

`src/smieventreader.cpp` owns the CPLD link.

- Circle's `CSMIMaster` switches GPIO 8 to 25 (SD0 to SD17) to SMI and sets the SMI clock and timing. It is built with `bUseAddressPins = FALSE`, so GPIO 0 to 5 stay untouched. The kernel switches GPIO 6 (SOE, the read strobe) to SMI itself, because `CSMIMaster` does not. GPIO 7 is never touched. SMI only does read transfers, so the Pi never drives SD0 to SD17.
- SMI runs in 18-bit mode with no packing, so each read is one 32-bit ring word with the read word in bits 17 to 0. A pair (an event or an empty pair) is two ring words.
- Circle's SMI driver has DMA for writes only. The reader programs a DMA channel itself. One control block covers the whole ring (512 K words, 2 MiB) and names itself as its next block, so the DMA loops over the ring for ever, paced by the SMI DREQ.
- The consumer reads the producer position from the DMA channel's destination address register. Before it reads ring words, it invalidates their cache lines, because the DMA writes memory behind the CPU cache.

SMI timing is in `src/smitiming.h`:

| Setting | Value |
|---|---|
| SMI clock | PLLD 500 MHz / 10 = 50 MHz, 20 ns |
| Setup / strobe / hold / pace | 3 / 4 / 3 / 0 clocks = 60 / 80 / 60 / 0 ns |
| One read | 200 ns, SOE low for 80 ns |
| One pair | 400 ns, so 2.5 million pairs per second |

The Z80 writes memory no faster than PUSH does, about 640,000 bytes per second, so the link reads about four times faster than the fastest event stream.

An SMI transfer has a fixed count of reads. The count is 65,534 (even, so a transfer ends on a pair boundary), which is about 13.1 ms of reads. The width of the SMI length register is not documented, and this count fits in 16 bits. When a transfer ends, core 1 starts the next one. Reads stop for the time between the end of one transfer and the start of the next. Core 1 measures an upper bound of that gap and shows the largest one on screen. The CPLD queue holds events during the gap.

### Pair check and SYNC

Each R0 must have H = 0 and each R1 H = 1. When one does not, core 1:

1. stops SMI reads,
2. waits 2 µs so the DMA takes what is already in the SMI FIFO,
3. holds SYNC (GPIO 26) high for 2 µs, then low for 2 µs before any read,
4. clears the SMI FIFO, skips every ring word up to the producer position, and starts reads again.

Each of these counts as one H mismatch. The kernel also pulses SYNC once at start-up, before the first read, and that pulse is not counted. The first pair after each SYNC is skipped: the CPLD sends it with a count of 0 that is not a time count.

### Decoder

`src/event.h` sorts each pair into one of four kinds, and `src/intake.cpp` keeps a pair together when it ends in the next batch of ring words.

- **Empty**: V = 0, nothing set but H and the count bits C4 to C0 in R0, and R1 exactly 0x08000.
- **Event**: V = 1 with valid fields: a memory write, an I/O write or a reset, with the full 16-bit address, the data byte, OVF, F and the count bits T4 to T0.
- **Mismatch**: a wrong H bit. The resync above follows.
- **Malformed**: correct H bits, but the pair is neither a valid empty pair nor a valid event: KIND 11, a reset event with a non-zero address or data, or an empty pair with other bits set. The pair is dropped and counted, with no resync.

An event with OVF = 1 is used as normal and counted. The events dropped before it cannot be recovered. A reset event can be among them.

### Clock count

`src/tstateclock.cpp` rebuilds the CPLD's full clock count from the 5-bit Gray-coded counts (protocol.md, Time base).

- **Empty pairs.** The CPLD loads an empty pair's R0, and takes its count, at the end of the read before it. The running count advances by the difference of two counts modulo 32. The number of whole wraps comes from the time between the two loads and the clock rate (3.5469 MHz): the advance is the value that matches the 5 bits and is nearest to that time. The time of each read comes from its position in the ring (`src/ringclock.h`): SMI reads at a fixed rate, every read lands in the ring, and each transfer start is timed. So the wraps across a transfer restart or a resync are counted as well.
- **Events.** An event is captured before the CPLD loads the next empty pair. Its count is the latest count at or below that pair's count that matches its 5 bits. This is exact while the event waited less than 32 clocks, about 9 µs.
- **Stops.** Before reads stop for a restart or a resync, events that wait for an empty pair are placed from the empty pair before them: the earliest count at or after it, less a two-read delay, that matches their 5 bits.
- **Long waits.** An event that waited 32 clocks or more, which only a stop in the reads that long can cause, may be placed a multiple of 32 clocks off. Such events are counted (`TAMB` on the overlay).

### Frame clock

`src/frameclock.cpp` turns a count into a frame number and a Fuse T-state.

The CPLD sets F on the first write after each interrupt acknowledge, which is the Z80 pushing its return address. Fuse starts the acknowledge at the first instruction boundary at or after T-state 0 of the frame, adds 7 T-states and then writes the first byte, at T-state 10 (`z80.c`, `z80_interrupt`). So an F event's count less 10 is the frame start plus the rest of the instruction the Z80 was running: 0 to about 22 T-states, and 0 to 3 in HALT.

- The first F event sets the frame start.
- An F event up to 64 T-states before the frame start moves the frame start back to it. One up to 64 T-states after it agrees with it.
- Eight F events in a row that agree with neither set the frame start again, from the last of them.
- Until the first F event, or with interrupts off from the start, frames run from an arbitrary start.

With a clock that runs without stopping, the frame start never moves in counts. Once one acknowledge has come exactly at an instruction boundary, every position is exact from then on.

**Limit.** A program locked to the cycle that always takes its interrupt at the same point after the start, for example a HALT that always ends 2 T-states late, never shows the true start. Every write is then placed that many T-states early, up to 3 in HALT, and the error does not change. Any frame before or after with an acknowledge at the start, such as BASIC or a loader, removes it.

Fuse makes a write take effect at a T-state of its own. A memory write takes effect at its count: /WR rises half a T-state before the end of the write cycle, and Fuse writes at the end of it (`memory_pages.c`, `writebyte`). An I/O write takes effect 3 T-states earlier: /WR rises half a T-state before the end of the 4 T-state I/O cycle, and Fuse writes the port one T-state after its start (`periph.c`, `writeport`). For a contended port write on the 48K and 128K the start of the cycle is not visible, so the write is placed as if uncontended. The constants are in `src/machine.h`.

Positions in the log never go backwards. An event that would land before the last logged position, or in a completed frame, gets that last position instead and is counted (`CLMP`). A frame is complete when the count of an empty pair is past its end by more than the CPLD's capture delay.

### Clocks per frame

The count between the F events of two frames in a row is the number of clocks in a frame, give or take the change in instruction time. The overlay shows the last one and the smallest and largest of the last 50 (`CPF`, `CMIN`, `CMAX`).

A clock that never stops gives 69,888 per frame on the 48K and 70,908 on the 128K class. If the edge-connector clock stops while the ULA holds the Z80 clock for contention, the count is Z80 clocks and not ULA T-states. Writes after contended accesses are then placed early by the stall so far in the frame, the frame length is short and changes with the program, and the 48K/128K test below can fail. The Sinclair Wiki says the 48K and 128K edge-connector clock stops for contention; it does not say for the +2A/+3. Nothing on the edge connector gives the ULA's own clock.

### Machine model

| | 48K | 128K, +2, +2A, +3 |
|---|---|---|
| T-states per frame (libspectrum `timings.c`) | 69,888 | 70,908 |
| Fuse machine | `48` | `128`, or `plus3` once port 0x1FFD is written |

The frame lengths come from libspectrum's timing tables at start-up. A frame length within 64 T-states of one of them, four frames in a row, selects that class; the 128K class is the start. The frame length cannot tell a 128K or +2 from a +2A or +3: they share the timing but decode the paging ports differently. Only the +2A/+3 has port 0x1FFD. Its ROM writes that port at start-up and each time it changes ROM, and the 128K and +2 ROMs never write it. So by default the kernel uses Fuse's 128K for the 128K class until it sees the first I/O write that matches Fuse's 0x1FFD decode (mask 0xF002, value 0x1000). From then on, until the kernel restarts, it uses Fuse's +3. The model applies per Spectrum frame, so the frame that holds that first write is already a +3 frame. A 128K program that writes to 0x1FFD selects the +3 by mistake; `auto128` prevents that.

The `machine` option in `cmdline.txt` on the SD card changes the choice:

| Value | Model |
|---|---|
| `auto` (default) | 48K; for the 128K class, 128K until the first 0x1FFD write, then +3 |
| `auto128` | 48K, or 128K for the 128K class |
| `48` | always 48K |
| `128` | always 128K |
| `plus3` | always +3 |

### Fuse

Fuse and libspectrum are submodules of `public/`, pinned to release tags and never modified:

| Path | Upstream | Pinned at | Licence |
|---|---|---|---|
| `../fuse` | `https://git.code.sf.net/p/fuse-emulator/fuse` | `fuse-1.9.2` | GPL-2.0-or-later |
| `../libspectrum` | `https://git.code.sf.net/p/fuse-emulator/libspectrum` | `libspectrum-1.8.0` | GPL-2.0-or-later |

The kernel is GPL-3.0-or-later and builds them in. `fuseula/fuseula.mk` lists the files used:

- Fuse: the display model (`display.c`, `rectangle.c`), memory and paging (`memory_pages.c`, `machine.c`, `spectrum.c`, and the 48K, 128K, +2, +2A and +3 machine files), the port decode (`periph.c`, `machines/machines_periph.c`) and the ULA port (`peripherals/ula.c`).
- libspectrum: the timing tables (`timings.c`), machine capabilities, memory allocation, the snapshot accessors and its GLib replacement.

Their own builds use autoconf. Here the headers autoconf and their Perl scripts make (`libspectrum.h`, `settings.h`, `options.h` and `snap_accessors.c`) are generated into `fuseula/gen` with their own scripts, and `fuseula/config.h` stands in for autoconf's `config.h`. The Fuse files are compiled into `fuseula/obj`, so the submodules stay clean. Their compiler warnings are turned off; ours are not.

Our layer in `fuseula/` sits between Fuse and the C++ code:

- `fuseula.h` and `fuseula.c`: the C interface. Each write is applied by setting Fuse's T-state counter and calling Fuse's `writebyte_internal` or `writeport_internal`; a reset calls Fuse's `machine_reset`; the end of a frame calls Fuse's `display_frame`. Fuse's drawing calls (`uidisplay_plot8`) land in the picture.
- `fusestubs.c`: definitions for the parts of Fuse that are not built (the Z80, sound, tape, disk, debugger, menus and other peripherals), each doing nothing or holding Fuse's value for an absent feature. ROMs are all zero; the display reads only RAM, and ROM pages are not writable.

Fuse's display covers 320 x 240 pixels: the screen with 32 pixels of border left and right and 24 lines above and below. The layer draws the rest of the 352 x 270 picture's border with Fuse's own beam rule: an 8-pixel chunk that starts at T-state T shows the state of T + 3 (`display.c`, `get_beam_position`). It records each border change Fuse makes with its T-state for this.

Fuse has no ULAplus, so the layer adds it, on every model: the register port 0xBF3B and the data port 0xFF3B, decoded on A15 to A14 = 10 or 11, A7 to A6 = 00, A2 = 0 and A0 = 1 (the TK-Pie board's decode). Data-port writes with register group 0 set palette entries and group 1 sets the mode; groups 2 and 3 are ignored. Mode bit 0 turns ULAplus on. In ULAplus mode, attribute bits 7 to 6 select one of four 16-entry groups, ink is entry group × 16 + INK and paper is group × 16 + 8 + PAPER, FLASH does not swap, and the border is entry 8 + border colour. A palette or mode change applies by the same chunk rule as the border. Fuse only redraws chunks whose pixel byte or attribute changed, so after a palette or mode change the layer has Fuse redraw the rest of that frame and the whole next frame. A reset turns ULAplus off and clears its register; the palette keeps its values.

The ULAplus colour of a GGGRRRBB palette entry: the missing low blue bit is the OR of the two stored blue bits, and the 3-bit levels scale to 0 to 255. Normal mode uses the Spectrum colours, 0xA0 per channel and 0xFF when BRIGHT.

### Event log

`src/eventlog.h` is the hand-off from core 1 to core 0. It is a ring of timed events (frame number, T-state, kind, address, data) with one writer and one reader, and no lock.

- Core 1 writes events in time order and publishes its write index with a release store. When the ring is full it drops the event and counts it. It never waits.
- When a frame is complete, core 1 writes a record for it (its model and whether the frame start is set) and then stores the count of complete frames with a release store.
- Core 0 loads the complete count with an acquire load, uses the frame's events, and publishes its read index with a release store.

A Z80 writing as fast as it can (PUSH) makes about 640,000 writes a second, about 13,000 a frame. The ring holds 131,072 events, about ten such frames. Core 0 keeps at most three frames waiting.

### Presentation

The frame buffer is 480 x 270 pixels at 32 bits per pixel, 16:9, which the firmware scales by exactly 4 to the HDMI mode. `config.txt` sets 1920 x 1080 at 50 Hz (CEA mode 31), with HDMI on even when no display is detected at start-up, and `force_turbo=1` keeps the ARM cores at their full 1000 MHz. The Spectrum picture is 352 x 270 pixels in the middle and fills the height: the 256 x 192 screen with 48 pixels of border left and right and 39 lines above and below. The World of Spectrum 48K reference gives the visible border as 48 pixels at each side, 48 lines above the screen and 56 below. The display shows all of the side border and the 39 lines nearest the screen above and below. The areas left and right of the picture are black.

`src/presenter.cpp` runs once per HDMI frame on core 0:

1. It runs the oldest complete Spectrum frame through Fuse: it selects the frame's model, applies each event at its T-state, and ends the frame. Fuse draws into a picture in RAM.
2. Core 0 copies the picture into the hidden half of a 480 x 540 frame buffer.
3. It moves the display to that half (`SetVirtualOffset`) and waits for vertical sync (`WaitForVerticalSync`).

The Spectrum makes frames a little faster than HDMI shows them: 50.08 Hz on a 48K and 50.02 Hz on the 128K class, against 50.00 Hz. When three or more complete frames are waiting, the presenter runs the oldest through Fuse without showing them until one will be left after the one shown. That skips one whole frame about every 12.5 s on a 48K and every 47 s on the 128K class. Every frame goes through Fuse, so FLASH and the picture stay right across skips. When no frame is complete, the last one is shown again. The screen only ever shows whole Spectrum frames.

The time from a Spectrum frame's end to its display is one to two HDMI frames.

## Building

You need:

- the Arm GNU toolchain for `aarch64-none-elf` on `PATH`,
- GNU make 4 or later (`gmake` on macOS),
- perl and a host C compiler, for the Fuse headers,
- the `fuse` and `libspectrum` submodules of `public/` checked out (`git submodule update --init fuse libspectrum` in `public/`),
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
gmake                # kernel8.img
gmake OVERLAY=0      # kernel8.img without the on-screen counters
```

`CIRCLE_WORLD=/path/to/circle-stdlib` uses a different rpi3 world. circle-stdlib puts the libc++ headers on the C include path; their `<stdatomic.h>` is for C++ only, so the Fuse C files get the compiler's own `<stdatomic.h>` first.

## SD card

Copy these files to the FAT partition of the SD card:

- `kernel8.img` from this directory,
- `config.txt` from this directory,
- the Raspberry Pi firmware files `bootcode.bin`, `start.elf`, `fixup.dat` and `bcm2710-rpi-zero-2-w.dtb`.

Circle's `boot/README` explains where the firmware files come from. In the rpi3 world it is at `../circle-libsdl2/circle-stdlib-rpi3/libs/circle/boot/README`.

To choose the Spectrum model, add a `cmdline.txt` with one line such as `machine=plus3` (see [Machine model](#machine-model)). Without it the kernel chooses from the frame length.

The board has no serial console. When start-up fails, the ACT LED blinks a code in a loop: 1 for Circle start-up, 2 for the frame buffer, 3 for the SMI reader or its DMA channel, 4 for starting the second core, 5 for Fuse.

## Instrumentation

With `OVERLAY=1` (the default), the counters are shown as text in the black areas left and right of the Spectrum picture. The overlay never draws over the picture or its border. Per-frame values are from the HDMI frame before; the others are since boot.

Left side:

| Label | Meaning |
|---|---|
| `EV` | Events in the last frame |
| `EMP` | Empty pairs read in the last frame |
| `FEV` | Events with F set in the last frame |
| `OVF` | Events with OVF set, since boot. Each one means at least one lost event |
| `HS` | H mismatches since boot. Each one caused a SYNC pulse |
| `BAD` | Malformed pairs since boot |
| `RST` | Reset events since boot. They are also counted in `EV` |
| `IN` | Core 1 time spent on ring words, restarts and resyncs, in µs per frame |
| `IDI` | Core 1 time spent polling with nothing new, in µs per frame |
| `RN` | Core 0 time to run the frame through Fuse, draw the overlay and copy, in µs |
| `IDR` | Core 0 time to request the flip and wait for vertical sync, in µs. This is the renderer's idle time |
| `BL` | Largest ring backlog seen, in words (one word is one read) |
| `RS` | SMI transfer restarts since boot |
| `GAP` | Largest restart gap seen, in µs (an upper bound) |
| `PB` | Times the DMA position read back outside the ring |
| `NE` | NOT_EMPTY (GPIO 27) at the end of the frame |

Right side:

| Label | Meaning |
|---|---|
| `MACH` | Model of the frame shown: `48K`, `128K` or `+2A/+3` |
| `LOCK` | 1 when the frame start is set from F events |
| `ACQ` | Times the frame start was set from scratch, since boot |
| `CPF` | Clocks between the F events of the last two frames in a row |
| `CMIN` | Smallest of the last 50 such values |
| `CMAX` | Largest of the last 50 such values |
| `SF` | Number of the Spectrum frame shown |
| `SKIP` | Spectrum frames not shown, since boot |
| `REP` | HDMI frames that showed the last Spectrum frame again, since boot |
| `LD` | Events lost because the event log was full, since boot |
| `CLMP` | Events moved to keep log positions in order, since boot |
| `TAMB` | Events whose count the 5 bits did not fix, since boot |
| `F` | HDMI frames since boot |
| `C` | ARM clock in MHz, read from the firmware at start-up |

`CPF`, `CMIN` and `CMAX` answer whether the edge-connector clock runs during contention. All three within a few dozen clocks of 70,908 on a 128K-class machine (69,888 on a 48K), whatever the program, means it runs without stopping. Values well below, which change with the program, mean it stops.

Times come from the ARM generic timer's physical counter (`CNTPCT_EL0`). Every core reads the same counter, it does not change when the firmware changes the CPU clock, and reading it touches no device. It counts at the rate the firmware sets in `CNTFRQ_EL0`. That rate is expected to be 19.2 MHz, but it has not been read on this board.

The Pi budget is the idle time per frame (`IDI` for core 1, `IDR` for core 0) under the worst stress test, with `OVF`, `HS`, `BAD`, `LD` and `TAMB` staying at zero and `BL` well below the ring size of 524,288 words.

## Host tests

The decoder, the intake, the clock count, the frame clock, the event log, the presenter, and Fuse with our layer build and run on the build machine with its own compilers. The Fuse files are generated and compiled into `test/build`.

```sh
gmake test
```

The timed tests use a simulated CPLD: Z80 writes at known clock counts, a four-entry queue that drops events when full, a two-read delay before the read side sees a new event, empty pairs that take the count when they are loaded, and SMI reads every 200 ns with stops between transfers.

The tests cover:

- the Gray code, the pair fields and time bits, empty pairs, H mismatches and malformed pairs,
- pairs split across batches of ring words, and the skipped first pair after SYNC,
- the clock count of every event in a steady stream, across a full queue with OVF, across restart gaps (one long enough to need the read times for its wraps), and an event that waited too long being counted,
- the frame start from the earliest acknowledge, the exact T-state of memory and I/O writes, and the clocks per frame,
- machine detection: the 48K by frame length, the +2A/+3 by its first 0x1FFD write, the `auto128` and fixed choices, and frames from a stopping clock leaving the class alone,
- in Fuse: a still picture and the border inside and outside Fuse's area, multicolour per line including a write one T-state too late, border changes inside a line, a 0x7FFD screen switch in the middle of a frame and a reset, the +2A/+3 paging decode, and ULAplus palette and mode, with a palette change in the middle of a frame,
- whole frames: skips and repeats in the presenter,
- end to end, through the simulated CPLD, the intake, the clock count, the frame clock, the log and Fuse: multicolour per line, border changes inside a line, and a restart gap in each frame, with a different acknowledge delay each frame.

At the end the tests print the time core 0 takes to run a frame of 13,000 screen writes through Fuse, and the time core 1's code takes for one frame of words at the worst write rate, on the build machine.

## Cost

On an Apple Silicon build machine the tests measure about 0.13 ms for core 0 to run a frame of 13,000 screen writes through Fuse, and about 0.35 to 0.45 ms for core 1's code to decode and place one frame of words (100,000 ring words, 14,000 events). A Cortex-A53 at 1000 MHz is assumed to be about ten times slower: about 1.3 ms of core 0's 20 ms per frame, plus the copy of the 480 x 270 picture to the frame buffer, and about 4.5 ms of core 1's 20 ms, not counting the DMA and SMI register reads of each loop pass. These are estimates; `RN`, `IDR`, `IN` and `IDI` on the overlay give the real figures.

## What is not verified

No hardware was attached during development. Everything below is untested:

- SMI on the Zero 2 W in 18-bit mode, and whether the CPLD keeps up with the chosen timing.
- That in 18-bit mode with no packing each read lands as one 32-bit FIFO word with the data in bits 17 to 0.
- The SMI clock source. Circle assumes PLLD at 500 MHz. If that is wrong, every timing above scales with it, and so do the read times used for the wraps across stops.
- The width of the SMI length register, and so the largest transfer count.
- Whether clearing the SMI enable bit stops a transfer at once.
- Whether SMI leaves SD0 to SD17 undriven at all times when it only does reads.
- Whether a word is in memory by the time the DMA destination register has moved past it. If it is not, the consumer can read a stale word.
- Whether the firmware applies the virtual offset at the next vertical sync. If it applies it at once, a flip can land mid-scan, between two complete frames.
- That an SMI transfer gives exactly 65,534 ring words, and that reads run at exactly the read period from the moment core 1 starts a transfer. A late first read shortens the time used for the wraps across that stop.
- Whether the edge-connector clock runs during contention, on each model (see [Clocks per frame](#clocks-per-frame)).
- Whether the /WR rising edge can meet a counting edge of the edge-connector clock on the +2A/+3, which can make a count one clock off (see `../cpld/README.md`).
- That Fuse's write points (memory at the end of the cycle, I/O one T-state after its start) match the relation of /WR to the ULA on the real machines; Fuse is calibrated against the real machines with its own Z80 timing, which is not run here.
- Whether the firmware scales the 480 x 270 frame buffer to 1080p with sharp pixels or with smoothing.
- The real restart gaps, backlog and CPU times. They exist only once the kernel runs on the board.
