# CPLD to Pi protocol

This is the interface between the CPLD on the TK-Pie board and the Raspberry Pi Zero 2 W. The CPLD watches the Spectrum bus and queues events. The Pi reads the queue through its SMI (Secondary Memory Interface) peripheral with DMA.

## Signals

| Pi GPIO | SMI function | Direction | Use |
|---|---|---|---|
| 0 to 3, 5 | SA5 to SA2, SA0 | Pi to CPLD | Reserved. The CPLD ignores them. |
| 4 | none (GPCLK0) | Pi to CPLD | FCLK. A 25 MHz clock for the time count (see [Time base](#time-base)), or a steady level. |
| 6 | SOE (read strobe, active low) | Pi to CPLD | Each low pulse is one SMI read. |
| 7 | SWE (write strobe, active low) | Pi to CPLD | Reserved. The CPLD ignores it. |
| 8 to 25 | SD0 to SD17 | CPLD to Pi | Read data, one 18-bit read word per read. The CPLD drives these lines all the time. |
| 26 | none | Pi to CPLD | SYNC. High resets the CPLD queue and read-word pointer. |
| 27 | none | CPLD to Pi | NOT_EMPTY. High when the queue holds at least one event. Information only. |

The Pi uses 18-bit SMI. It must never drive GPIO 8 to 25 and must not do SMI writes.

FCLK is the general-purpose clock GPCLK0 on GPIO 4 (function ALT0): PLLD (500 MHz) divided by 20 with the integer divider (MASH 0), 25.000 MHz. The CPLD uses it as a clock, so the Pi must either run it at that frequency or leave GPIO 4 at a steady level. A GPIO 4 left as an input with its default pull-up is a steady level.

The CPLD pin for each GPIO is in the upstream TK-Pie pin assignments (`CPLD/EPM240/CPLD_TK-Pie.qsf`, signals `GPIO[0]` to `GPIO[23]` and `GPIO_24` to `GPIO_27`).

## Read cycle

- The CPLD changes SD0 to SD17 only on the rising edge of SOE, at the end of a read. The next read sees data that has been stable since the previous read ended.
- An event is two reads, a pair. The first read returns read word 0 (R0) and the second returns read word 1 (R1).
- The CPLD copies the oldest event into its output register before R0 is presented, and removes the event from the queue after R1 has been read. Both words of a pair always belong to the same event.
- When the queue is empty, the pair is an empty pair. It carries the time count (see [Time base](#time-base)). The Pi can read continuously.
- The read side sees new events two reads late. Empty pairs can therefore arrive while NOT_EMPTY is high, and the first pair after power-up or SYNC is always empty.
- In 18-bit SMI mode with DMA, each read is one 32-bit word in memory, with the read word in bits 17 to 0.

## Event format

The CPLD sends every Z80 memory write and every I/O write as one event, in the order they happen, with the full address, the data byte and its time count. It does no address decode and keeps no paging state. The Pi decides what each write means: which RAM bank it goes to, which port it is, and what it changes.

R0 of an event:

| Bit | 17 | 16 | 15 | 14 | 13 | 12 | 11, 10 | 9 | 8 | 7 to 0 |
|---|---|---|---|---|---|---|---|---|---|---|
| Field | T4 | T3 | H = 0 | V = 1 | OVF | F | KIND | T2 | A15 | DATA |

R1 of an event:

| Bit | 17 | 16 | 15 | 14 to 0 |
|---|---|---|---|---|
| Field | T1 | T0 | H = 1 | A14 to A0 |

- **H** marks the read word. An R0 with H = 1, or an R1 with H = 0, means the Pi and the CPLD disagree about which word comes next. The Pi then pulses SYNC.
- **V** is 1 for an event and 0 for an empty pair.
- **OVF** is 1 on the first event stored after one or more events were dropped because the queue was full. Events between the last event sent and this one are lost.
- **F** is 1 on the first event stored after an interrupt acknowledge. See [Frame events](#frame-events).
- **KIND** is the event kind, below.
- **A15 to A0** is the address of the write: the memory address for a memory write, the full 16-bit port address for an I/O write.
- **DATA** is the byte the Z80 wrote.
- **T4 to T0** is the time count at the end of the write, in Gray code. See [Time base](#time-base).

| KIND | Event | Address and DATA |
|---|---|---|
| 00 | Memory write | Memory address, data byte |
| 01 | I/O write | Port address (all 16 bits), data byte |
| 10 | Reset: /RESET went high (end of a Spectrum reset) | Address 0, DATA 0 |
| 11 | Reserved, never sent | |

An event with KIND 11, or a reset event with a non-zero address or DATA, is malformed.

## Empty pair

R0 of an empty pair:

| Bit | 17, 16 | 15 | 14 | 13 to 6 | 5 | 4 to 0 |
|---|---|---|---|---|---|---|
| Field | 0 | H = 0 | V = 0 | 0 | STL | C4 to C0 |

R1 of an empty pair is 0x08000: H = 1 and every other bit 0.

C4 to C0 is the time count, in Gray code, at the SOE rising edge that loaded R0. An empty pair with any other bit set is malformed.

STL is 1 when the CPLD measured at least one stalled clock phase since it loaded the previous empty pair (see [Time base](#time-base)). A stall that comes at the same time as that load can show on this empty pair, on the next one, or on both. Without FCLK, STL is always 0. On the +2A/+3 the edge-connector clock never stalls, so STL is 0 there too; on the 48K and the 128K it is 1 after contention. SYNC does not clear it.

The first pair after power-up or SYNC is an empty pair that no SOE edge loaded. Its C4 to C0 is 0 and is not a time count. The Pi ignores it.

## Time base

The CPLD keeps a free-running 5-bit time count. Neither SYNC nor /RESET resets it. The count goes to the Pi in the 5-bit reflected binary Gray code (`g = b xor (b >> 1)`). It changes by one step at a time, so a count sampled while it changes is either the old value or the new one, never a mix.

What the count counts depends on FCLK:

- **With FCLK** the count is ULA time, in T-states, on every model. The CPLD counts each rising edge of the edge-connector clock (CPLD pin 62), and it measures the length of every high and every low phase of that clock with FCLK. A phase that is longer than normal by whole T-states is a stall: on the 48K and the 128K, the ULA stops the Z80 clock during contention and the edge-connector clock with it. The CPLD adds one count for each T-state of the stall. A phase of L FCLK periods adds (L - 1) / 7, rounded down, so a normal phase (about 3.5 periods) adds nothing and a phase longer by k T-states (about 3.5 + 7.05 k periods) adds k. A clock edge reaches the count 80 to 120 ns after the edge. The extra counts of a stall come before the stalled phase ends.
- **Without FCLK** the count is the number of rising edges of the edge-connector clock. On the +2A/+3 this is ULA time, because that clock never stops. On the 48K and the 128K it leaves out the T-states of every stall.

The CPLD sees whether FCLK runs. It changes from one count to the other within eight clock periods of FCLK starting or stopping, and the count then jumps once. The Pi starts FCLK before it reads the queue and ignores the counts of the first 10 µs after it started FCLK.

- An event carries the count at the rising edge of /WR that ends the write (T4 to T0).
- An empty pair carries the count when its R0 was loaded (C4 to C0).

The count wraps every 32 counts. The Pi rebuilds the full count:

- **Empty pairs.** The Pi reads an empty pair every few reads while the queue is empty, and the count moves by less than 32 between two of them. The Pi adds the difference, modulo 32, to its running total. Across a stop in the reads, such as the gap between two SMI transfers, it uses its own timer to choose the number of wraps.
- **Events.** An event is captured before the CPLD loads the next empty pair, so its count is at most the count of the next empty pair. The Pi gives the event the latest count at or below that empty pair's count that matches T4 to T0. This is exact while an event waits less than 32 counts between its capture and the next empty pair. An event that waits in the queue through a stop in the reads of 32 counts or more cannot be placed this way.


## Frame events

The Spectrum interrupts the Z80 once per frame, at a fixed point of the ULA's frame. The Z80 accepts the interrupt at the end of the instruction it is running and acknowledges with /M1 and /IORQ low together, which no other cycle does. It then pushes its return address to the stack. The first push is the first write after the acknowledge, and it comes a fixed number of clocks after the start of the acknowledge.

The CPLD sets F = 1 on the first event it stores after an acknowledge. When the queue is full and that event is dropped, the next stored event carries F = 1 (and OVF = 1). The acknowledge happens only while the Z80 has interrupts enabled, so a program that runs with interrupts disabled gives no F events.

The count of an F event without OVF is the count of the acknowledge plus a constant. The difference between the counts of the F events of two frames in a row is the number of counts per frame, plus the change in the time the Z80 took to finish its instruction. ULA time gives 69,888 counts per frame on the 48K and 70,908 on the 128K class, give or take that instruction time. Without FCLK, a clock that stops during contention gives fewer, and the number changes with the program.

## Reset event

When /RESET goes high, the CPLD stores a reset event. It comes after every event from before the reset and before every event from after it. When the queue is full, it is dropped like any other event, and the next event has OVF = 1.

## Write capture

The CPLD captures A0 to A15 and D0 to D7 for a write while /WR is low, with values that are valid at the end of the write. The Z80 can put I/O write data on the bus slightly after /WR goes low, so capture at the start of /WR is not used. The edge connector clock is inverted relative to the CPU clock on the +2A/+3, so the capture must not depend on its phase.

## SYNC

While SYNC is high the CPLD empties the queue, clears the overflow flag and sets the read-word pointer to R0. It does not change the time count. The Pi holds SYNC high for at least 1 µs, with no SMI reads during that time, and waits at least 1 µs after SYNC goes low before the next read.
