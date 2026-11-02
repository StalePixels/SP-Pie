//
// smieventreader.h
//
// Continuous SMI reads from the CPLD into a ring buffer in memory, by DMA.
//
// Circle's CSMIMaster sets the pins and the SMI clock and timing. It has no
// DMA reads, so this class programs the SMI read transfer and one DMA
// channel itself:
//
//  - SMI runs programmed read transfers of SmiBatchTransfers 16-bit reads.
//    With pixel packing (CS PXLDAT) two reads fill one 32-bit FIFO word, so
//    one CPLD event pair lands as one ring word.
//  - One DMA control block covers the whole ring and names itself as the
//    next block, so the DMA loops over the ring for ever, paced by the SMI
//    DREQ.
//  - The consumer finds the producer position in the DMA channel's
//    destination address register.
//  - A programmed SMI transfer has a finite count. When it ends (CS DONE),
//    Restart() starts the next one. Reads stop between the two; that gap is
//    measured by the caller.
//
// Pins: GPIO 6 (SOE) and GPIO 8 to 23 (SD0 to SD15) are switched to SMI.
// SMI only ever does read transfers here, so the Pi does not drive SD0 to
// SD15. GPIO 0 to 5, 7, 24 and 25 are not touched. GPIO 26 is the SYNC
// output and GPIO 27 (NOT_EMPTY) an input.
//
// Initialize () runs on core 0. Everything else runs on the intake core only.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_smieventreader_h
#define _sppie_smieventreader_h

#include <circle/smimaster.h>
#include <circle/gpiopin.h>
#include <circle/dmachannel.h>
#include <circle/types.h>

namespace sppie {

// SMI timing. The SMI clock is PLLD (500 MHz, as Circle's CSMIMaster
// assumes) divided by SmiCycleNs / 2, so one SMI clock is SmiCycleNs.
//
// One read = (setup + strobe + hold + pace) clocks
//          = (3 + 4 + 3 + 0) x 20 ns = 200 ns, SOE low for 80 ns.
// One event pair = two reads = 400 ns, so 2.5 million pairs per second.
static const unsigned SmiCycleNs	= 20;
static const unsigned SmiSetup		= 3;
static const unsigned SmiStrobe		= 4;
static const unsigned SmiHold		= 3;
static const unsigned SmiPace		= 0;

// Reads per programmed transfer. Must be even so that a transfer always ends
// on a pair boundary. The width of the SMI length register is not
// documented; this value fits in 16 bits. At 200 ns per read one transfer
// lasts about 13.1 ms.
static const unsigned SmiBatchTransfers	= 65534;

// Ring size in 32-bit words: 1 MiB, about 105 ms of reads at 2.5 M pairs/s.
static const unsigned RingWords		= 256 * 1024;

static const unsigned SyncPin		= 26;
static const unsigned NotEmptyPin	= 27;
static const unsigned SoePin		= 6;

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

	// Starts the next SMI transfer after TransferDone ().
	void Restart (void);

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
	void PrepareTransfer (void);
	void GoTransfer (void);
	void PulseSync (void);

private:
	CSMIMaster	m_SMIMaster;
	CGPIOPin	m_SOEPin;
	CGPIOPin	m_SyncPin;
	CGPIOPin	m_NotEmptyPin;

	unsigned	m_nDMAChannel;
	TDMAControlBlock *m_pControlBlock;
	u8		*m_pRingAlloc;
	u32		*m_pRing;
	u32		m_nRingBusAddress;
};

}

#endif
