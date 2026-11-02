# SP-Pie CPLD logic

VHDL for the Altera EPM240T100C5 (MAX II) CPLD on the TK-Pie board. The CPLD
watches the ZX Spectrum bus, turns screen writes and port writes into events,
and queues them. The Raspberry Pi Zero 2 W reads the queue through its SMI
(Secondary Memory Interface) peripheral. The event format and the read cycle
are defined in [`docs/protocol.md`](../docs/protocol.md). This logic implements
that document.

The CPLD never drives the Spectrum bus. D0 to D7 and /RESET are inputs, and
/ROMCS stays at high impedance.

## Files

| File | Contents |
|---|---|
| `sp_pie_cpld.vhd` | The design. One entity, `sp_pie_cpld`, plain VHDL-93. |
| `sp_pie_cpld.qpf`, `sp_pie_cpld.qsf` | Quartus project for the EPM240T100C5, with all pin assignments. |
| `tb/tb_sp_pie_cpld.vhd` | Self-checking testbench (VHDL-2008). |
| `sim.sh` | Compiles and runs the testbench with nvc. |

The queue depth is the generic `DEPTH` (default 4, a power of two, at least 2).

## What the logic does

For every Z80 write it decides whether the write is an event:

- A memory write to RAM bank 5 or bank 7 with an offset below 0x1B00 inside
  the bank (TYPE 1 and 2). The bank comes from the CPLD's copies of ports
  0x7FFD and 0x1FFD, in normal and +2A/+3 special paging.
- An I/O write that matches the port 0xFE, ULAplus 0xBF3B, ULAplus 0xFF3B,
  0x7FFD or 0x1FFD decode (TYPE 3 to 7). The decodes are the ones in the
  protocol table.

Writes to 0x7FFD and 0x1FFD also update the paging copies. Bit 5 of 0x7FFD
locks both ports until /RESET. Locked writes change nothing and queue no
event. /RESET low clears both copies.

