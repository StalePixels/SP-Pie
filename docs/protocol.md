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

Half-word 0:

| Bit | 15 | 14 | 13 | 12 to 10 | 9, 8 | 7 to 0 |
|---|---|---|---|---|---|---|
| Field | H = 0 | V | OVF | TYPE | 0 | DATA |

Half-word 1:

| Bit | 15 | 14, 13 | 12 to 0 |
|---|---|---|---|
| Field | H = 1 | 0 | OFFSET |

- **H** marks the half. A half-word 0 with H = 1, or a half-word 1 with H = 0, means the Pi and the CPLD disagree about which half comes next. The Pi then pulses SYNC.
- **V** is 1 for an event and 0 for an empty pair.
- **OVF** is 1 on the first event queued after one or more events were dropped because the queue was full. Events between the last event sent and this one are lost.
- **TYPE** is the event kind, below.
- **DATA** is the byte the Z80 wrote.
- **OFFSET** is the offset inside the screen bank, 0x0000 to 0x1AFF, for TYPE 1 and 2. It is 0 for other types.

| TYPE | Event | Decode |
|---|---|---|
| 0 | Not used | |
| 1 | Write to RAM bank 5, OFFSET below 0x1B00 | Memory write, bank from the paging state |
| 2 | Write to RAM bank 7, OFFSET below 0x1B00 | Memory write, bank from the paging state |
| 3 | Write to port 0xFE | I/O write, A0 = 0 |
| 4 | Write to ULAplus register port 0xBF3B | I/O write, A15..A14 = 10, A7..A6 = 00, A2 = 0, A0 = 1 |
| 5 | Write to ULAplus data port 0xFF3B | I/O write, A15..A14 = 11, A7..A6 = 00, A2 = 0, A0 = 1 |
| 6 | Write to port 0x7FFD, accepted | I/O write, A15..A14 = 01, A1..A0 = 01, paging not locked |
| 7 | Write to port 0x1FFD, accepted | I/O write, A15..A13 = 000, A12 = 1, A1 = 0, paging not locked |

The I/O decodes are the decodes of the upstream TK-Pie CPLD, so software that works with TK-Pie works with this board. The 0x7FFD decode is narrower than the real 128K and +2 decode (A15 = 0, A1 = 0): a write to an address such as 0x3FFD or 0x7FFC pages a real 128K but queues no event.

One I/O write can match both the 0xFE and the 0x1FFD decodes, as on the +2A/+3. The CPLD keeps this as one queue entry and sends it as two events, TYPE 3 and then TYPE 7. When the queue is full, both events are dropped together. No other pair of decodes can match the same write.

## Paging state in the CPLD

The CPLD keeps copies of port 0x7FFD and port 0x1FFD to know which bank a memory write goes to.

- Both are cleared while /RESET is low.
- 0x7FFD bit 5 set locks both ports until reset. Writes to a locked port are ignored and queue no event.
- With 0x1FFD bit 0 = 0 (normal paging): 0x4000 to 0x7FFF is bank 5, and 0xC000 to 0xFFFF is the bank in 0x7FFD bits 2 to 0.
- With 0x1FFD bit 0 = 1 (special paging, +2A/+3), 0x1FFD bits 2 and 1 select the banks for 0x0000, 0x4000, 0x8000 and 0xC000:

| Bits 2, 1 | 0x0000 | 0x4000 | 0x8000 | 0xC000 |
|---|---|---|---|---|
| 0, 0 | 0 | 1 | 2 | 3 |
| 0, 1 | 4 | 5 | 6 | 7 |
| 1, 0 | 4 | 5 | 6 | 3 |
| 1, 1 | 4 | 7 | 6 | 3 |

The CPLD does not know which Spectrum model it is on. On a 48K machine the paging registers stay at 0 unless software writes to an address that matches the 0x7FFD or 0x1FFD decode. The real 128K and +2 decode 0x7FFD on A15 = 0 and A1 = 0 only, so on those machines a write to 0x1FFD also pages 0x7FFD; the CPLD treats it as a 0x1FFD write.

## Write capture

The CPLD captures A0 to A15 and D0 to D7 for a write while /WR is low, with values that are valid at the end of the write. The Z80 can put I/O write data on the bus slightly after /WR goes low, so capture at the start of /WR is not used. The edge connector clock is inverted relative to the CPU clock on the +2A/+3, so the capture must not depend on its phase.

## SYNC

While SYNC is high the CPLD empties the queue, clears the overflow flag and sets the half-word pointer to half-word 0. The paging state is not changed. The Pi holds SYNC high for at least 1 µs, with no SMI reads during that time, and waits at least 1 µs after SYNC goes low before the next read.
