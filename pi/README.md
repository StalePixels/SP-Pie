# SP-Pie video service for the Raspberry Pi Zero 2 W

This is a bare-metal kernel for the Raspberry Pi Zero 2 W on the TK-Pie board. The CPLD on the board sends every Z80 memory write and I/O write to the Pi over SMI (the Pi's Secondary Memory Interface). The Pi keeps its own copy of the Spectrum's RAM and ports and works like a ULA: it draws each Spectrum frame line by line, using the state at the moment the real ULA reads each part of the screen. Effects that depend on when the Spectrum writes memory or ports work: multicolour (attributes rewritten for each line), border changes inside a line, screen switching in the middle of a frame, pictures that alternate between frames, and ULAplus palette changes. The kernel also measures how much Pi time the work uses.

It is built on [Circle](https://github.com/rsta2/circle) for AArch64 (`RASPPI=3`, `kernel8.img`). The interface to the CPLD is in [../docs/protocol.md](../docs/protocol.md).

None of this has run on hardware yet. See [What is not verified](#what-is-not-verified).

## Architecture

### Cores

The Zero 2 W has four cores. The kernel uses two.

- **Core 0** starts Circle, the frame buffer and the SMI reader, then runs the renderer. Once per HDMI frame it draws the next complete Spectrum frame and shows it. It also takes Circle's interrupts (the system timer).
- **Core 1** runs event intake. It polls the DMA position, puts a time on each new event, locks to the Spectrum frame, and writes the events to the event log. It also restarts or resyncs SMI. It takes no interrupts and never waits for core 0.
- **Cores 2 and 3** halt.

The two working cores share the event log (see [Event log](#event-log)) and a block of counters. Core 1 publishes the counters with atomic 64-bit stores, and core 0 reads them once per frame.

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

`src/event.h` sorts each ring word into one of four kinds.

- **Empty**: exactly 0x0000 then 0x8000.
- **Event**: V = 1 with valid fields: a memory write, an I/O write or a reset, with the full 16-bit address, the data byte, OVF and F.
- **Mismatch**: a wrong H bit. The resync above follows.
- **Malformed**: correct H bits, but the word is not the exact empty pair and not a valid event: KIND 11, a set bit 9, or a reset event with a non-zero address or data. The word is dropped and counted, with no resync.

An event with OVF = 1 is used as normal and counted. The events dropped before it cannot be recovered. A reset event can be among them.

### Time stamps

Core 1 reads the DMA producer position, then the ARM generic timer (`CNTPCT_EL0`). Every event in the new ring words gets that time: the moment core 1 first saw it. The CPLD queue is almost always empty and core 1 polls without a break, so this is a short, nearly fixed time after the Z80 wrote.

### Frame lock

`src/framelock.cpp` ties the Pi's timer to the Spectrum frame. It uses the events with F = 1: the CPLD sets F on the first write after each interrupt acknowledge, which is the Z80 pushing its return address. Such an event arrives at

> interrupt time + Z80 delay + transport latency

The Z80 delay is about 10 T-states (the 7 T-state acknowledge and the first push, from the Zilog Z80 CPU User Manual) plus the rest of the instruction the Z80 was running, up to about 23 T-states. The transport latency (CPLD queue, SMI read, DMA, the core 1 poll) varies by under a microsecond. Neither is ever negative, so the earliest F arrivals lie on a straight line, one point per frame. The lock estimates that line.

- **Acquisition.** A run of F events, close together, gives a first period (time span divided by frames) and the line through the earliest of them.
- **Tracking.** F events are grouped in blocks of frames. The earliest arrival of each block is one point. After each block, a least-squares line through the recent points corrects the period and the phase.
- **Flywheel.** With no F events (a program that runs with interrupts off), the line goes on with the last period and phase.
- **Re-acquisition.** When the newest points lie far off the line, the lock acquires again. The old line keeps running meanwhile.

The frame start is the interrupt time, as seen in arrival time: the line minus the Z80 delay of the earliest arrivals, plus the calibration constant below. An event's arrival time minus the frame start is its time in the frame, which gives its frame number and T-state.

Until the first lock the kernel runs free on the nominal 128K period, with an arbitrary phase. The picture is then correct apart from timing effects.

### Machine timing and model

The measured period selects the timing. The constants are in `src/machinetiming.h`, with their sources (World of Spectrum FAQ, 48K and 128K technical references).

| | 48K | 128K, +2, +2A, +3 |
|---|---|---|
| Z80 clock | 3.5 MHz | 3.5469 MHz |
| T-states per line | 224 | 228 |
| Lines per frame | 312 | 311 |
| T-states per frame | 69,888 | 70,908 |
| Frame period | 19.968 ms | 19.9915 ms |
| First screen pixel | T-state 14,336 (line 64) | T-state 14,364 (line 63) |

A period below the midpoint is the 48K class. The period cannot tell a 128K or +2 from a +2A or +3: they share the timing but decode the paging ports differently. Only the +2A/+3 has port 0x1FFD. Its ROM writes that port at start-up and each time it changes ROM, and the 128K and +2 ROMs never write it. So by default the kernel uses the 128K/+2 decode for the 128K class until it sees the first I/O write that matches the 0x1FFD decode (A15..A12 = 0001, A1 = 0). From then on, until the kernel restarts, it uses the +2A/+3 decode. The model applies per Spectrum frame, so the frame that holds that first write is already decoded as a +2A/+3. A 128K program that writes to 0x1FFD selects the +2A/+3 decode by mistake; `auto128` prevents that.

The `machine` option in `cmdline.txt` on the SD card changes the choice:

| Value | Model |
|---|---|
| `auto` (default) | 48K; for the 128K class, 128K/+2 until the first 0x1FFD write, then +2A/+3 |
| `auto128` | 48K, or 128K/+2 for the 128K class |
| `48` | always 48K |
| `128` | always 128K/+2 |
| `plus3` | always +2A/+3 |

### State model

`src/spectrumstate.cpp` is the Pi's copy of the Spectrum. It changes only through events, and it decodes each write as the chosen model does.

- All eight 16 KB RAM banks. A memory write goes to the bank that the paging maps at its address. Writes to ROM change nothing.
- Port 0xFE, decoded on A0 = 0 on every model: the border colour (bits 2 to 0).
- Port 0x7FFD: on the 128K/+2 any port with A15 = 0 and A1 = 0; on the +2A/+3 A15 to A14 = 01 and A1 = 0; none on the 48K. Bits 2 to 0 select the bank at 0xC000, bit 3 the shown screen (bank 5 or bank 7), and bit 5 locks both paging ports until reset.
- Port 0x1FFD, on the +2A/+3 only: A15 to A12 = 0001 and A1 = 0. Bit 0 turns on special paging, and bits 2 and 1 select one of its four layouts.
- ULAplus, on every model: the register port 0xBF3B and the data port 0xFF3B, decoded on A15 to A14 = 10 or 11, A7 to A6 = 00, A2 = 0 and A0 = 1. Data-port writes with register group 0 set palette entries and group 1 sets the mode. Groups 2 and 3 are ignored. Mode bit 0 turns ULAplus on. The optional greyscale bit is not used.

One write that matches several ports acts on each of them, as on the real machine (for example port 0x7FFC on a 128K sets both the border and the paging).

A reset event sets ports 0x7FFD and 0x1FFD to 0, which also unlocks paging, and sets the ULAplus mode and register to 0. RAM, the border colour and the ULAplus palette keep their values.

Only core 0 uses the state. It applies each event at the event's T-state while it draws, so the state is always the state at one moment of the Spectrum frame.

### Event log

`src/eventlog.h` is the hand-off from core 1 to core 0. It is a ring of timed events (frame number, T-state, kind, address, data) with one writer and one reader, and no lock.

- Core 1 writes events in time order and publishes its write index with a release store. When the ring is full it drops the event and counts it. It never waits.
- When the time is past the end of a frame, core 1 writes a record for the frame (its model and lock state) and then stores the count of complete frames with a release store. An event seen later has a later time stamp, so it belongs to a later frame.
- Core 0 loads the complete count with an acquire load, uses the frame's events, and publishes its read index with a release store.

Positions in the log never go backwards. When a lock correction moves the line back a little, an event that would land before the last logged position, or in a complete frame, gets the last position.

A Z80 writing as fast as it can (PUSH) makes about 640,000 writes a second, about 13,000 a frame. The ring holds 131,072 events, about ten such frames. Core 0 keeps at most three frames waiting.

### Renderer

The frame buffer is 640 x 360 pixels at 32 bits per pixel, 16:9, which the firmware scales by exactly 3 to the HDMI mode. `config.txt` sets 1920 x 1080 at 50 Hz (CEA mode 31), with HDMI on even when no display is detected at start-up. The Spectrum picture is 352 x 296 pixels in the middle: the 256 x 192 screen with 48 pixels of border left and right, 48 lines above and 56 below. These are the border sizes in the World of Spectrum 48K reference ("The border is 48 pixels wide at each side"; of the 64 lines before the screen "at least the last 48 of these are actual border-lines"; then "56 border lines"). The areas left and right of the picture are black.

`src/renderer.cpp` draws a frame in 8-pixel blocks, 44 a line, in the order the ULA shows them. 1 T-state is two pixels, so each block has a T-state; the first screen byte of each line is at the line's first-pixel T-state. Before each block, the renderer applies every event of the frame before that T-state. A screen block then takes its pixel byte, attribute, screen bank (port 0x7FFD bit 3) and colours from the state, and a border block its border colour. The ULA changes the border only at these 8-pixel blocks, every 4 T-states (World of Spectrum: "you cannot change the border within a 'byte', an 8-pixel chunk"). After the last block, the rest of the frame's events are applied, which leaves the state at the start of the next frame.

32 bits per pixel gives the exact colours in both modes with no palette changes. Normal mode uses the Spectrum colours (0xA0 per channel, 0xFF when BRIGHT). ULAplus mode converts each GGGRRRBB palette entry to 24-bit colour; the renderer keeps the converted palette and updates an entry on each palette write, so a palette change in the middle of a frame shows from the next block.

FLASH counts Spectrum frames and changes phase every 16 frames. In ULAplus mode FLASH selects a palette group and does not flash.

### Presentation

`src/presenter.cpp` runs once per HDMI frame on core 0:

1. It draws the oldest complete Spectrum frame into a buffer in RAM.
2. Core 0 copies the picture into the hidden half of a 640 x 720 frame buffer.
3. It moves the display to that half (`SetVirtualOffset`) and waits for vertical sync (`WaitForVerticalSync`).

The Spectrum makes frames a little faster than HDMI shows them: 50.08 Hz on a 48K and 50.02 Hz on the 128K class, against 50.00 Hz. When three or more complete frames are waiting, the presenter applies the oldest without drawing them until one will be left after the drawn one. That skips one whole frame about every 12.5 s on a 48K and every 47 s on the 128K class. When no frame is complete, the last one is shown again. The screen only ever shows whole Spectrum frames. A picture that alternates on alternate frames shows its phase change once at each skip.

The time from a Spectrum frame's end to its display is one to two HDMI frames.

### Accuracy and calibration

An event's position in the frame is only as good as its time stamp and the lock.

- **Arrival jitter.** The transport latency spreads over about 0.8 µs: the CPLD shows a new event two SMI reads late (200 ns each), and core 1 sees new words once per poll. 0.8 µs is about 2.8 T-states, or about 6 pixels. This is an estimate; it has not been measured.
- **Lock phase.** The earliest F arrival in a block of frames has a Z80 delay a little above the minimum, depending on the program's instructions. A program that waits in HALT reaches the minimum within 3 T-states.
- **Machines.** The World of Spectrum FAQ notes that some machines are one T-state later than others for all timings.

In the host tests' simulation (Z80 delay 0 to 22 T-states, latency spread 0.8 µs) the position error stays within 4 T-states, with a mean of about 1.6 T-states early. 4 T-states is one 8-pixel block. A write that lands within a few T-states of the moment the ULA reads a cell can therefore show one line or one border block early or late. Multicolour engines that write well ahead of the ULA are not affected.

The one calibration constant is `TransportCalibrationNs` in `src/machinetiming.h`. It is the difference between the latency of an ordinary event and the smallest latency the lock sees, and it also absorbs any constant error of the Z80 delay above. It is set to 400 ns, half the estimated latency spread. A larger value moves every event later in the frame. It must be set from a hardware test that writes the border at a known T-state.

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
gmake                # kernel8.img
gmake OVERLAY=1      # kernel8.img with the on-screen counters
```

`CIRCLE_WORLD=/path/to/circle-stdlib` uses a different rpi3 world.

## SD card

Copy these files to the FAT partition of the SD card:

- `kernel8.img` from this directory,
- `config.txt` from this directory,
- the Raspberry Pi firmware files `bootcode.bin`, `start.elf`, `fixup.dat` and `bcm2710-rpi-zero-2-w.dtb`.

Circle's `boot/README` explains where the firmware files come from. In the rpi3 world it is at `../circle-libsdl2/circle-stdlib-rpi3/libs/circle/boot/README`.

To choose the Spectrum model, add a `cmdline.txt` with one line such as `machine=plus3` (see [Machine timing and model](#machine-timing-and-model)). Without it the kernel chooses from the frame period.

The board has no serial console. When start-up fails, the ACT LED blinks a code in a loop: 1 for Circle start-up, 2 for the frame buffer, 3 for the SMI reader or its DMA channel, 4 for starting the second core.

## Instrumentation

With `OVERLAY=1`, the counters are shown as text in the black areas left and right of the Spectrum picture. The overlay never draws over the picture or its border. Per-frame values are from the HDMI frame before; the others are since boot.

Left side:

| Label | Meaning |
|---|---|
| `EV` | Events in the last frame |
| `EMP` | Empty pairs read in the last frame |
| `FEV` | Events with F set in the last frame |
| `OVF` | Events with OVF set, since boot. Each one means at least one lost event |
| `HS` | H mismatches since boot. Each one caused a SYNC pulse |
| `BAD` | Malformed words since boot |
| `RST` | Reset events since boot. They are also counted in `EV` |
| `IN` | Core 1 time spent on ring words, restarts and resyncs, in µs per frame |
| `IDI` | Core 1 time spent polling with nothing new, in µs per frame |
| `RN` | Core 0 time to draw the frame, the overlay and the copy, in µs |
| `IDR` | Core 0 time to request the flip and wait for vertical sync, in µs. This is the renderer's idle time |
| `BL` | Largest ring backlog seen, in words (one word is one pair) |
| `RS` | SMI transfer restarts since boot |
| `GAP` | Largest restart gap seen, in µs (an upper bound) |
| `PB` | Times the DMA position read back outside the ring |
| `NE` | NOT_EMPTY (GPIO 27) at the end of the frame |

Right side:

| Label | Meaning |
|---|---|
| `MACH` | Model of the frame shown: `48K`, `128K` or `+2A/+3` |
| `LOCK` | 1 when the frame lock is locked |
| `PER` | Measured Spectrum frame period, in ns |
| `ACQ` | Frame lock acquisitions since boot |
| `SF` | Number of the Spectrum frame shown |
| `SKIP` | Spectrum frames not shown, since boot |
| `REP` | HDMI frames that showed the last Spectrum frame again, since boot |
| `LD` | Events lost because the event log was full, since boot |
| `CLMP` | Events moved to keep log positions in order, since boot |
| `F` | HDMI frames since boot |
| `C` | ARM clock in MHz, read from the firmware at start-up |

Times come from the ARM generic timer's physical counter (`CNTPCT_EL0`). Every core reads the same counter, it does not change when the firmware changes the CPU clock, and reading it touches no device. It counts at the rate the firmware sets in `CNTFRQ_EL0`. That rate is expected to be 19.2 MHz (about 52 ns a tick, a fifth of a T-state), but it has not been read on this board. The PMU cycle counter was not used: it counts CPU cycles, so its numbers change with the CPU clock, and Circle does not set it up.

The Pi budget is the idle time per frame (`IDI` for core 1, `IDR` for core 0) under the worst stress test, with `OVF`, `HS`, `BAD` and `LD` staying at zero and `BL` well below the ring size of 262,144 words.

## Host tests

The decoder, the intake loop, the state model, the frame lock, the timeline, the event log, the presenter and the renderer build and run on the build machine with its own C++ compiler. The tests feed synthetic event streams through the same code the Pi runs and check the resulting pixels.

```sh
gmake test
```

The timed tests use a simulated Spectrum: writes at known T-states, F events with a random Z80 delay, a random transport latency, a 19.2 MHz timer and a 50.00 Hz vertical sync.

The tests cover:

- event fields, the exact empty pair, H mismatches and malformed words,
- OVF and F counting, and the stop at an H mismatch,
- the memory map and port decodes of the 48K, the 128K/+2 and the +2A/+3, special paging and the paging lock,
- a known pixel and attribute pattern, the shadow screen, the border, FLASH and ULAplus,
- the reset event: paging cleared and unlocked, ULAplus off with the palette kept, the border and RAM kept,
- the event log dropping events when full instead of waiting,
- the frame lock's period and model for the 48K and the 128K class, and its position accuracy,
- a multicolour attribute change on every line of a character row,
- a border change inside a line, at the exact block in the renderer and within one block end to end,
- a 0x7FFD screen switch in the middle of a frame,
- pictures that alternate every frame, each shown whole across skips,
- the flywheel: positions stay right for a thousand frames with no F events.

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
- The transport latency and its spread, and so `TransportCalibrationNs`.
- The Z80 delay from interrupt to the first push (`FrameEventTStates`, 10 T-states from the Z80 manual's cycle timing).
- Where the ULA reads a cell relative to where it shows the border. The renderer uses the same T-state for both, at the first-pixel T-state from the World of Spectrum FAQ. The real ULA fetches screen bytes a little before it shows them.
- That the +2A/+3 has the same first-pixel T-state as the 128K. The World of Spectrum FAQ says it shares the 128K timing but gives different contention start points.
- Whether the firmware scales the 640 x 360 frame buffer to 1080p with sharp pixels or with smoothing.
- The real restart gaps, backlog and CPU times. They exist only once the kernel runs on the board. A host measurement scaled to the Pi gives core 1 about 5 ms of work per 20 ms frame at the worst write rate and core 0 about 1.5 ms to draw a frame with 13,000 events, plus the copy to the frame buffer.