When /RESET goes high, the logic queues a reset event (TYPE 0, DATA 0,
OFFSET 0). It goes into the queue after every event from before the reset
and before every event from after it. See [Reset event](#reset-event).

Each event goes into the queue with its TYPE, OFFSET, DATA and an OVF mark.
When the queue is full, the event is dropped and an overflow flag is set. The
next event that is stored carries OVF = 1 and clears the flag. So OVF = 1
marks the first event after one or more drops. A reset event follows the
same rule: if the queue is full at the end of a reset, the reset event is
dropped, and the first event stored after it carries OVF = 1. The Pi then
gets no reset event for that reset.

### Two events from one write

One I/O write can match the 0xFE decode (A0 = 0) and the 0x1FFD decode
together, for example port 0x1FFC. No other pair of decodes can match the
same address. The protocol asks for two events, TYPE 3 then TYPE 7. The read
side sends such an entry as two pairs, TYPE 3 then TYPE 7, with the same DATA
and OFFSET 0, and removes it after the second pair. One entry per write keeps
the write pointer moving one step at a time, which the Gray-code pointer
crossing needs.

All eight TYPE codes are protocol events, so the double entry has no code of
its own. It is stored as TYPE code 0 with OFFSET bit 0 set:

| Stored TYPE | Stored OFFSET bit 0 | Sent as |
|---|---|---|
| 0 | 0 | TYPE 0 (reset) |
| 0 | 1 | TYPE 3, then TYPE 7 |
| 1 to 7 | any | that TYPE |

Reset and I/O events always send OFFSET 0, so OFFSET bit 0 of a code 0 entry
carries no other information. The read side clears that bit in half-word 1
for both pairs of a double entry. This adds no bit to each queue entry. A
fourth TYPE bit would add DEPTH storage registers and widen the TYPE
multiplexer. The marker costs a one-bit OFFSET multiplexer at the next-entry
index, one register (`dbl`) and the mask on OFFSET bit 0. See
[Logic element estimate](#logic-element-estimate).

## Reset event

The write side is clocked by `wclk = /WR and /RESET`. While /RESET is high,
`wclk` is /WR. While /RESET is low, `wclk` is low, so /WR edges during a reset
have no effect. The Z80 holds /WR high while /RESET is low and after it, so
the end of a reset is a `wclk` rising edge of its own, separate from the next
write.

- `rst_seen` is set asynchronously while /RESET is low and cleared at the next
  `wclk` falling edge, which is the /WR falling edge of the first write after
  the reset.
- At the `wclk` rising edge made by /RESET going high, `rst_seen` is 1, so the
  decode makes a reset event, with DATA and OFFSET forced to 0 whatever the
  bus holds. It goes through the normal queue write, full test and overflow
  flag. `rst_seen` does not change at that edge, so it needs no synchronizer.
- The paging copies are cleared while /RESET is low. The reset event does not
  write them.
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
at the end of the write. The edge-connector clock is not used.

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
  +2A/+3, and may be stretched by contention. The capture uses only /WR,
  /MREQ, /IORQ, the address and the data, so neither matters.

## Clock domains

There are two clock domains. The write side is clocked by `wclk`, which is
/WR while /RESET is high, plus one rising edge at the end of a reset. The read side
is clocked by the rising edge of SOE (Pi GPIO 6). The two edges have no fixed
relation. The queue pointers cross between them in Gray code, so a pointer
sampled while it changes is either the old value or the new one.

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
the overflow flag, the half-word pointer and the output register. It does not
touch the paging copies. A write that ends while SYNC is high is not queued.

## SMI read side

The output register PI_SD changes only on the SOE rising edge. After
half-word 0 is read, it loads half-word 1 of the same entry. After half-word 1
is read, it removes the entry (if the pair was an event) and loads half-word 0
of the next entry, or an empty pair if the queue looks empty. An empty pair
reads as 0x0000 then 0x8000. After power-up and after SYNC, the output
register holds an empty half-word 0, so the first pair is always an empty
pair.

PI_SD drives GPIO 8 to 23 all the time, not only during SOE. SD16 and SD17
(GPIO 24, 25) are driven low. The Pi must not drive these GPIOs.

## Pin map

The pin numbers come from the upstream TK-Pie pin assignments
(`CPLD/EPM240/CPLD_TK-Pie.qsf`). Pins 17 and 21 have no PCB track and are not
used. Unused pins are reserved as tri-stated inputs.

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
| `BUS_RD` | /RD, not used | 99 |
| `BUS_WR` | /WR | 100 |
| `BUS_RESET` | /RESET, input only | 1 |
| `BUS_ROMCS` | /ROMCS, high impedance | 6 |
| `M1` | /M1, not used | 18 |
| `CLK` | Edge-connector clock, not used | 62 |
| `PI_SA(0)` | GPIO 5 (SA0), ignored | 49 |
| `PI_SA(1)` | GPIO 4 (SA1), ignored | 72 |
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
| `PI_SD16` | GPIO 24, SD16, driven low | 58 |
| `PI_SD17` | GPIO 25, SD17, driven low | 56 |
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

- The bus model makes memory and I/O writes with Zilog cycle timing at
  3.5 MHz. I/O writes put the wrong byte on the data bus until 30 ns after
  /WR falls. After /WR rises, the data bus goes to `X` after 60 ns. A design
  that captures too early or too late fails.
- I/O writes can move /IORQ before or after /WR.
- The edge-connector clock can be inverted.
- The Pi reader reads pairs back to back. SOE low and high times vary
  pseudo-randomly, with no fixed relation to the bus clock. It checks the H
  bit of every half-word, checks that empty pairs are exactly 0x0000 and
  0x8000, and records every event.

The tests cover the bank 5 offset limit, bank 5 and bank 7 at 0xC000 through
0x7FFD, the four +3 special layouts, the lock bit, /RESET, every I/O TYPE, a
write that matches both 0xFE and 0x1FFD, the inverted clock, back-to-back
writes while the Pi reads, overflow and OVF, and SYNC (queue emptied,
overflow cleared, paging kept, half-word pointer reset).

The reset event tests check one reset event at power-up and one per reset,
with DATA 0 and OFFSET 0 while the bus carries other values. They check that
the reset event comes after events queued before the reset and before
events after it, that paging is cleared, that a double match after a reset
still arrives as TYPE 3 then TYPE 7 with OFFSET 0, and that a reset on a full
queue is dropped and the next event carries OVF = 1.

Simulation checks function only. It does not check timing or metastability.

## Building with Quartus

Quartus II 13.0sp1 Web Edition runs only on x86-64 Linux or Windows. On an Apple Silicon Mac it runs in an amd64 container under OrbStack.

1. Install OrbStack and check that `docker` works.
2. Download "Quartus II Web Edition 13.0sp1 for Linux" (`Quartus-web-13.0.1.232-linux.tar`) from Altera. The download needs a free Altera account. Extract it, then install the Linux edition into `/u/toolchains/quartus` from a directory that holds only `QuartusSetupWeb-13.0.1.232.run` and `max_web-13.0.1.232.qdz`. Hard links work there; symbolic links make the installer fail with "payload initialization failed". Run the installer in the `quartus13-env` container with `--mode unattended --unattendedmodeui none`.
3. Run `public/cpld/quartus.sh public/cpld`. The `.pof` goes to `public/cpld/output_files/`. The script builds the `quartus13-env` container image the first time.

`QUARTUS_HOME` selects another installation and `QUARTUS_IMAGE` another container image.

To make an `.svf` for openFPGALoader:

```sh
quartus_cpf -c -q 10MHz -g 3.3 -n p output_files/sp_pie_cpld.pof output_files/sp_pie_cpld.svf
```

## Logic elements

From the Quartus 13.0sp1 fitter report. A MAX II logic element (LE) has one 4-input look-up table and one register. The EPM240 has 240.

| Design | Logic elements | Pins |
|---|---|---|
| Upstream TK-Pie CPLD | 37 | 63 |
| This design, DEPTH 4 | 195 | 61 |
| This design, DEPTH 2 | 125 | 61 |

DEPTH 4 is the default. Each step of depth costs about 35 LEs.

The project has no timing constraints (`.sdc`) yet, so the Timing Analyzer uses default clocks and its slack figures mean nothing. Its Fmax figures are 54.6 MHz for the write clock and 84.9 MHz (DEPTH 4) for the SOE clock. Quartus promotes the write clock `wclk` to a global clock. The fitter reports no latches and no combinational loops.

## Not verified

- Timing against real constraints. An `.sdc` with the /WR and SOE clocks and
  the input and output delays is needed for real slack figures.
- The /IORQ flag uses an asynchronous set driven by logic (/IORQ and /WR both
  low). How Quartus implements it on MAX II is not known.
- The write-side clock `wclk` is made by logic (/WR and /RESET). Quartus
  puts it on a global clock; its skew against the bus inputs is not known.
- The rise time of /RESET on each Spectrum model, and whether a slow or
  bouncing edge gives more than one reset event.
- The SOE low and high times the read side needs. They depend on the Quartus
  timing report.
- The Z80A (NMOS) data hold time after /WR rises.
- Behaviour on hardware.
