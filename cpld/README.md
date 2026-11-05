# SP-Pie CPLD logic

VHDL for the Altera EPM240T100C5 (MAX II) CPLD on the TK-Pie board. The CPLD
watches the ZX Spectrum bus, stores every Z80 memory write and I/O write as
an event, and queues the events. The Raspberry Pi Zero 2 W reads the queue through its SMI
(Secondary Memory Interface) peripheral. The event format and the read cycle
are defined in [`docs/protocol.md`](../docs/protocol.md). This logic implements
that document.

The CPLD drives the Spectrum bus only to answer a read of the ULAplus data
port 0xFF3B (see [ULAplus read-back](#ulaplus-read-back)). At all other times
D0 to D7 are high impedance. /RESET is an input, and /ROMCS stays at high
impedance.

## Files

| File | Contents |
|---|---|
| `sp_pie_cpld.vhd` | The design. One entity, `sp_pie_cpld`, plain VHDL-93. |
| `sp_pie_cpld.qpf`, `sp_pie_cpld.qsf` | Quartus project for the EPM240T100C5, with all pin assignments. |
| `tb/tb_sp_pie_cpld.vhd` | Self-checking testbench (VHDL-2008). |
| `sim.sh` | Compiles and runs the testbench with nvc. |

The queue depth is the generic `DEPTH` (default 4, a power of two, at least 2).

## What the logic does

Every Z80 memory write and every I/O write is one event. The event holds the
full address A15 to A0, the data byte, the event kind (KIND 00 for a
memory write, 01 for an I/O write) and the time count T4 to T0 at the end of
the write (see [Time base](#time-base)). The CPLD does no address decode for
events and keeps no paging state. The Pi decides what each write means.

When /RESET goes high, the logic queues a reset event (KIND 10, address 0,
DATA 0). It goes into the queue after every event from before the reset and
before every event from after it. See [Reset event](#reset-event).

Each event goes into the queue with its address, DATA, KIND, time count, an
OVF mark and an F mark. When the queue is full, the event is dropped and an overflow flag
is set. The next event that is stored carries OVF = 1 and clears the flag. So
OVF = 1 marks the first event after one or more drops. A reset event follows
the same rule: if the queue is full at the end of a reset, the reset event is
dropped, and the first event stored after it carries OVF = 1. The Pi then
gets no reset event for that reset.

### Time base

The time count is a free-running 5-bit counter, `t_bin`, and a 5-bit
register, `t_gray`, that holds its value in reflected binary Gray code
(`g = b xor (b >> 1)`). Both load at the same clock edge, so exactly one bit
of `t_gray` changes per step, and it is a register output, so it has no
glitches. Neither SYNC nor /RESET resets the count. It wraps every 32 counts.

The counter's clock is `tclk`. It is FCLK (Pi GPIO 4, pin 72) while the FCLK
detector sees FCLK run, and the edge-connector clock `CLK` (pin 62) when it
does not.

- **Without FCLK**, the counter advances at every rising edge of `CLK`.
- **With FCLK** (25 MHz, 40 ns), the counter counts ULA time. Two registers
  bring `CLK` into the FCLK domain, and a third gives its last level. A
  change between the last two is a `CLK` edge, rising or falling. The
  counter advances by one at each rising edge. A 3-bit counter, `f_sub`,
  counts FCLK periods since the last edge, modulo 7. Each time a phase is
  still running 7, 14, 21 (and so on) periods after the edge that started
  it, the counter advances by one more. A phase of L periods so adds
  k = (L - 1) / 7, rounded down.

The ULA stops the Z80 clock for whole T-states. A T-state is 7.05 FCLK
periods at 3.5469 MHz (128K) and 7.14 at 3.5 MHz (48K), and a normal phase
is half of that. The thresholds sit half way between the phase lengths:

| Measured phase L (FCLK periods) | Extra T-states k | Expected L at 3.5469 MHz | Expected L at 3.5 MHz |
|---|---|---|---|
| 0 to 7 | 0 | 3.5 | 3.6 |
| 8 to 14 | 1 | 10.6 | 10.7 |
| 15 to 21 | 2 | 17.6 | 17.9 |
| 22 to 28 | 3 | 24.7 | 25.0 |
| 29 to 35 | 4 | 31.7 | 32.1 |
| 36 to 42 | 5 | 38.8 | 39.3 |
| 43 to 49 | 6 | 45.8 | 46.4 |

At a 50 % duty cycle, every expected length is at least 2.5 periods from
the nearest other row, for k = 0 to 6 and both clock rates. The
measurement of L can be off by up to one period. A duty cycle of 40/60 moves
a normal phase by 0.7 periods, which leaves about 1.8 periods. The logic
measures both phases, so it works whether the ULA holds the clock high or
low. Stalls longer than six T-states keep adding counts every 7 periods, so
they drift by 0.05 (128K) or 0.14 (48K) periods per T-state.

An extra count needs 7 periods without an edge, so it never comes in the
same FCLK period as a rising edge. The count therefore changes at most once
per FCLK period, one Gray step at a time. A `CLK` rising edge reaches
`t_gray` 80 to 120 ns after it (the third FCLK rising edge at the latest).
The extra counts of a stall come during the stall, before the stalled phase
ends. A write's own contention stall ends more than a T-state before /WR
rises, so its counts are in the count at the /WR rising edge. /WR rises a
short delay after a falling edge of the Z80 clock. Where the
edge-connector clock is the inverse of the Z80 clock, its rising edge is
that falling edge, and it reaches the count at least 80 ns later: the count
at /WR does not include it as long as /WR rises less than 80 ns after the
edge. Where the edge-connector clock is the Z80 clock, its last rising
edge before /WR is half a T-state plus that delay earlier (about 180 ns),
and the count includes it.

**FCLK detector.** A register in the FCLK domain, `f_tog`, toggles at every
FCLK edge. Two registers sample it at each `CLK` rising edge and a third
holds the last sample. A `CLK` period is 7 or 8 FCLK periods (never 8 twice
in a row) or more during a stall, so `f_tog` changes between two samples
almost every time while FCLK runs. `use_f` goes to 1 at the first change and
back to 0 after eight `CLK` rising edges without one. It changes only when
FCLK starts or stops.

**Clock switch.** `tclk` is a multiplexer of FCLK and `CLK` selected by
`use_f`. Quartus puts it on a global clock. When `use_f` changes, `tclk` can
have one short or missing pulse, and the counter's registers can take any
value. Every value is a legal state: the next `tclk` edge loads `t_gray`
from `t_bin + 1`, and `f_sub` moves on from any value. The count is
consistent again one edge later. The Pi sees one jump in the count when it
starts or stops FCLK. A second counter clocked by `CLK`, with a selector at
the output, would avoid the short pulse, but it needs more logic elements
than the device has left.

**Sampling.** The `wclk` rising edge that stores an event also stores
`t_gray` as the event's T4 to T0. For a write this is the /WR rising edge.
For a reset event it is the edge made by the filtered /RESET going high,
which comes just after a `CLK` edge. The SOE rising edge that loads the R0 of
an empty pair samples `t_gray` into C4 to C0 of that R0. The R1 that follows
is a constant and does not sample it again. Both sides sample the count at
edges that have no fixed relation to `tclk`. When a sample is taken as the
count changes, Gray code makes it the old count or the new one, never a mix.
The Pi reads an empty R0 one full read after it was loaded, so the sample
has that read to settle.

**STL.** Each extra count also makes `stl_p` high for one FCLK period, and
only while `use_f` is 1. `stl_p` sets `stl_r` asynchronously. The SOE rising
edge that loads an empty R0 copies `stl_r` into bit 5 of that R0 (STL) and
clears it. So STL = 1 says that the counter added at least one stall
T-state since the previous empty R0 was loaded. When `stl_p` is high at the
loading edge the set wins, and the stall shows on this empty pair, the next
one, or both. SYNC does not clear `stl_r`.

### Frame flag

The Z80 acknowledges an interrupt with /M1 and /IORQ low together. No other
cycle has that combination. A flag is set asynchronously while /M1 and /IORQ
are both low, and cleared at the `wclk` rising edge that stores an event. The
stored event takes the flag as its F mark, so the first event stored after an
acknowledge has F = 1. An event dropped on a full queue does not clear the
flag, so the next stored event carries F = 1 (and OVF = 1). Two acknowledges
with no stored event between them give one F. A reset event is an event, so
it carries F = 1 when it is the first event after an acknowledge.

### ULAplus read-back

The ULAplus data port 0xFF3B is read/write, and software reads it to detect
ULAplus. This is the only read the CPLD answers. The CPLD keeps the group of
the last write to the register port 0xBF3B (mode group or not), the mode
register, and the last byte written to the data port 0xFF3B. The port
decodes are:

| Port | A15, A14 | A7, A6 | A2 | A0 |
|---|---|---|---|---|
| 0xBF3B (register) | 1, 0 | 0, 0 | 0 | 1 |
| 0xFF3B (data) | 1, 1 | 0, 0 | 0 | 1 |

An I/O read with the 0xFF3B decode (/IORQ and /RD low) drives D0 to D7 with
the mode register when the mode group is selected, and with the last byte
written otherwise. The CPLD keeps no copy of the palette, so a read in the
palette group does not return the palette entry. /RESET low clears all three
registers. Writes to both ports are also ordinary I/O events.

## Reset event

/RESET goes through a filter first. The edge-connector /RESET line can carry
pulses of a few nanoseconds that are not resets, and a real reset lasts
milliseconds. The CPLD samples /RESET on each rising edge of the
edge-connector clock (pin 62), and the filtered signal `rst_n` changes only
when the last four samples agree, about 1.1 µs at 3.5 MHz. `rst_n` is a
register output, so it has clean edges. The filter does not depend on the
clock's phase.

The write side is clocked by `wclk = /WR and rst_n`. While `rst_n` is high,
`wclk` is /WR. While `rst_n` is low, `wclk` is low, so /WR edges during a
reset have no effect. The Z80 holds /WR high while /RESET is low and for some
microseconds after it, so the end of a reset is a `wclk` rising edge of its
own, separate from the next write.

- `rst_seen` is set asynchronously while `rst_n` is low and cleared at the next
  `wclk` falling edge, which is the /WR falling edge of the first write after
  the reset.
- At the `wclk` rising edge made by /RESET going high, `rst_seen` is 1, so the
  logic stores a reset event, with the address and DATA forced to 0 whatever
  the bus holds. It goes through the normal queue write, full test and
  overflow flag. `rst_seen` does not change at that edge, so it needs no
  synchronizer.
- The read pointer the write side uses for the full test is the one sampled
  at the last `wclk` falling edge: when /RESET went low, or the last /WR
  falling edge before that. It can be stale, which only makes the queue look
  fuller. The reset event can then be dropped as on a full queue.
- A write that ends while SYNC is high is not queued. The same holds for a
  reset that ends while SYNC is high.
- If /RESET goes high while the CPLD is not yet configured, there is no reset
  event for that reset.
- Each `wclk` rising edge while `rst_seen` is 1 queues one reset event. A
  /RESET edge that bounces gives more than one reset event. The Pi treats
  them all the same, so extra ones change nothing on screen.

## Write capture

The event is stored on the rising edge of `wclk` (/WR while /RESET is high),
at the end of the write. The capture of the address, data and kind does not
use the edge-connector clock.

- At the /WR rising edge, D0 to D7 are valid for memory writes and for I/O
  writes. Memory write data is valid before /WR falls. I/O write data can
  come slightly after /WR falls (Zilog Z84C00 data sheet, -10 ns at 4 MHz), so
  capture at the falling edge is not safe.
- The Z80 holds address and data after /WR rises. The Zilog manual (UM0080)
  says /WR goes high half a T-state before the bus changes. The Z84C00 data
  sheet gives 60 ns minimum data hold at 4 MHz. The hold time of the NMOS
  Z80A in the Spectrum is not known.
- /MREQ and /IORQ rise at the same clock edge as /WR, so the logic does not
  read them at the /WR rising edge. It remembers them while /WR is low:
  - /MREQ is sampled at the /WR falling edge. On a memory write /MREQ has been
    low for one T-state by then.
  - On an I/O write, /IORQ and /WR fall at the same clock edge in no fixed
    order. A flag is cleared at the /WR falling edge and set asynchronously
    while /IORQ and /WR are both low. The flag is still set at the /WR rising
    edge.
- The edge-connector clock is inverted relative to the CPU clock on the
  +2A/+3, and is stretched by contention on the 48K and the 128K. The
  capture of the address, data and kind uses only /WR, /MREQ, /IORQ, the
  address and the data, so neither matters for those fields. The time count
  does depend on the clock (see [Time base](#time-base)).

## Clock domains

The clock domains are:

- The write side is clocked by `wclk`, which is /WR while /RESET is high,
  plus one rising edge at the end of a reset.
- The read side is clocked by the rising edge of SOE (Pi GPIO 6).
- The edge-connector clock `CLK` clocks the /RESET filter and the FCLK
  detector.
- `tclk` (FCLK or `CLK`) clocks the time counter. One register, `f_tog`, is
  clocked by FCLK alone.

The edges of these clocks have no fixed relation. The queue pointers
cross between the write and read sides in Gray code, and the time count goes
to both sides in Gray code. A value sampled while it changes is either the
old value or the new one.

- Read pointer into the write side: one register samples it at the `wclk`
  falling edge. The write side first uses it at the next `wclk` rising edge,
  the end of the same write, so the register has at least one T-state (about
  280 ns at 3.5 MHz) to settle from metastability. A `wclk`-clocked second
  register would only update on the next write, so it is not used.
- Write pointer into the read side: two registers in series, clocked by SOE.
- A stale pointer is always safe. A stale read pointer makes the queue look
  fuller than it is, so a write can never overwrite an entry the Pi has not
  read. A stale write pointer hides new entries for a few reads, and the Pi
  gets empty pairs meanwhile. An entry's storage is written at the same `wclk`
  edge that advances the write pointer, and the read side reads it at least
  two SOE edges later.

The Pi must keep reading for new events to show. The read side only sees the
write pointer move on SOE edges, so after an idle period the first reads can
return empty pairs while events are already queued.

NOT_EMPTY (GPIO 27) compares the two pointers directly, across the domains.
It can glitch for a moment when a pointer changes. The protocol marks it as
information only.

SYNC (GPIO 26) is an asynchronous reset of both pointers, the pointer copies,
the overflow flag, the read-word pointer and the output register. It does not
touch the time counter, STL, the frame flag or the ULAplus registers. A write that ends while SYNC is
high is not queued.

## SMI read side

The read word is 18 bits wide, SD0 to SD17 (GPIO 8 to 25). The output
register changes only on the SOE rising edge, all 18 bits together.

| Read word | Bits 17, 16 | Bit 15 | Bits 14 to 0 |
|---|---|---|---|
| Event R0 | T4, T3 | H = 0 | V = 1, OVF, F, KIND, T2 (bit 9), A15, DATA |
| Event R1 | T1, T0 | H = 1 | A14 to A0 |
| Empty R0 | 0 | H = 0 | 0, except STL in bit 5 and C4 to C0 in bits 4 to 0 |
| Empty R1 | 0 | H = 1 | 0 |

After R0 is read, the register loads R1 of the same entry. After R1 is read,
it removes the entry (if the pair was an event) and loads R0 of the next
entry, or an empty R0 if the queue looks empty. An empty R1 is always
0x08000. SYNC and power-up clear the output register to an empty R0 with
STL = 0 and C4 to C0 = 0, which no SOE edge has loaded, so the first pair after power-up
or SYNC does not carry the time count.

SD0 to SD17 drive GPIO 8 to 25 all the time, not only during SOE. The Pi must
not drive these GPIOs.

## Pin map

The pin numbers come from the upstream TK-Pie pin assignments
(`CPLD/EPM240/CPLD_TK-Pie.qsf`). Pins 17 and 21 have no PCB track and are not
used. Unused pins are reserved as tri-stated inputs. Every input the logic
uses has the 3.3V Schmitt-trigger input standard. The write side is clocked by
/WR and /RESET, the read side by SOE and the time count by CLK and FCLK, so a
slow or noisy edge on those pins must give one transition only. FCLK is a
fast clock from the Pi, but the logic samples `CLK` with it and has no
timing relation to keep with other inputs, so the small extra delay of the
Schmitt-trigger input does not matter. The Pi inputs the logic ignores (SA0,
SA2 to SA5, SWE) keep the default standard.

| Port | Signal | CPLD pin |
|---|---|---|
| `BUS_A(0)` | A0 | 87 |
| `BUS_A(1)` | A1 | 89 |
| `BUS_A(2)` | A2 | 91 |
| `BUS_A(3)` | A3 | 95 |
| `BUS_A(4)` | A4 | 5 |
| `BUS_A(5)` | A5 | 4 |
| `BUS_A(6)` | A6 | 3 |
| `BUS_A(7)` | A7 | 2 |
| `BUS_A(8)` | A8 | 7 |
| `BUS_A(9)` | A9 | 8 |
| `BUS_A(10)` | A10 | 15 |
| `BUS_A(11)` | A11 | 16 |
| `BUS_A(12)` | A12 | 81 |
| `BUS_A(13)` | A13 | 82 |
| `BUS_A(14)` | A14 | 77 |
| `BUS_A(15)` | A15 | 78 |
| `BUS_D(0)` | D0 | 84 |
| `BUS_D(1)` | D1 | 85 |
| `BUS_D(2)` | D2 | 86 |
| `BUS_D(3)` | D3 | 92 |
| `BUS_D(4)` | D4 | 96 |
| `BUS_D(5)` | D5 | 90 |
| `BUS_D(6)` | D6 | 88 |
| `BUS_D(7)` | D7 | 83 |
| `BUS_MRQ` | /MREQ | 97 |
| `BUS_IORQ_i` | /IORQ (weak pull-up) | 98 |
| `BUS_IORQ_COPY_i` | /IORQ, second pin (weak pull-up) | 76 |
| `BUS_RD` | /RD, for ULAplus reads | 99 |
| `BUS_WR` | /WR | 100 |
| `BUS_RESET` | /RESET, input only | 1 |
| `BUS_ROMCS` | /ROMCS, high impedance | 6 |
| `M1` | /M1, for the interrupt acknowledge | 18 |
| `CLK` | Edge-connector clock, for the time count and the /RESET filter | 62 |
| `PI_SA0` | GPIO 5 (SA0), ignored | 49 |
| `PI_FCLK` | GPIO 4 (GPCLK0), FCLK | 72 |
| `PI_SA(2)` | GPIO 3 (SA2), ignored | 74 |
| `PI_SA(3)` | GPIO 2 (SA3), ignored | 75 |
| `PI_SA(4)` | GPIO 1 (SA4), ignored | 51 |
| `PI_SA(5)` | GPIO 0 (SA5), ignored | 50 |
| `PI_SOE` | GPIO 6, SMI read strobe SOE | 47 |
| `PI_SWE` | GPIO 7, SMI write strobe SWE, ignored | 52 |
| `PI_SD(0)` | GPIO 8 (SD0) | 54 |
| `PI_SD(1)` | GPIO 9 (SD1) | 55 |
| `PI_SD(2)` | GPIO 10 (SD2) | 57 |
| `PI_SD(3)` | GPIO 11 (SD3) | 53 |
| `PI_SD(4)` | GPIO 12 (SD4) | 48 |
| `PI_SD(5)` | GPIO 13 (SD5) | 44 |
| `PI_SD(6)` | GPIO 14 (SD6) | 73 |
| `PI_SD(7)` | GPIO 15 (SD7) | 71 |
| `PI_SD(8)` | GPIO 16 (SD8) | 43 |
| `PI_SD(9)` | GPIO 17 (SD9) | 69 |
| `PI_SD(10)` | GPIO 18 (SD10) | 70 |
| `PI_SD(11)` | GPIO 19 (SD11) | 42 |
| `PI_SD(12)` | GPIO 20 (SD12) | 41 |
| `PI_SD(13)` | GPIO 21 (SD13) | 39 |
| `PI_SD(14)` | GPIO 22 (SD14) | 66 |
| `PI_SD(15)` | GPIO 23 (SD15) | 67 |
| `PI_SD16` | GPIO 24 (SD16) | 58 |
| `PI_SD17` | GPIO 25 (SD17) | 56 |
| `PI_SYNC` | GPIO 26, SYNC | 40 |
| `PI_NOT_EMPTY` | GPIO 27, NOT_EMPTY | 68 |

## Simulation

The testbench uses [nvc](https://www.nickg.me.uk/nvc/). On macOS:

```
brew install nvc
./sim.sh          # DEPTH 2, 4 and 8
./sim.sh 4        # one depth
```

Each run prints `TB PASS` or `TB FAIL`, and `sim.sh` exits nonzero if any run
fails. Build files go in `build/`.

The testbench has a Z80 bus model and a Pi reader model:

- The bus model makes memory writes, I/O writes, I/O reads and interrupt
  acknowledges with Zilog cycle timing at 3.5 MHz. I/O writes put the wrong
  byte on the data bus until 30 ns after /WR falls. After /WR rises, the data
  bus goes to `X` after 60 ns. A design that captures too early or too late
  fails.
- I/O writes can move /IORQ before or after /WR.
- The edge-connector clock is the Z80 clock, and it can be inverted. /WR
  rises 40 ns after the falling edge of T3 of the Z80 clock, for memory
  writes and for I/O writes.
- The clock model can stall: about one phase in four, at one chosen level
  of the edge-connector clock, is longer by 1 to 6 whole T-states
  (pseudo-random, never two phases in a row). The bus model waits on the
  clock, so bus cycles stretch with it.
- FCLK runs at 25 MHz, with each edge moved by up to 2 ns, and has no fixed
  relation to the Z80 clock. It can be started and stopped.
- The testbench keeps its own counts: the rising edges of the
  edge-connector clock, and ULA time (those edges plus each stall T-state).
  Without FCLK the design must give the edge count. With FCLK it must give
  ULA time, 72 to 128 ns late. A count sample is checked against the window
  of counts that its time allows; the window has two values only when a
  count step comes in it.
- The Pi reader reads pairs back to back. SOE low and high times vary
  pseudo-randomly, with no fixed relation to the bus clock or FCLK. It
  checks the H bit of every read word, checks that an empty R0 has only STL
  and C4 to C0 set and that an empty R1 is exactly 0x08000, rejects
  malformed events, and records every event.
- A monitor checks that the design's count changes by one Gray step at a
  time, and with FCLK at most once per FCLK period.

The suite of tests below runs five times: without FCLK; with FCLK and
stalls in the high phase; with FCLK and stalls in the low phase; with FCLK
and no stalls; and with FCLK stopped again, with stalls. Between the runs the
testbench starts or stops FCLK with the reader off, checks that the design
switched its count, and lines its own count up with the design's.

The tests cover:

- Memory writes anywhere in 0x0000 to 0xFFFF and I/O writes to any port, with
  the exact address and DATA, including a walking one and a walking zero on
  every address and data line.
- I/O reads and interrupt acknowledges, which are not events.
- The reset event: one at power-up and one per reset, with address 0 and
  DATA 0 while the bus carries other values; its place in the queue between
  earlier and later events; and a reset on a full queue, which is dropped
  and gives OVF = 1 on the next event.
- The F mark: on the first event after an acknowledge only, one F for two
  acknowledges, F on a reset event, and F kept across dropped events.
- Back-to-back writes three T-states apart (stack pushes) while the Pi reads.
- Overflow and OVF, and SYNC (queue emptied, overflow cleared, read-word
  pointer reset).
- ULAplus read-back: the mode register in the mode group, the last byte
  otherwise, no drive on other ports or between reads, and all three
  registers cleared by /RESET.
- The time count:
  - Every event's T4 to T0 is the Gray code of the testbench count at its
    /WR rising edge, or at the end of the reset for a reset event. This also
    checks the bit positions: T4, T3 in SD17, SD16 and T2 in bit 9 of R0,
    and T1, T0 in SD17, SD16 of R1. With FCLK, every event's window has one
    value. Without FCLK, a reset event's window has two, because the edge
    that ends a reset comes just after a clock edge.
  - Every empty pair's C4 to C0 is the Gray code of a count in the window
    for the SOE rising edge that loaded its R0. Between two empty pairs in
    one run of reads, the step of C modulo 32 is a step the two windows
    allow. The first pair of each run of reads is not checked, because the
    reader did not see the edge that loaded it.
  - Every stall length from 1 to 6 T-states occurs, in both phases.
  - For each event written while the reader runs, the next empty pair has a
    count at or above the event's and less than 32 above it. The count the
    Pi rebuilds from T4 to T0 and that empty pair (the latest count at or
    below the empty pair's count that matches T4 to T0) is the true count of
    the write.
  - The count is not reset by SYNC or by /RESET: empty pairs after two SYNCs
    and after two resets a few clocks apart still carry the testbench count.
- STL, on every empty pair whose previous empty pair the reader also saw:
  without FCLK it is 0. With FCLK it is 1 when the design's stall pulse for
  a testbench stall T-state must lie between the two loads, and it is 0
  when no stall pulse can.

Each time check and each STL check was shown to fail on a copy of the
design with that feature broken: wrong threshold spacing, thresholds one
period early, stall T-states not added, a binary count register, two count
steps at once, one phase not measured (each phase in turn), no fallback to
`CLK`, a detector that does not see FCLK stop, STL stuck at 1 or 0, STL not
gated without FCLK, STL cleared at every R0 load, and STL never cleared.

Simulation checks function only. It does not check timing or metastability.

## Building with Quartus

Quartus II 13.0sp1 Web Edition runs only on x86-64 Linux or Windows. On an Apple Silicon Mac it runs in an amd64 container under OrbStack.

1. Install OrbStack and check that `docker` works.
2. Download "Quartus II Web Edition 13.0sp1 for Linux" (`Quartus-web-13.0.1.232-linux.tar`) from Altera. The download needs a free Altera account. Extract it, then install the Linux edition from a directory that holds only `QuartusSetupWeb-13.0.1.232.run` and `max_web-13.0.1.232.qdz`. Hard links work there; symbolic links make the installer fail with "payload initialization failed". Run the installer in the `quartus13-env` container with `--mode unattended --unattendedmodeui none`.
3. Set `QUARTUS_HOME` to the installation directory, then run `cpld/quartus.sh cpld` from the repository root. The `.pof` goes to `cpld/output_files/`. The script builds the `quartus13-env` container image the first time.

`QUARTUS_IMAGE` selects another container image.

## Programming

Programming uses openFPGALoader (Homebrew) and an Altera USB-Blaster on the JTAG header CN3, which has the standard Altera 10-pin layout (1 TCK, 2 GND, 3 TDO, 4 VCC, 5 TMS, 9 TDI, 10 GND). The board must have power: the blaster only senses VCC on pin 4. A Pi on the board, powered from its own USB, gives the board power.

`quartus_cpf` makes a programming `.svf` (`-n p`) and a verify `.svf` (`-n v`) from the `.pof`. Run it inside the `quartus13-env` container, as `quartus.sh` runs Quartus:

```sh
quartus_cpf -c -q 10MHz -g 3.3 -n p output_files/sp_pie_cpld.pof output_files/sp_pie_cpld.svf
quartus_cpf -c -q 10MHz -g 3.3 -n v output_files/sp_pie_cpld.pof output_files/sp_pie_cpld_verify.svf
```

Then, on the Mac:

```sh
openFPGALoader --detect -c usb-blaster          # finds EPM240T100C5N, IDCODE 0x20A10DD
openFPGALoader -c usb-blaster output_files/sp_pie_cpld.svf
openFPGALoader -c usb-blaster output_files/sp_pie_cpld_verify.svf
```

openFPGALoader compares the TDO values in the verify `.svf`, so a device that holds a different design fails with "TDO value ... isn't the one expected". "TDO is stuck at 0" in the scan means the blaster gets no VCC on pin 4, no ground, or no answer from the board.

The upstream TK-Pie design can be put back the same way from `CPLD/EPM240.pof` in the TK-Pie repository (<https://gitlab.com/victor.trucco/TK-Pie>).

## Logic elements

From the Quartus 13.0sp1 fitter report. A MAX II logic element (LE) has one 4-input look-up table and one register. The EPM240 has 240.

| Design | Logic elements | Pins |
|---|---|---|
| Upstream TK-Pie CPLD | 37 | 63 |
| This design, DEPTH 4 | 225 | 61 |
| This design, DEPTH 2 | 169 | 61 |

DEPTH 4 is the default.

The project has no timing constraints (`.sdc`) yet, so the Timing Analyzer uses default clocks and its slack figures mean nothing. Its Fmax figures (DEPTH 4) are 39.4 MHz for the write clock, 57.7 MHz for the SOE clock, and 112.5 MHz for FCLK and for the edge-connector clock (86.5 MHz restricted by a hold check on the edge-connector clock), where the time counter's paths through `tclk` are counted under both. FCLK needs 25 MHz. Quartus puts `CLK`, SOE, `wclk` and `tclk` on the four global clocks, and reports `tclk` as a clock multiplexer. The fitter reports no latches and no combinational loops.
