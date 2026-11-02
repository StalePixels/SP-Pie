//
// smieventreader.h
//
// Continuous SMI reads from the CPLD into a ring buffer in memory, by DMA.
//
// Circle's CSMIMaster sets the pins and the SMI clock and timing. It has no
// DMA reads, so this class programs the SMI read transfer and one DMA
// channel itself:
//
//  - SMI runs programmed read transfers of SmiBatchTransfers 18-bit reads.
//    Each read fills one 32-bit FIFO word, with the read word in bits 17
//    to 0, so one CPLD pair lands as two ring words.
//  - One DMA control block covers the whole ring and names itself as the
//    next block, so the DMA loops over the ring for ever, paced by the SMI
//    DREQ.
//  - The consumer finds the producer position in the DMA channel's
//    destination address register.
//  - A programmed SMI transfer has a finite count. When it ends (CS DONE),
//    Restart() starts the next one. Reads stop between the two; that gap is
//    measured by the caller.
//
// Pins: GPIO 6 (SOE) and GPIO 8 to 25 (SD0 to SD17) are switched to SMI.
// SMI only ever does read transfers here, so the Pi does not drive SD0 to
// SD17. GPIO 4 carries GPCLK0, the 25 MHz clock the CPLD measures held Z80
// clock phases with (PLLD 500 MHz / 20, integer divider). GPIO 0 to 3, 5
// and 7 are not touched. GPIO 26 is the SYNC output and
// GPIO 27 (NOT_EMPTY) an input.
//
// Initialize () runs on core 0. Everything else runs on the intake core only.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_smieventreader_h
#define _sppie_smieventreader_h

#include "smitiming.h"
#include <circle/smimaster.h>
#include <circle/gpiopin.h>
#include <circle/dmachannel.h>
#include <circle/gpioclock.h>
#include <circle/types.h>

namespace sppie {

// Ring size in 32-bit words: 2 MiB, about 105 ms of reads at 5 M reads/s.
static const unsigned RingWords		= 512 * 1024;

static const unsigned SyncPin		= 26;
static const unsigned NotEmptyPin	= 27;
static const unsigned SoePin		= 6;
static const unsigned FastClockPin	= 4;	// GPCLK0 (ALT0)
static const unsigned FastClockDivider	= 20;	// PLLD 500 MHz / 20 = 25 MHz

class CSmiEventReader
{
public:
	CSmiEventReader (void);
	~CSmiEventReader (void);

	// Sets pins, SMI timing, allocates the ring and a DMA channel. Core 0.
	boolean Initialize (void);

	// Pulses SYNC, starts the DMA loop and the first SMI transfer.
	// Returns the ring index the consumer starts from.
	unsigned Start (void);

	// Ring index the DMA writes next, read from the DMA destination address
	// register. Returns RingWords when the register holds an address
	// outside the ring (the caller then skips this sample).
	unsigned ProducerIndex (void) const;

	// TRUE when the current SMI transfer has finished (and its FIFO data
	// has been taken by the DMA).
	boolean TransferDone (void) const;

	// Starts the next SMI transfer, of nReads reads (even, at most
	// SmiBatchTransfers), after TransferDone ().
	void Restart (unsigned nReads = SmiBatchTransfers);

	// After an H-bit mismatch: stops reads, holds SYNC high for at least
	// 1 us with no reads, clears the SMI FIFO, discards every ring word up
	// to the producer and starts reads again. Returns the ring index the
	// consumer continues from.
	unsigned Resync (void);

	const volatile u32 *Ring (void) const	{ return m_pRing; }

	// Makes the CPU see the DMA's writes to ring words [nFirst, nEnd).
	void InvalidateWords (unsigned nFirst, unsigned nEnd) const;

	// Level of NOT_EMPTY (GPIO 27), information only.
	boolean NotEmpty (void);

private:
	void StopReads (void);
	void PrepareTransfer (unsigned nReads = SmiBatchTransfers);
	void GoTransfer (void);
	void PulseSync (void);

private:
	CSMIMaster	m_SMIMaster;
	CGPIOPin	m_SOEPin;
	CGPIOPin	m_SyncPin;
	CGPIOPin	m_NotEmptyPin;
	CGPIOClock	m_FastClock;
	CGPIOPin	m_FastClockPin;

	unsigned	m_nDMAChannel;
	TDMAControlBlock *m_pControlBlock;
	u8		*m_pRingAlloc;
	u32		*m_pRing;
	u32		m_nRingBusAddress;
};

}

#endif
