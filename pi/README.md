# SP-Pie video service for the Raspberry Pi Zero 2 W

This is a bare-metal kernel for the Raspberry Pi Zero 2 W on the TK-Pie board. The CPLD on the board sends every Z80 memory write and I/O write to the Pi over SMI (the Pi's Secondary Memory Interface), each with its exact clock count. The Pi feeds every write, at its T-state, into the ULA display model and the memory paging of the [Fuse](https://fuse-emulator.sourceforge.net/) emulator, and shows the frame Fuse draws. Effects that depend on when the Spectrum writes memory or ports work: multicolour (attributes rewritten for each line), border changes inside a line, screen switching in the middle of a frame, pictures that alternate between frames, and ULAplus palette changes. The same writes drive Fuse's sound generator, so the HDMI output also carries the Spectrum's sound: the AY, a second AY (TurboSound), the beeper and Covox DACs. A program turns these devices, the border extension and the HUD on and off through SP-Pie's control ports ([../docs/control-ports.md](../docs/control-ports.md)). The kernel also measures how much Pi time the work uses.

It is built on [Circle](https://github.com/rsta2/circle) for AArch64 (`RASPPI=3`, `kernel8.img`). The interface to the CPLD is in [../docs/protocol.md](../docs/protocol.md).

None of this has run on hardware yet. See [What is not verified](#what-is-not-verified).

## Architecture

### Cores

The Zero 2 W has four cores. The kernel uses all four.

- **Core 0** starts Circle, the frame buffer, HDMI sound, the SMI reader and Fuse, then runs the frames. Once per HDMI frame it shows the newest complete Spectrum frame, and it runs each Spectrum frame through Fuse as soon as it is complete (see [Presentation](#presentation) and [Latency](#latency)). With each Spectrum frame it makes the frame's sound and writes it to the audio ring (see [Sound](#sound)). It also takes Circle's interrupts (the system timer).
- **Core 1** runs event intake. It polls the DMA position, decodes the new ring words into pairs, rebuilds each event's clock count, and hands counts, read times and events to core 2 through a lock-free queue (`src/ulaqueue.h`). It also restarts or resyncs SMI. It takes no interrupts and never waits for another core.
- **Core 2** turns the counts into the ULA's T-state count where the clock stops during contention, gives each event its frame and T-state, and writes the events to the event log. It also tells core 1 where the next frame starts, for planning restarts.
- **Core 3** sends the sound to HDMI. It reads the audio ring through the resampler and writes each sample to the HDMI audio FIFO as soon as the FIFO has room. With no HDMI sound it halts.

Cores 2 and 0 share the event log (see [Event log](#event-log)), cores 0 and 3 the audio ring, and cores 1, 2 and 0 a block of counters. Core 1 publishes the counters with atomic 64-bit stores, and core 0 reads them once per frame.

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

An SMI transfer has a fixed count of reads. The count is 65,534 (even, so a transfer ends on a pair boundary), which is about 13.1 ms of reads. The width of the SMI length register is not documented, and this count fits in 16 bits. When a transfer ends, core 1 starts the next one. Reads stop for the time between the end of one transfer and the start of the next. The CPLD queue holds four events during the gap, and an event that waits more than 32 clocks (9 µs) cannot be placed from its 5 count bits, so the gap must stay short. From 40 µs before a transfer is due to end, core 1 waits for the end and starts the next transfer at once, and it never handles more than 256 ring words between two looks at the transfer, so a backlog cannot delay the restart. Core 1 measures an upper bound of each gap and shows the largest one on screen.

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

- **Read period.** The read period is taken as 200 ns until it is measured: in each transfer, core 1 notes the DMA position and the timer at its first look and at each later one, and over 32,768 words or more these give the true period. The first measurement replaces the nominal value, later ones move it by an eighth. Read times within a transfer use it, so a read period that is a little off does not move the time of the reads after a restart. `RDPS` shows it, in picoseconds.
- **Empty pairs.** The CPLD loads an empty pair's R0, and takes its count, at the end of the read before it. The running count advances by the difference of two counts modulo 32. The number of whole wraps comes from the time between the two loads and the clock rate (3.5469 MHz): the advance is the value that matches the 5 bits and is nearest to that time. The time of each read comes from its position in the ring (`src/ringclock.h`): SMI reads at a fixed rate, every read lands in the ring, and each transfer start is timed. So the wraps across a transfer restart or a resync are counted as well.
- **Events.** An event is captured before the CPLD loads the next empty pair. Its count is the latest count at or below that pair's count that matches its 5 bits. This is exact while the event waited less than 32 clocks, about 9 µs.
- **Stops.** Before reads stop for a restart or a resync, events that wait for an empty pair are placed from the empty pair before them: the earliest count at or after it, less a two-read delay, that matches their 5 bits.
- **Long waits.** An event that waited 32 clocks or more, which only a stop in the reads that long can cause, may be placed a multiple of 32 clocks off. Such events are counted (`TAMB` on the overlay).

### Model from the clock

The CPLD counts the ULA's T-states on every model (protocol.md, Time base). On the +2A/+3 the edge-connector clock (CKEXT) runs without stopping: 70,908 counts per frame, measured on a +3. On the 48K and the 128K it is the Z80 clock, which the ULA holds during contention: without correction a 128K running Bifrost* measures about 67,300 to 67,800 counts per frame. The CPLD measures each held phase of that clock against GPCLK0, a 25 MHz clock the kernel drives on GPIO 4 (PLLD 500 MHz / 20, integer divider), and adds the T-states it lasted. So the count is ULA time on all three models, and the Pi treats them alike. An empty pair with STL set says the CPLD measured such a held phase since the empty pair before.

`src/ulaclock.cpp` passes the count through and takes from it what it says about the model:

- **Class.** The frame period, measured from the read times of the last 16 F events: 19.968 ms is a 48K (69,888 T-states at 3.5 MHz), 19.9915 ms the 128K class (70,908 at 3.5469 MHz).
- **Clock.** STL since the last reset rules out the +2A/+3, whose clock never stops, until the next reset; so does a frame shorter than its length, which a CPLD without the fast clock gives on a 48K or 128K. `CLK` on the overlay shows 1 then.
- **Clocks per frame.** The count between the F events of two frames in a row (`CPF`, and the smallest and largest of the last 50, `CMIN` and `CMAX`): 70,908 on the 128K class and 69,888 on the 48K, give or take the instruction the Z80 was running.

### Frame clock

`src/frameclock.cpp` turns a count into a frame number and a Fuse T-state.

The CPLD sets F on the first write after each interrupt acknowledge, which is the Z80 pushing its return address. Fuse starts the acknowledge at the first instruction boundary at or after T-state 0 of the frame, adds 7 T-states and then writes the first byte, at T-state 10 (`z80.c`, `z80_interrupt`). So an F event's count less 10 is the frame start plus the rest of the instruction the Z80 was running: 0 to about 22 T-states, and 0 to 3 in HALT.

- The first F event sets the frame start.
- An F event up to 64 T-states before the frame start moves the frame start back to it. One up to 64 T-states after it agrees with it.
- Eight F events in a row that agree with neither set the frame start again, from the last of them.
- Until the first F event, or with interrupts off from the start, frames run from an arbitrary start.

With a count that is ULA time, the frame start never moves in counts. Once one acknowledge has come exactly at an instruction boundary, every position is exact from then on.

**Contention check.** A program locked to the cycle that always takes its interrupt at the same point after the start, for example a HALT that always ends 2 T-states late, never shows the true start, and one early F event moves the start too early. In Fuse's model a contended memory write ends 3 T-states after a T-state with no contention delay; inside the screen fetch of a display line that is one T-state in 8 on the +2A/+3. For each write to 0x4000 to 0x7FFF (contended on every model), the clock counts which of the frame starts 4 T-states before to 3 after the current one would put the write where it cannot end. After 256 such writes it moves the frame start to the one that fits, the nearest when two fit, and from then on F events no longer move it back (`ALN` counts the moves, `LOCK` shows 2). A multicolour engine makes hundreds of these writes a frame.

**Limit.** A program that writes no contended memory during the display keeps the earliest-F frame start, which is late by the acknowledge delay of the earliest F event seen, up to 3 T-states in HALT.

Fuse makes a write take effect at a T-state of its own. A memory write takes effect at its count: /WR rises half a T-state before the end of the write cycle, and Fuse writes at the end of it (`memory_pages.c`, `writebyte`). An I/O write takes effect 3 T-states earlier: /WR rises half a T-state before the end of the 4 T-state I/O cycle, and Fuse writes the port one T-state after its start (`periph.c`, `writeport`). For a contended port write on the 48K and 128K the start of the cycle is not visible, so the write is placed as if uncontended. The constants are in `src/machine.h`.

Positions in the log never go backwards. An event that would land before the last logged position, or in a completed frame, gets that last position instead and is counted (`CLMP`). A frame is complete when the count of an empty pair is past its end by more than the CPLD's capture delay.

### Clocks per frame

The count between the F events of two frames in a row is the number of T-states in a frame, give or take the change in instruction time. The overlay shows the last one and the smallest and largest of the last 50 (`CPF`, `CMIN`, `CMAX`).

### Machine model

| | 48K | 128K, +2, +2A, +3 |
|---|---|---|
| T-states per frame (libspectrum `timings.c`) | 69,888 | 70,908 |
| Fuse machine | `48` | `128`, or `plus3` once port 0x1FFD is written |

The frame lengths come from libspectrum's timing tables at start-up. The class comes from the frame period, measured from the read times of the F events: 19.968 ms is a 48K, 19.9915 ms the 128K class. The count cannot give it, because a clock that stops makes a 128K frame look as short as a 48K's. The 128K class is the start. The frame length cannot tell a 128K or +2 from a +2A or +3: they share the timing but decode the paging ports differently. Only the +2A/+3 has port 0x1FFD. Its ROM writes that port at start-up and each time it changes ROM, and the 128K and +2 ROMs never write it. The +2A/+3's edge-connector clock never stops; the 48K's and the 128K's stops during contention. So a stall reported by the CPLD (STL), or a frame shorter than its length, since the last reset rules out the +2A/+3 until the next reset. With a clock that has not stopped, the kernel uses Fuse's 128K for the 128K class until it sees the first I/O write to exactly port 0x1FFD. A write to 0x1FFD alone is not enough: the Bifrost* demo writes 0x04 there on a 128K. A 128K program that never touches contended memory and writes 0x1FFD looks like a +2A/+3 until its first frame with contention; then it is a 128K again. Until then the picture differs only in the +2A/+3's timings and paging decode. Fuse decodes the port on fewer lines (A15 to A12 = 0001, A1 = 0), but a 128K writes such addresses too: OUT (n),A puts A on A15 to A8, and stray values appear while /RESET bounces. The +2A/+3 software writes the full address: the +3 manual's examples load BC with 1FFDh before OUT (C),A, and BASIC keeps the port's value in BANK678 (Spectrum +3 manual, chapter 8 part 26). From then on the kernel uses Fuse's +3, until a reset or a change of class; the +3 ROM writes 0x1FFD again soon after a reset. `P3W` shows the write that made the choice, as address and data in hex.

A reset button bounces at both the press and the release, and the CPLD sends a reset event for each edge. Reset events less than 70,000 T-states (about 20 ms) apart are one reset. From a reset event until 70,000 T-states after the last one, F events do not touch the frame start or the class, and port writes do not select the +2A/+3. Fuse takes every reset event, which leaves the state of one reset at the end of the burst. The Z80 is held between the press and the release, so it writes nothing there. `RST` counts these resets: a press and a release far apart count as two. The model applies per Spectrum frame, so the frame that holds that first write is already a +3 frame. A 128K program that writes to 0x1FFD selects the +3 by mistake; `auto128` prevents that.

`machine=` in the `[sp-pie]` section of `config.txt` changes the choice (see [Settings in config.txt](#settings-in-configtxt)). Without it, the `machine` option in `cmdline.txt` does, as before:

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

- Fuse: the display model (`display.c`, `rectangle.c`), memory and paging (`memory_pages.c`, `machine.c`, `spectrum.c`, and the 48K, 128K, +2, +2A and +3 machine files), the port decode (`periph.c`, `machines/machines_periph.c`), the ULA port (`peripherals/ula.c`), and the sound generator (`sound.c`) with its Blip_Buffer and filters (`sound/blipbuffer.c`, `sound/speaker_filter.c`, `sound/ula_filter.c`).
- libspectrum: the timing tables (`timings.c`), machine capabilities, memory allocation, the snapshot accessors and its GLib replacement.

Their own builds use autoconf. Here the headers autoconf and their Perl scripts make (`libspectrum.h`, `settings.h`, `options.h` and `snap_accessors.c`) are generated into `fuseula/gen` with their own scripts, and `fuseula/config.h` stands in for autoconf's `config.h`. The Fuse files are compiled into `fuseula/obj`, so the submodules stay clean. Their compiler warnings are turned off; ours are not.

Our layer in `fuseula/` sits between Fuse and the C++ code:

- `fuseula.h` and `fuseula.c`: the C interface. Each write is applied by setting Fuse's T-state counter and calling Fuse's `writebyte_internal` or `writeport_internal`; a reset calls Fuse's `machine_reset`; the end of a frame calls Fuse's `display_frame` with the T-state counter at the start of the next frame, as Fuse's own frame end does. Fuse's FLASH swap inside `display_frame` marks cells through the beam rule, so a counter left at the end of the frame would make it redraw the screen from the memory at the end of the frame. Fuse's drawing calls (`uidisplay_plot8`) land in the picture.
- `fusesound.h` and `fusesound.c`: the sound. See [Sound](#sound).
- `fusesound_rename.h` and the `fusesound_*.c` wrappers: Fuse's `sound.c` compiled once for each sound generator.
- `fusestubs.c`: definitions for the parts of Fuse that are not built (the Z80, tape, disk, debugger, menus and other peripherals), each doing nothing or holding Fuse's value for an absent feature. Fuse's ULA port code reports the ULA's sound output to `sound_ula`, which passes it to `fusesound.c`. ROMs are all zero; the display reads only RAM, and ROM pages are not writable.

Fuse's display covers 320 x 240 pixels: the screen with 32 pixels of border left and right and 24 lines above and below. The layer draws the rest of the 352 x 270 picture's border with Fuse's own beam rule: an 8-pixel chunk that starts at T-state T shows the state of T + 3 (`display.c`, `get_beam_position`). It records each border change Fuse makes with its T-state for this.

Fuse has no ULAplus, so the layer adds it, on every model: the register port 0xBF3B and the data port 0xFF3B, decoded on A15 to A14 = 10 or 11, A7 to A6 = 00, A2 = 0 and A0 = 1 (the TK-Pie board's decode). Data-port writes with register group 0 set palette entries and group 1 sets the mode; groups 2 and 3 are ignored. Mode bit 0 turns ULAplus on. In ULAplus mode, attribute bits 7 to 6 select one of four 16-entry groups, ink is entry group × 16 + INK and paper is group × 16 + 8 + PAPER, FLASH does not swap, and the border is entry 8 + border colour. A palette or mode change applies by the same chunk rule as the border. Fuse only redraws chunks whose pixel byte or attribute changed, so after a palette or mode change the layer has Fuse redraw the rest of that frame and the whole next frame. A reset turns ULAplus off and clears its register; the palette keeps its values.

The ULAplus colour of a GGGRRRBB palette entry: the missing low blue bit is the OR of the two stored blue bits, and the 3-bit levels scale to 0 to 255. Normal mode uses the Spectrum colours, 0xA0 per channel and 0xFF when BRIGHT.

### Sound

`fuseula/fusesound.c` makes the sound from the same events as the picture. The presenter gives it every I/O write and reset at its T-state, the T-state at which Fuse makes the write take effect (see [Frame clock](#frame-clock)), and ends its frame with the display's.

Fuse's sound generator (`sound.c`) keeps its state in globals, so one copy makes one stream. `fusesound_rename.h` gives each compile of the unmodified `sound.c` its own names. These copies run:

| Generator | Makes |
|---|---|
| `fusesound_ay0` | AY chip 0 |
| `fusesound_ay1` | AY chip 1, for TurboSound |
| `fusesound_ula48` | The ULA's sound output on the 48K, through Fuse's model of the 48K's internal speaker |
| `fusesound_ula128` | The ULA's sound output on the 128K class, through Fuse's model of the TV speaker, and the Covox DACs |

Fuse picks these speaker models when its speaker type is Automatic, its default (`sound.c`, `sound_get_speaker_type`). Each ULA generator keeps its speaker type from start-up. Fuse clears the ULA buffer when the speaker type changes, and from then on the beeper drops to 0 for one sample every few frames; with a fixed type for each generator this does not happen.

**Time.** The generators count a clock of 3,546,900 Hz, the 128K's Z80 clock and twice the AY clock. On the 128K class a count is a T-state. On the 48K, `fusesound.c` scales each T-state by 3,546,900 / 3,500,000, and a frame of 69,888 T-states is 70,825 or 70,826 counts, so the sum stays exact. Every generator makes 48 kHz samples covering the real length of each Spectrum frame. So the sound is made in Spectrum time on every model, whatever its frame rate. Fuse's Blip_Buffer turns counts into samples with a 16-bit fraction (887 / 65,536 samples per count), so it makes 48,005 samples per second of Spectrum time, 113 ppm more than 48,000; the resampler takes up the difference.

**Devices.** The decode of the AY, TurboSound, beeper and Covox ports is in [../docs/control-ports.md](../docs/control-ports.md).

- AY writes go to Fuse's `sound_ay_write`, as Fuse's own AY port code sends them. Fuse applies them at its AY step of 32 T-states.
- The beeper comes from Fuse's own ULA port code, so EAR and MIC have Fuse's levels: EAR 50 x 256 and MIC 2 x 256 in Fuse's 16-bit samples.
- Each Covox DAC is a Blip_Synth of its own in the `fusesound_ula128` generator, with Fuse's Covox level of 128 per step, around the centre value 0x80.
- A device that is turned off stops at the T-state of the control write: the beeper and the Covox DACs fall to 0 at that time, and an AY chip leaves the mix from the sample that holds it.
- A reset event writes 0 to every register of both AY chips at its T-state.

**Mix.** The output is mono, as Fuse's default AY stereo separation (None) is. The generators' 16-bit samples are added and multiplied by 128 into signed 24-bit samples, with saturation. The two AY chips and the beeper together stay below full scale. The Covox DACs share the `fusesound_ula128` generator, and Fuse limits its output to 16 bits, so three DACs near full scale clip.

**Audio ring and resampler.** `src/audiostream.h` holds the hand-off to core 3. Core 0 writes each frame's samples, skipped frames included, to a ring of 16,384 samples with one writer and one reader and no lock; a full ring drops the rest of the frame. The samples are in Spectrum time and the HDMI output takes 48,000 per second of the Pi's clock, so the ring would slowly fill or empty. Core 3 reads it through a resampler with a ratio close to 1, with 4-point cubic (Catmull-Rom) interpolation. Every 10 ms the resampler takes the mean fill level over the outputs of those 10 ms, smooths it (over about 0.64 s) and sets the ratio to 1 plus 1,000 ppm for each 1,024 samples above the target of 1,440 samples (30 ms), limited to 1,000 ppm either way. The fill rises by a frame of samples at each write and falls between writes; the mean over every output follows its true average, where a single reading every 10 ms would depend on the phase of the writes. A constant clock difference of d ppm leaves the fill about d samples off the target. A Spectrum frame that comes early or late against the HDMI frames moves 960 samples at once, so the ratio swings by a few hundred ppm around the clock difference and back over the following minute. A ratio of 1,000 ppm changes the pitch by 1.7 cents. When the ring runs empty, the output holds its last value, the underrun is counted (`AUR` on the HUD), and output starts again once the ring holds the target. Control steps at which the ratio is held at its limit are counted too (`LimitHits`). [Latency](#latency) gives the reason for the target.

**HDMI.** Circle's `CHDMISoundBaseDevice` in polling mode, at 48 kHz: core 3 checks that the HDMI audio FIFO has room and writes each sample twice, for the left and the right channel. It needs no interrupt and no DMA channel. `config.txt` sets `hdmi_drive=2`, HDMI mode with sound; in DVI mode Circle's driver does not start, and the kernel then runs without sound.

**Fuse's AY at a frame start.** Fuse's generator starts each frame as if every AY channel were at 0 (`sound.c`, `sound_ay_overlay`, `last_chan1` to `last_chan3`). A channel that falls to 0 at the first AY step of a frame therefore stays at its old level: a tone half period lasts three half periods, and a channel silenced at T-state 0 keeps its level as a constant offset. This is Fuse's output, unchanged.

### Event log

`src/eventlog.h` is the hand-off from core 1 to core 0. It is a ring of timed events (frame number, T-state, kind, address, data) with one writer and one reader, and no lock.

- Core 1 writes events in time order and publishes its write index with a release store. When the ring is full it drops the event and counts it. It never waits.
- When a frame is complete, core 1 writes a record for it (its model and whether the frame start is set) and then stores the count of complete frames with a release store.
- Core 0 loads the complete count with an acquire load, uses the frame's events, and publishes its read index with a release store.

A Z80 writing as fast as it can (PUSH) makes about 640,000 writes a second, about 13,000 a frame. The ring holds 131,072 events, about ten such frames. Core 0 keeps at most three frames waiting.

### Presentation

The display is 480 x 270 pixels at 32 bits per pixel, 16:9. The frame buffer is 960 x 540, also 16:9, which the firmware scales by exactly 2 to the HDMI mode. The firmware keeps the frame buffer's shape, so the buffer must be 16:9: a 480 x 540 buffer is shown 8:9, not stretched. Each display pixel is 2 x 2 frame buffer pixels (`WriteFrameBuffer` in `src/picture.h`). The one exception is the double high mode of interlace mixing, where the second frame buffer line of a display line shows the other screen bank (see [Interlace mixing](#interlace-mixing)). `config.txt` sets 1920 x 1080 at the 128K-class Spectrum frame rate, 50.0211 Hz (the CEA 1080p50 timings with the pixel clock raised to 148.56 MHz, through `hdmi_timings`), so a 128K or +3 frame is skipped only when its crystal differs from the nominal rate; a 48K runs at 50.0801 Hz and still has a skip about every 17 s, with HDMI on even when no display is detected at start-up, and `force_turbo=1` keeps the ARM cores at their full 1000 MHz. The Spectrum picture is 352 x 270 pixels in the middle and fills the height: the 256 x 192 screen with 48 pixels of border left and right and 39 lines above and below. The World of Spectrum 48K reference gives the visible border as 48 pixels at each side, 48 lines above the screen and 56 below. The display shows all of the side border and the 39 lines nearest the screen above and below. The areas left and right of the picture are black. With the border extension on (control-port feature 2, see [../docs/control-ports.md](../docs/control-ports.md)), each line of those areas takes the colour of the line's leftmost or rightmost picture pixel, and the border fills the screen (`src/borderextension.h`). The HUD, when on, is drawn over them.

`src/presenter.cpp` runs on core 0. Each HDMI frame, from the end of a vertical-sync wait:

1. `Present` runs every complete Spectrum frame waiting through Fuse: it selects the frame's model, applies each event at its T-state, and ends the frame. Fuse draws into a picture of the presenter's own, and the presenter keeps a copy of the frame before.
2. `Present` writes the picture to show into the display in RAM: the newest frame run, with the lines that mix blended with the frame before it. It does this on every HDMI frame, also when no new Spectrum frame is complete, so whatever was drawn over the picture (the splash screen) goes.
3. Core 0 draws the splash screen instead when it shows, fills the side areas (border extension, black or HUD), and copies the whole display, each pixel as 2 x 2, into the hidden half of a frame buffer twice the height of the 960 x 540 buffer, 960 x 1080, for the flip. It copies in 10 bands of 27 display rows. In double high, the second frame buffer line of each display line takes its picture area from the presenter's second picture (`SecondPicture`).
4. After each band, and then until 2 ms before the next vertical sync is due, `RunCompleted` runs each Spectrum frame that has become complete, for its sound. It writes no picture, and the frame buffer copy reads only the display and the second picture, which only `Present` writes, so the copy stays one whole frame. The next `Present` shows the newest of these frames.
5. Core 0 moves the display to that half (`SetVirtualOffset`) and waits for vertical sync (`WaitForVerticalSync`).

The vertical sync is due one mean HDMI frame period (measured over the last 50 frames) after the last wait ended. Until 50 frames have been timed, step 4 stops after the copy. A wait that lasts more than half a period means that the sync came before the wait began; the 2 ms guard then grows by 0.5 ms, up to 6 ms.

The Spectrum frame rate is not exactly the HDMI rate: 50.08 Hz on a 48K and 50.02 Hz on the 128K class, against the 50.0211 Hz that `config.txt` sets. Each HDMI frame shows the newest complete Spectrum frame. The frames run since the HDMI frame before, other than the newest, are skipped: on a 48K one about every 17 s, on the 128K class only when its crystal differs from the nominal rate. Every frame goes through Fuse, so FLASH and the picture stay right across skips. When no frame has completed, the last one is shown again. The screen only ever shows whole Spectrum frames.

### Latency

These are the delays inside SP-Pie, from the design. They are not measured, and a TV adds its own. Times are at the 128K-class frame of 20.0 ms (48K: 19.97 ms) and the HDMI frame of 20.0 ms.

**Video.** A Spectrum frame is shown from the first HDMI frame whose `Present` starts after the frame's end. `Present` starts when a vertical-sync wait ends, so the wait for it is between 0 and 20 ms, and it depends on the phase of the Spectrum frames against the HDMI frames. On the 128K class the two rates are the same, so the phase stays the same for a long time. The picture then shows from the next vertical sync, 20 ms later, from the top of the picture to its bottom over 19.2 ms (1,080 of the 1,125 lines of a frame). So the delay from a frame's end to the start of its display is 20 to 40 ms. From a write in the middle of a frame to the middle of the picture it is 40 to 60 ms.

**Audio.** A sample plays a fixed time after the Spectrum time it stands for. That time is the sum of: half a frame on average from a sample to its frame's end (10 ms; all samples of a frame reach the ring together at its end); the wait from the frame's end to its write into the ring (about 1 ms, or, when the frame ends in the window from the guard before a vertical sync to the first band after it, up to the end of that window, 3 to 10 ms); the mean fill of the ring (the 1,440-sample target, 30 ms); and the HDMI audio FIFO. Neither Circle nor the Linux `vc4_hdmi` driver gives the FIFO's depth; the Linux driver sets its thresholds in 6-bit fields, so it is likely to hold at most about 64 words, 0.7 ms at 96,000 words per second. The total is about 41 ms, and up to about 50 ms in the window. A clock difference of d ppm moves the fill d samples off the target, which adds about 6 ms at 300 ppm fast or takes it away at 300 ppm slow.

**Audio against video.** For a write in the middle of a frame and the middle of the picture, the picture comes between about 5 ms before and 18 ms after the sound: about 15 to 18 ms after when the frame ends just after a vertical sync, and about 2 to 5 ms before when it ends in the window before a sync, where the sound waits for the end of the window. The bottom of the picture comes 9.6 ms later than the middle, and the top 9.6 ms earlier.

**Why 1,440 samples.** Core 3 never lets the HDMI FIFO run dry while the ring holds samples: it writes the next sample as soon as the FIFO has room, and the resampler takes about 4 ns per sample on the build machine. So only the ring can run empty. Between two writes of whole frames the fill falls by the samples played, and the resampler holds its mean at the target. With a write at every frame's end, the lowest fill is half a frame (480 samples) below the mean. When a frame ends in the window around a vertical sync, its write comes up to the length of the window later, and with the frame ends near the edge of the window, some frames wait and some do not, so the fill drops by that wait too. The 300 ppm offset adds 307 samples. The host test `audio: fill target against the renderer's writes` simulates this for both frame lengths, clocks 300 ppm fast and slow and exact, and phases across a whole HDMI frame, with these assumptions: a window from 6.5 ms before each sync (the largest guard and 0.5 ms for the wait to return) to 1 to 4 ms after it (`Present`, the side areas and the first band), and up to 1 ms from a frame's end to its write outside the window. Its lowest fill is printed; with 1,440 samples it stays above 0 with no limit hit, and with 1,024 samples the ring runs empty. Writing only once per HDMI frame would need about 1,500 samples or more, as a frame that just misses a vertical sync waits a whole HDMI frame; the test checks that this underruns with 1,440.

### Splash screen

`src/splash.h` and `src/splash.cpp` draw a start-up screen in place of the Spectrum picture, so the display never stays blank. It shows from start-up until an HDMI frame shows a new Spectrum frame while the frame start is locked (`LOCK` on the HUD) and after the Spectrum has written to its screen memory (0x4000 to 0x5AFF), and again after 50 HDMI frames in a row, about 1 s, with no new Spectrum frame: the Spectrum switched off or held in reset, or the board unplugged. Until the first screen write, the Pi's copy of the screen holds nothing the Spectrum drew. When the splash comes back, the screen must be written again before it goes. Short gaps, such as a repeated frame, do not bring it back. Every Spectrum frame still goes through Fuse while it shows, and so does the sound. When it goes, the presenter's next picture covers it.

It is drawn in the Spectrum's colours, on bright white paper: a black title bar with the name and the four-colour stripe of the Spectrum's packaging (bright red, yellow, green and cyan), the same stripe along the foot, and these lines:

- the firmware version, `git describe --tags --always --dirty` of `public/` when the kernel was built (`src/version.h`, made by the Makefile),
- the HDMI mode: its size, which the firmware gives at start-up before the frame buffer is set up (property tag `GET_DISPLAY_DIMENSIONS`, as Circle reads it to choose a frame buffer size), and its frame rate, measured from when the firmware's vertical sync returns, over the last 50 HDMI frames,
- `CPLD link`: found while empty pairs from the CPLD keep coming,
- `Spectrum clock`: found while Spectrum frames complete, which needs the CPLD's count to advance,
- `Machine`: the model of the last frame once the frame start is locked,
- `Frame lock`: locked once an F event has set the frame start,
- how `config.txt` was read: `[sp-pie] read`, `no [sp-pie] section`, `not read`, or in red the first line of the section that was ignored,
- the prompt `Switch on or reset your Spectrum.`

Found items are green and waiting items red. The text uses Circle's 8 x 8 font (`lib/font8x8.cpp`, the Linux `lat1-8` console font, GPL-2.0-or-later), not the Sinclair ROM font. The border extension and the HUD work as usual while the splash shows: the extension then takes the splash's white edges.

### Interlace mixing

`src/interlace.h` and the presenter implement control-port feature 3 ([../docs/control-ports.md](../docs/control-ports.md), Display). Detection uses the screen bank (port 0x7FFD bit 3) of each line, not the pictures. At the end of each frame, `fuseula_end_frame` records, for each of the 270 picture lines, the bank that Fuse drew the line's first screen chunk from (`fuseula_line_screens`). Fuse changes the shown bank after it has drawn every chunk the beam has passed, so a 0x7FFD write at T-state t reaches a chunk that starts at T-state T when t <= T + 3, the same rule as for the border. The lines above and below the screen take the bank at the same place in their line. After each complete Spectrum frame, shown or skipped, `CInterlaceDetector` takes these banks. A line flips when its bank differs from its bank in the frame before. Feature 3 bits 5 and 4 choose the method. Per page (the default): the frame flips when any line flips, and every line mixes after 2 flipping frames in a row and stops after 4 without. Per line: each line starts to mix after 2 of its own flipping frames in a row and stops after 4 without. The presenter mixes only the lines that mix, each with the same line of the frame before; with feature 3 bit 1 it mixes every line. The presenter keeps a copy of the frame before every frame it runs, skipped frames included, so the line that a mix blends with is always from the Spectrum frame just before the one shown. Because of this, the mix of a program that flips screens stays the same across a skip. When a frame repeats, the same two frames are mixed again.

The detector looks at one byte for each line. Blending reads the frame before only for the lines that mix. `gmake test` prints the time of each.

- Average: each channel `(a >> 1) + (b >> 1) + (a & b & 1)`, which rounds down.
- CRT-style: each channel `(5 x newest + 3 x previous) >> 3`.
- Double high: the contract (`../docs/control-ports.md`) is bank 5 and bank 7, each from its own memory, on alternate lines on every frame. `fuseula_screen_line` builds the two lines. On the board the mode does not engage: the output keeps square pixels (`../../docs/open_issues.html`).
- Line alternate: no blending, normal height. On each line that mixes, the screen area of picture line y comes from `fuseula_bank_line`: screen line n = y - 39 of Spectrum frame f shows the first bank (feature 3 bit 6) when n + f is even, and the other bank when it is odd. f is the presenter's number of the frame shown, so the order swaps every Spectrum frame and stays with the Spectrum frame across a skip or a repeat. The line has that bank's own pixel bytes and attributes. A chunk that Fuse drew from that bank is the chunk as drawn. For any other chunk, `fuseula_end_frame` draws both banks from their memory at the end of the frame, before `display_frame`, so `display_parse_attr` gives the FLASH phase of the frame just drawn, and with the ULAplus state at the chunk's beam time. It does this only for a frame that ends with mode 11 set (`fuseula_bank_lines`), because it is about as much work as drawing the screen twice. Multicolour inside the screen area shows only on the lines of the bank shown at the time.

**Pentagon Gigascreen (port #EFF7).** With feature 1 bit 6 set, `CControlPorts` takes a write to 0xEFF7, on all 16 address bits. Data bit 4 = 1 keeps feature 3 and sets bit 1 and mode 11; bit 4 = 0 puts the kept value back; a reset clears it ([../docs/control-ports.md](../docs/control-ports.md), Pentagon Gigascreen). The Pentagon hardware is Dmitry Pugachev's, described in two Russian sources: Dmitry Pugachev (SchemeMan, Dark Souls Group), "Gigascreen для Pentagon с интерлейсом: аппаратная модификация для расширения цветовой палитры", Black Crow #04 (2000), <https://zxpress.ru/ru/ezines/black-crow/04/gigascreen-dlya-pentagon-s-interleysom-apparatnaya-modifikaciya-dlya-rasshireniya-cvetovoy-palitry>, and "Автор аппаратного гигаскрина", real.speccy echo (2002), <https://zxpress.ru/ru/zxnet/real.speccy/avtor-aparatnogo-gigaskrina>. From them come the line-by-line alternation of pages 5 and 7, the order swapped every frame, no colour mixing, and the control by port #EFF7 bit 4. They do not give the polarity of bit 4; bit 4 = 1 for on is an assumption.

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

## Settings in config.txt

The `[sp-pie]` section at the end of `config.txt` holds SP-Pie's start-up settings. The kernel reads it once at start-up, before the other cores start, through Circle's SD card driver and FatFs (`src/bootconfig.cpp`, `CKernel::ReadBootConfig`). The firmware treats a section name in square brackets as a condition and does not use the keys in this one. A firmware setting after the section needs an `[all]` line first.

| Key | Value |
|---|---|
| `feature1`, `feature2`, `feature3`, `feature255` | The default of that control-port feature ([../docs/control-ports.md](../docs/control-ports.md)): its value at start-up and after every Spectrum reset. Decimal, or hex with `0x`. `feature0` takes 0 only. |
| `machine` | `auto`, `auto128`, `48`, `128` or `plus3` (see [Machine model](#machine-model)) |

Section and key names are not case-sensitive. Blank lines and lines that start with `#` are skipped. A value with reserved bits set, a reserved detection method, a reserved feature, an unknown key or an unknown model is ignored, and the splash screen shows the first line ignored. A key given twice takes its last value. A missing section, or a card the kernel cannot read, leaves the built-in defaults.

The `config.txt` in this directory has the section with the built-in defaults. `machine` in `cmdline.txt`, with one line such as `machine=plus3`, still works when `config.txt` gives no `machine`.

The board has no serial console. When start-up fails, the ACT LED blinks a code in a loop: 1 for Circle start-up, 2 for the frame buffer, 3 for the SMI reader or its DMA channel, 4 for starting the second core, 5 for Fuse.

## Instrumentation

With `OVERLAY=1` (the default), the counters are shown as text in the areas left and right of the Spectrum picture: the HUD. The HUD never draws over the picture or its border. A program turns it off and on with control-port feature 0xFF ([../docs/control-ports.md](../docs/control-ports.md)); it is on after start-up and after every reset. Per-frame values are from the HDMI frame before; the others are since boot.

Left side:

| Label | Meaning |
|---|---|
| `EV` | Events in the last frame |
| `EMP` | Empty pairs read in the last frame |
| `FEV` | Events with F set in the last frame |
| `OVF` | Events with OVF set, since boot. Each one means at least one lost event |
| `HS` | H mismatches since boot. Each one caused a SYNC pulse |
| `BAD` | Malformed pairs since boot |
| `RST` | Resets since boot, one per burst of reset events (see [Machine model](#machine-model)) |
| `IN` | Core 1 time spent on ring words, restarts and resyncs, in µs per frame |
| `IDI` | Core 1 time spent polling with nothing new, in µs per frame |
| `RN` | Core 0 time to run the frames through Fuse, make their sound, draw the side areas and copy, in µs. It includes the frames run between the bands of the copy |
| `IDR` | Core 0 time from the end of the copy to the end of the vertical-sync wait, in µs: the frames run for their sound until the guard, the flip request and the wait. Most of it is idle |
| `BL` | Largest ring backlog seen, in words (one word is one read) |
| `RS` | SMI transfer restarts since boot |
| `GAP` | Largest restart gap seen, in µs (an upper bound) |
| `PB` | Times the DMA position read back outside the ring |
| `CLK` | 1 when the Z80 clock was held since the last reset (STL or a short frame): not a +2A/+3 |

`ACQ` should stay still once a program runs; it rises when F events keep disagreeing with the frame start.

Right side:

| Label | Meaning |
|---|---|
| `MACH` | Model of the frame shown: `48K`, `128K` or `+2A/+3` |
| `LOCK` | 1 when the frame start is set from F events, 2 when the contention check has set it |
| `ACQ` | Times the frame start was set from scratch, since boot |
| `ALN` | Times the contention check moved the frame start, since boot |
| `CPF` | Clocks between the F events of the last two frames in a row |
| `CMIN` | Smallest of the last 50 such values |
| `CMAX` | Largest of the last 50 such values |
| `SF` | Number of the Spectrum frame shown |
| `SKIP` | Spectrum frames not shown, since boot |
| `REP` | HDMI frames that showed the last Spectrum frame again, since boot |
| `LD` | Events lost because the event log or core 2's queue was full, since boot |
| `CLMP` | Events moved to keep log positions in order, since boot |
| `TAMB` | Events whose count the 5 bits did not fix, since boot |
| `P3W` | The write that selected the +2A/+3, address and data in hex (0 when none) |
| `RDPS` | Measured SMI read period, in picoseconds |
| `AUR` | Times the audio ring ran empty, since boot (`CAudioResampler::Underruns`). It should stay at 0 once the sound has started |

`CPF`, `CMIN` and `CMAX` should read within a few dozen of 70,908 on the 128K class and 69,888 on the 48K. Well below, changing with the program, means the CPLD is not measuring the held Z80 clock: no fast clock on GPIO 4, or an older CPLD design.

Times come from the ARM generic timer's physical counter (`CNTPCT_EL0`). Every core reads the same counter, it does not change when the firmware changes the CPU clock, and reading it touches no device. It counts at the rate the firmware sets in `CNTFRQ_EL0`. That rate is expected to be 19.2 MHz, but it has not been read on this board.

Reads arrive all the time, so nearly every pass of core 1's loop finds new words, and `IN` includes the fixed cost of each pass (the DMA and SMI register reads, the cache invalidate, the counter stores). A slower pass handles more words at once. Whether core 1 keeps up shows in `BL`, the largest backlog.

The Pi budget is the idle time per frame (`IDI` for core 1, `IDR` for core 0) under the worst stress test, with `OVF`, `HS`, `BAD`, `LD` and `TAMB` staying at zero and `BL` well below the ring size of 524,288 words.

## Host tests

The decoder, the intake, the clock count, the frame clock, the event log, the presenter, Fuse with our layer, the sound, the control ports, the border extension, the splash screen and the resampler build and run on the build machine with its own compilers. The splash tests compile Circle's 8 x 8 font from the Circle source in `../circle-libsdl2`; no Circle build is needed. The Fuse files are generated and compiled into `test/build`. The tests also build one more copy of Fuse's sound generator (`test/fusesound_ref.c`) and drive it directly, as Fuse's own AY port code does, as a reference.

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
- in Fuse: a still picture and the border inside and outside Fuse's area, multicolour per line including a write one T-state too late, multicolour kept through the frames where Fuse swaps FLASH, border changes inside a line, a 0x7FFD screen switch in the middle of a frame and a reset, the +2A/+3 paging decode, and ULAplus palette and mode, with a palette change in the middle of a frame,
- whole frames: skips and repeats in the presenter,
- the contention check: the frame start found from +3 contended writes when every acknowledge is 2 T-states late, and when one F event is 2 T-states early, with writes on both sides of each fetch,
- a reset clearing the +2A/+3 detection, and a bouncing reset: a dozen reset events at the press and thirty at the release with stray writes between them (one to exactly 0x1FFD), then a 128K's OUT to 0x10FD; the 128K stays a 128K, a +3's 0x1FFD write after the burst still makes a +3, and the frame start is not disturbed,
- end to end through the simulated CPLD, the intake, the clock count, the model stage, the frame clock, the log and Fuse: multicolour per line, border changes inside a line, and a restart gap in each frame, with a different acknowledge delay each frame,
- the control ports: only the exact 16-bit ports, each feature's bits and defaults (feature 1 bit 6 off by default), reserved features and bits ignored, the selected feature kept, and a reset restoring every default,
- the border extension: from a frame run through Fuse with border changes inside lines, each line's side areas match its leftmost and rightmost picture pixel, the picture is not touched, and clearing makes the sides black,
- sound: AY writes through the port decode and the mix give exactly Fuse's own generator's samples, times 128, over a sequence of tone, noise and envelope writes,
- the AY tone frequency on the 128K, the 48K and the +3 for periods 256, 252, 126 and 63, against 1,773,450 / (16 x period) and against the 439.8, 879.7 and 1759.3 Hz that MAME 0.289 gives for periods 252, 126 and 63 on a 128K, and the number of samples per frame,
- the AY envelope: the period of shape 0x0C, and shape 0x00 falling once and staying at 0,
- the AY decode (A15, A14, A1), TurboSound chip select with 0xFE and 0xFF, each chip's own register select, chip 1 leaving the mix at the sample of the control write, 0xFE and 0xFF as register selects without TurboSound, writes ignored with the AY off, and a reset clearing both chips,
- each Covox port only with its bit, its 8-bit decode, its levels around 0x80, the DACs adding up and clipping, and a 48K write landing at its time in the 48K's frame,
- the beeper: Fuse's EAR and MIC levels on the TV speaker, the 48K's high-pass speaker model, a square wave at its frequency on the 128K and the 48K with no sample lost across a change of model, silence with the beeper off, and its level back when it is on again,
- control-port writes and a reset through the presenter: a device turned off in the middle of a frame, the HUD and the border extension, the defaults after a reset, and the samples reaching the audio ring,
- interlace mixing through the presenter: screen flips at the start of every frame mix every line from the second flipping frame, with the average of red and green paper, and mixing stops with the 4th frame without a flip; a 0x7FFD write in the middle of the frame that makes only the lines above it, or only the lines below it, flip: exactly those lines mix, by the average and by the CRT-style weight, each pixel checked against the frames as Fuse drew them, and the split line follows Fuse's beam rule (a write 3 T-states after the line's first screen chunk starts reaches it, 4 T-states after does not); a moving block and one screen rewritten with two pictures in turn never mix; across two skipped frames and a repeat every mixed picture blends the frame shown with the Spectrum frame just before it; the CRT-style pixels, mixing always on, mode 11 taken, and the reserved detection methods and bit 7 ignored,
- the frame buffer: every pixel of a normal frame as 2 x 2, the side areas included, with the padding at the end of each line left alone,
- double high, with bank 5 and bank 7 holding different pixel bytes and attributes and the screens flipping every frame, per line and per page and for both values of bit 6: the bank 5 and bank 7 lines in that order in the frame buffer, both in the ink and paper of the bank shown on that frame, and the border parts as Fuse's picture on both lines; with an attribute change for each line of a character row in the middle of every frame, each line's own ink on both of its frame buffer lines, over bank 5's pixels on one and bank 7's on the other; and mode 10 with neither detection nor bit 1, or with detection and no flips, showing the normal picture,
- line alternate, with mixing always on and bank 5 shown, and with detection and the screens flipping every frame, for both values of bit 6 and both frame parities: each screen line in its own bank's pixels and attributes (also the bank not shown), the order by line and frame, the border parts as Fuse's picture, and both frame buffer lines of a display line the same,
- port #EFF7: ignored with feature 1 bit 6 off, and on every lazily decoded address (0x00F7, 0xFFF7 and others one or more address bits away); with bit 6 on, bit 4 = 1 setting mixing always on and line alternate with the other bits kept, a second bit 4 = 1 keeping the first kept value, bit 4 = 0 restoring the value from before, and a reset clearing it; the same through the presenter,
- `config.txt`: no section, every key in any case and form, a section ended by another, every model, invalid lines ignored with the first one kept and cut to fit, the last of a repeated key, and the defaults applied at start-up and restored by a reset through the presenter,
- the splash screen: showing from start-up, not hiding for frames without a lock, hiding with the first locked frame, staying away through gaps shorter than 50 HDMI frames, back at the 50th, and away again when locked frames return; the frame rate from vertical-sync times; the rendered pixels of every line of text, the title bar, both stripes and the colours of found and waiting items, for several sets of findings; and the picture whole again on the next HDMI frame after it, also with no new Spectrum frame,
- the resampler: a straight line through the interpolation, and five minutes of Spectrum frames written once per HDMI frame with the Spectrum's clock 300 ppm fast, 300 ppm slow and exact: no underrun, no limit hit, no drop, the fill within two frames of the target, the ratio averaging to the clock difference and a 440 Hz tone at 440 Hz times the clock ratio; and the ring filling up with a clock difference beyond the limit, with limit hits counted,
- the fill target against the renderer's writes (see [Latency](#latency)): no underrun and no limit hit on the 128K and the 48K, at 300 ppm fast and slow and exact, across a whole HDMI frame of phases; and underruns when the frames are written only once per HDMI frame,
- newest-frame presentation: frames run early for their sound put their samples in the ring at once and leave the picture and the frame shown as they were; the next `Present` shows the newest frame and counts the others as skipped, mixing blends the newest frame with the one just before it, and a frame with nothing new after it repeats,
- end to end on a 48K and a 128K whose Z80 the ULA holds per Fuse's contention table, with the CPLD counting ULA time and setting STL, unseen contended reads, restart gaps with up to 500 ns of start delay, and the Bifrost* demo's write of 0x04 to 0x1FFD: every write at its exact ULA T-state, the multicolour lines right, a steady frame start, frames of 70,908 (69,888) T-states, and the 128K not taken for a +2A/+3; the same with SMI reads 200 ppm slower than assumed. All of it goes through core 2's queue.

At the end the tests print, on the build machine, the time core 0 takes to run a frame of 13,000 screen writes through Fuse, the time core 1's code takes for one frame of words at the worst write rate, the time core 0 takes for the sound of a quiet frame and of a busy one, the time core 0's picture path takes unmixed, averaged and CRT-style, the time of the 960 x 540 frame buffer write into ordinary memory, the time of the interlace detector, and the time the resampler takes for one frame of output.

## Cost

On the +3 with Bifrost* and a 480 x 270 frame buffer, the overlay showed core 0 at about 1.7 to 2.0 ms per frame (`RN`) and core 1 at about 13 ms of 20 (`IN`) with a backlog of about 50 words (`BL`). `IN` counts whole passes of core 1's loop with their fixed costs (see [Instrumentation](#instrumentation)); the backlog shows that core 1 keeps up. On the build machine the tests measure about 0.1 ms for core 0 to run a frame of 13,000 screen writes through Fuse, and about 0.3 ms for core 1's code to decode and place one frame of words (100,000 ring words).

Core 2 takes the frame clock off core 1. On the host core 1's code costs about 6 ns per pair.

On the build machine the sound costs core 0 about 50 µs per quiet frame and about 80 µs per frame with both AY chips playing and thousands of AY, beeper and Covox writes. The resampler takes about 4 µs for one frame of output on core 3. Core 3 has no other work and waits on the HDMI FIFO.

Core 0 also copies Fuse's picture once per Spectrum frame (the frame before, 380 KB, for interlace mixing) and writes the picture to show (380 KB) on every HDMI frame. On the build machine a Spectrum frame with no writes, through Fuse, the sound and this picture path, takes about 61 µs unmixed, 69 µs with every line averaged and 101 µs with every line CRT-style. In double high the presenter also writes the second picture (380 KB). In line alternate the Fuse layer also draws both screen banks from memory at the end of each Spectrum frame, which adds about 29 µs per frame on the build machine; other modes do not do this work. The splash screen and the reading of `config.txt` cost nothing once the Spectrum runs: the splash is not drawn, and the file is read once at start-up.

On every HDMI frame core 0 writes the 960 x 540 frame buffer: 2 MB, as 259,200 aligned 64-bit stores to device memory. The 480 x 270 buffer took a quarter of that, 64,800 stores, and all of `RN` (Fuse, sound, side areas and copy) was 1.7 to 2.0 ms with it, so one store took at most about 30 ns. At that rate the 960 x 540 write takes at most about 8 ms of the 20 ms HDMI frame. This is an upper bound from that measurement, not a measurement of the new buffer; `RN` shows the real figure.

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
- The CPLD's measurement of the held Z80 clock on a real 48K and 128K, and GPCLK0 on GPIO 4 reaching the CPLD.
- That the 48K's edge-connector clock stops during contention like the 128K's (from the Sinclair Wiki; measured on a 128K only).
- Whether the /WR rising edge can meet a counting edge of the edge-connector clock on the +2A/+3, which can make a count one clock off (see `../cpld/README.md`).
- That Fuse's write points (memory at the end of the cycle, I/O one T-state after its start) match the relation of /WR to the ULA on the real machines; Fuse is calibrated against the real machines with its own Z80 timing, which is not run here.
- Whether the firmware scales the 960 x 540 frame buffer to 1080p with sharp pixels or with smoothing.
- The time core 0 takes to write the 960 x 540 frame buffer on the Pi (see [Cost](#cost)).
- That the firmware ignores the keys in `[sp-pie]`. The Raspberry Pi documentation describes `[all]` and the filters it knows; it does not say what an unknown name does. The keys are not firmware settings in any case.
- That Circle's SD card driver reads `config.txt` on the Zero 2 W at start-up with the other devices as they are set up here.
- That `GET_DISPLAY_DIMENSIONS` before the frame buffer is set up gives the size of the HDMI mode from `config.txt` (`hdmi_timings`) on the Zero 2 W. The splash shows what it returns.
- The real restart gaps, backlog and CPU times. They exist only once the kernel runs on the board.
- HDMI sound: that Circle's HDMI sound device starts in polling mode on the Zero 2 W with `hdmi_drive=2`, that core 3 keeps the HDMI audio FIFO filled, and the sound itself.
- The depth of the HDMI audio FIFO (see [Latency](#latency)).
- How long after a vertical sync `WaitForVerticalSync` returns, and how much that varies; the guard and the fill target assume at most 0.5 ms more than the guard covers.
- The time from the end of a vertical-sync wait to the end of the first band of the copy, which the fill target assumes is at most 4 ms.
- Every latency figure in [Latency](#latency): they come from the design, not from a measurement.
- The cost of the sound on core 0 and of the resampler on core 3 on the Pi. Only the host times above are measured.
- How far apart the Spectrum's clock and the Pi's HDMI audio clock really are. The resampler follows up to 1,000 ppm, Blip_Buffer's 113 ppm included.
