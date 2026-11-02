//
// smieventreader.cpp
//
// SMI register layout: from the Linux driver header
// include/linux/broadcom/bcm2835_smi.h. Broadcom does not document SMI.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "smieventreader.h"
#include "timebase.h"
#include <circle/bcm2835.h>
#include <circle/dmacommon.h>
#include <circle/machineinfo.h>
#include <circle/memio.h>
#include <circle/new.h>
#include <circle/synchronize.h>
#include <assert.h>

namespace sppie {

// SMI registers (ARM side and bus side)
static const uintptr SmiBase		= ARM_IO_BASE + 0x600000;
static const uintptr SmiCS		= SmiBase + 0x00;	// control and status
static const uintptr SmiL		= SmiBase + 0x04;	// transfer count
static const uintptr SmiA		= SmiBase + 0x08;	// device and address
static const uintptr SmiDC		= SmiBase + 0x30;	// DMA control
static const u32 SmiDataBus		= GPU_IO_BASE + 0x600000 + 0x0C;	// data FIFO

// SMI CS bits
static const u32 SmiCSEnable		= 1u << 0;
static const u32 SmiCSDone		= 1u << 1;
static const u32 SmiCSActive		= 1u << 2;
static const u32 SmiCSStart		= 1u << 3;
static const u32 SmiCSClear		= 1u << 4;
static const u32 SmiCSPxlDat		= 1u << 14;
static const u32 SmiCSAxiFifoError	= 1u << 25;

// SMI DMA control: DREQ when the read FIFO holds at least one word (the
// Linux driver's read threshold), panic at 32 words, DMA requests on.
static const u32 SmiDCReadThreshold	= 1;
static const u32 SmiDCReadPanic		= 0x20;
static const u32 SmiDCWriteThreshold	= 0x3F;
static const u32 SmiDCWritePanic	= 0x20;
static const u32 SmiDCValue		=   (SmiDCWriteThreshold << 0)
					  | (SmiDCReadThreshold << 6)
					  | (SmiDCWritePanic << 12)
					  | (SmiDCReadPanic << 18)
					  | (1u << 28);		// DMAEN

// Bounded register polls: a hung peripheral must not hang the intake core.
static const unsigned PollLimit		= 100000;

CSmiEventReader::CSmiEventReader (void)
:	m_SMIMaster ((1u << 16) - 1, FALSE),	// SD0..SD15 only, no address pins
	m_SOEPin (SoePin, GPIOModeAlternateFunction1),
	m_SyncPin (SyncPin, GPIOModeOutput),
	m_NotEmptyPin (NotEmptyPin, GPIOModeInput),
	m_nDMAChannel (DMA_CHANNEL_NONE),
	m_pControlBlock (0),
	m_pRingAlloc (0),
	m_pRing (0),
	m_nRingBusAddress (0)
{
	m_SyncPin.Write (LOW);
}

CSmiEventReader::~CSmiEventReader (void)
{
	PeripheralEntry ();
	write32 (SmiCS, 0);
	if (m_nDMAChannel != DMA_CHANNEL_NONE)
	{
		write32 (ARM_DMACHAN_CS (m_nDMAChannel), CS_RESET);
	}
	PeripheralExit ();

	if (m_nDMAChannel != DMA_CHANNEL_NONE)
	{
		CMachineInfo::Get ()->FreeDMAChannel (m_nDMAChannel);
	}

	delete m_pControlBlock;
	delete [] m_pRingAlloc;
}

boolean CSmiEventReader::Initialize (void)
{
	m_nDMAChannel = CMachineInfo::Get ()->AllocateDMAChannel (DMA_CHANNEL_NORMAL);
	if (m_nDMAChannel == DMA_CHANNEL_NONE)
	{
		return FALSE;
	}

	PeripheralEntry ();
	write32 (ARM_DMA_ENABLE, read32 (ARM_DMA_ENABLE) | (1u << m_nDMAChannel));
	PeripheralExit ();
	DelayNanoseconds (1000000);

	PeripheralEntry ();
	write32 (ARM_DMACHAN_CS (m_nDMAChannel), CS_RESET);
	unsigned nPolls = 0;
	while ((read32 (ARM_DMACHAN_CS (m_nDMAChannel)) & CS_RESET) && ++nPolls < PollLimit)
	{
		// wait
	}
	// A lite channel cannot transfer more than 64 KB per control block.
	boolean bLite = (read32 (ARM_DMACHAN_DEBUG (m_nDMAChannel)) & DEBUG_LITE) != 0;
	PeripheralExit ();
	if (bLite)
	{
		return FALSE;
	}

	// The ring: cache-line aligned, written only by the DMA.
	const unsigned nRingBytes = RingWords * sizeof (u32);
	m_pRingAlloc = new (HEAP_DMA30) u8[nRingBytes + DATA_CACHE_LINE_LENGTH_MIN];
	if (m_pRingAlloc == 0)
	{
		return FALSE;
	}
	uintptr nRing = ((uintptr) m_pRingAlloc + DATA_CACHE_LINE_LENGTH_MIN - 1)
			& ~(uintptr) (DATA_CACHE_LINE_LENGTH_MIN - 1);
	m_pRing = (u32 *) nRing;
	for (unsigned i = 0; i < RingWords; i++)
	{
		m_pRing[i] = 0;
	}
	CleanAndInvalidateDataCacheRange (nRing, nRingBytes);
	m_nRingBusAddress = BUS_ADDRESS (nRing);

	// One control block over the whole ring that loops to itself.
	m_pControlBlock = new (HEAP_DMA30) TDMAControlBlock;
	if (m_pControlBlock == 0)
	{
		return FALSE;
	}
	m_pControlBlock->nTransferInformation     =   (DREQSourceSMI << TI_PERMAP_SHIFT)
						    | TI_SRC_DREQ
						    | TI_DEST_INC
						    | TI_WAIT_RESP;
	m_pControlBlock->nSourceAddress           = SmiDataBus;
	m_pControlBlock->nDestinationAddress      = m_nRingBusAddress;
	m_pControlBlock->nTransferLength          = nRingBytes;
	m_pControlBlock->n2DModeStride            = 0;
	m_pControlBlock->nNextControlBlockAddress = BUS_ADDRESS ((uintptr) m_pControlBlock);
	m_pControlBlock->nReserved[0]             = 0;
	m_pControlBlock->nReserved[1]             = 0;
	CleanAndInvalidateDataCacheRange ((uintptr) m_pControlBlock, sizeof *m_pControlBlock);

	// SMI clock, width and timing for device 0 (also resets the SMI
	// registers).
	m_SMIMaster.SetupTiming (SMI16Bits, SmiCycleNs, SmiSetup, SmiStrobe, SmiHold, SmiPace, 0);

	PeripheralEntry ();
	write32 (SmiCS, SmiCSPxlDat);		// disabled, read direction, packing on
	write32 (SmiA, 0);			// device 0, address 0
	write32 (SmiDC, SmiDCValue);
	PeripheralExit ();

	return TRUE;
}

unsigned CSmiEventReader::Start (void)
{
	StopReads ();
	PulseSync ();
	PrepareTransfer ();

	PeripheralEntry ();
	write32 (ARM_DMACHAN_CONBLK_AD (m_nDMAChannel), BUS_ADDRESS ((uintptr) m_pControlBlock));
	write32 (ARM_DMACHAN_CS (m_nDMAChannel),   CS_WAIT_FOR_OUTSTANDING_WRITES
						 | (DEFAULT_PANIC_PRIORITY << CS_PANIC_PRIORITY_SHIFT)
						 | (DEFAULT_PRIORITY << CS_PRIORITY_SHIFT)
						 | CS_ACTIVE);
	PeripheralExit ();

	// The DMA loads the control block before its destination register
	// holds a ring address.
	unsigned nIndex = RingWords;
	for (unsigned nPolls = 0; nIndex == RingWords && nPolls < PollLimit; nPolls++)
	{
		nIndex = ProducerIndex ();
	}
	if (nIndex == RingWords)
	{
		nIndex = 0;
	}

	GoTransfer ();

	return nIndex;
}

unsigned CSmiEventReader::ProducerIndex (void) const
{
	PeripheralEntry ();
	u32 nDest = read32 (ARM_DMACHAN_DEST_AD (m_nDMAChannel));
	PeripheralExit ();

	if (nDest < m_nRingBusAddress)
	{
		return RingWords;
	}

	u32 nIndex = (nDest - m_nRingBusAddress) / sizeof (u32);
	if (nIndex > RingWords)
	{
		return RingWords;
	}

	// The end address can be seen just before the control block reloads.
	return nIndex == RingWords ? 0 : nIndex;
}

boolean CSmiEventReader::TransferDone (void) const
{
	PeripheralEntry ();
	u32 nCS = read32 (SmiCS);
	PeripheralExit ();

	return (nCS & SmiCSDone) != 0 && (nCS & SmiCSActive) == 0;
}

void CSmiEventReader::Restart (void)
{
	PrepareTransfer ();
	GoTransfer ();
}

unsigned CSmiEventReader::Resync (void)
{
	StopReads ();

	// Let the DMA take what is already in the SMI FIFO.
	DelayNanoseconds (2000);

	PulseSync ();

	PrepareTransfer ();		// clears the FIFO

	unsigned nIndex = RingWords;
	for (unsigned nPolls = 0; nIndex == RingWords && nPolls < PollLimit; nPolls++)
	{
		nIndex = ProducerIndex ();
	}
	if (nIndex == RingWords)
	{
		nIndex = 0;
	}

	GoTransfer ();

	return nIndex;
}

void CSmiEventReader::InvalidateWords (unsigned nFirst, unsigned nEnd) const
{
	assert (nFirst < nEnd && nEnd <= RingWords);
	InvalidateDataCacheRange ((uintptr) &m_pRing[nFirst], (nEnd - nFirst) * sizeof (u32));
}

boolean CSmiEventReader::NotEmpty (void)
{
	return m_NotEmptyPin.Read () == HIGH;
}

void CSmiEventReader::StopReads (void)
{
	PeripheralEntry ();
	write32 (SmiCS, SmiCSPxlDat);
	unsigned nPolls = 0;
	while ((read32 (SmiCS) & SmiCSEnable) && ++nPolls < PollLimit)
	{
		// wait
	}
	PeripheralExit ();
}

void CSmiEventReader::PrepareTransfer (void)
{
	StopReads ();

	PeripheralEntry ();
	write32 (SmiL, SmiBatchTransfers);
	write32 (SmiCS, SmiCSEnable | SmiCSPxlDat);
	unsigned nPolls = 0;
	while ((read32 (SmiCS) & SmiCSActive) && ++nPolls < PollLimit)
	{
		// wait
	}
	write32 (SmiCS, SmiCSEnable | SmiCSPxlDat | SmiCSClear | SmiCSAxiFifoError);
	PeripheralExit ();
}

void CSmiEventReader::GoTransfer (void)
{
	PeripheralEntry ();
	write32 (SmiCS, SmiCSEnable | SmiCSPxlDat | SmiCSStart);
	PeripheralExit ();
}

void CSmiEventReader::PulseSync (void)
{
	// Reads are stopped by the caller. SYNC high for 2 us, then 2 us low
	// before any read (protocol: at least 1 us each).
	m_SyncPin.Write (HIGH);
	DelayNanoseconds (2000);
	m_SyncPin.Write (LOW);
	DelayNanoseconds (2000);
}

}
