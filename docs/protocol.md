# CPLD to Pi protocol

This is the interface between the CPLD on the TK-Pie board and the Raspberry Pi Zero 2 W. The CPLD watches the Spectrum bus and queues events. The Pi reads the queue through its SMI (Secondary Memory Interface) peripheral with DMA.

## Signals

| Pi GPIO | SMI function | Direction | Use |
|---|---|---|---|
| 0 to 5 | SA5 to SA0 | Pi to CPLD | Reserved. The CPLD ignores them. |
| 6 | SOE (read strobe, active low) | Pi to CPLD | Each low pulse is one SMI read. |
| 7 | SWE (write strobe, active low) | Pi to CPLD | Reserved. The CPLD ignores it. |
| 8 to 23 | SD0 to SD15 | CPLD to Pi | Read data, one 16-bit half-word per read. The CPLD drives these lines all the time. |
| 24, 25 | SD16, SD17 | CPLD to Pi | Driven low. The Pi uses 16-bit SMI. |
| 26 | none | Pi to CPLD | SYNC. High resets the CPLD queue and half-word pointer. |
| 27 | none | CPLD to Pi | NOT_EMPTY. High when the queue holds at least one event. Information only. |

The Pi must never drive GPIO 8 to 25 and must not do SMI writes.

The CPLD pin for each GPIO is in the upstream TK-Pie pin assignments (`CPLD/EPM240/CPLD_TK-Pie.qsf`, signals `GPIO[0]` to `GPIO[23]` and `GPIO_24` to `GPIO_27`).

## Read cycle

- The CPLD changes SD0 to SD15 only on the rising edge of SOE, at the end of a read. The next read sees data that has been stable since the previous read ended.
- An event is two reads. The first read returns half-word 0 and the second returns half-word 1.
- The CPLD copies the oldest event into its output register before half-word 0 is presented, and removes the event from the queue after half-word 1 has been read. Both halves of a pair always belong to the same event.
- When the queue is empty, the pair is an empty event: half-word 0 is 0x0000 and half-word 1 is 0x8000. The Pi can read continuously.
- The read side sees new events two reads late. Empty pairs can therefore arrive while NOT_EMPTY is high, and the first pair after power-up or SYNC is always empty.
- In 16-bit SMI mode with DMA, the Pi receives one pair as one 32-bit word: half-word 0 in bits 15 to 0, half-word 1 in bits 31 to 16.

## Event format

The CPLD sends every Z80 memory write and every I/O write as one event, in the order they happen, with the full address and the data byte. It does no address decode and keeps no paging state. The Pi decides what each write means: which RAM bank it goes to, which port it is, and what it changes.

Half-word 0:

| Bit | 15 | 14 | 13 | 12 | 11, 10 | 9 | 8 | 7 to 0 |
|---|---|---|---|---|---|---|---|---|
| Field | H = 0 | V | OVF | F | KIND | 0 | A15 | DATA |

Half-word 1:

| Bit | 15 | 14 to 0 |
|---|---|---|
| Field | H = 1 | A14 to A0 |

- **H** marks the half. A half-word 0 with H = 1, or a half-word 1 with H = 0, means the Pi and the CPLD disagree about which half comes next. The Pi then pulses SYNC.
- **V** is 1 for an event and 0 for an empty pair.
- **OVF** is 1 on the first event stored after one or more events were dropped because the queue was full. Events between the last event sent and this one are lost.
- **F** is 1 on the first event stored after an interrupt acknowledge. See [Frame events](#frame-events).
- **KIND** is the event kind, below.
- **A15 to A0** is the address of the write: the memory address for a memory write, the full 16-bit port address for an I/O write.
- **DATA** is the byte the Z80 wrote.

| KIND | Event | Address and DATA |
|---|---|---|
| 00 | Memory write | Memory address, data byte |
| 01 | I/O write | Port address (all 16 bits), data byte |
| 10 | Reset: /RESET went high (end of a Spectrum reset) | Address 0, DATA 0 |
| 11 | Reserved, never sent | |

A word with V = 1 and KIND 11, a set bit 9, or a reset event with a non-zero address or DATA is malformed.

## Frame events

The Spectrum interrupts the Z80 once per frame, at a fixed point of the ULA's frame. The Z80 acknowledges with /M1 and /IORQ low together, which no other cycle does. It then pushes its return address to the stack, so the first write after the acknowledge comes a short, bounded time after the interrupt.

The CPLD sets F = 1 on the first event it stores after an acknowledge. When the queue is full and that event is dropped, the next stored event carries F = 1 (and OVF = 1). The acknowledge happens only while the Z80 has interrupts enabled, so a program that runs with interrupts disabled gives no F events.

## Reset event

When /RESET goes high, the CPLD stores a reset event. It comes after every event from before the reset and before every event from after it. When the queue is full, it is dropped like any other event, and the next event has OVF = 1.

## Write capture

The CPLD captures A0 to A15 and D0 to D7 for a write while /WR is low, with values that are valid at the end of the write. The Z80 can put I/O write data on the bus slightly after /WR goes low, so capture at the start of /WR is not used. The edge connector clock is inverted relative to the CPU clock on the +2A/+3, so the capture must not depend on its phase.

## SYNC

While SYNC is high the CPLD empties the queue, clears the overflow flag and sets the half-word pointer to half-word 0. The Pi holds SYNC high for at least 1 µs, with no SMI reads during that time, and waits at least 1 µs after SYNC goes low before the next read.
