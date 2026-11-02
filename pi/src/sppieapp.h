//
// sppieapp.h
//
// The work of each core.
//
//  Core 0: renderer. Draws the state model into a RAM buffer, copies it to
//          the hidden half of a double-height frame buffer, moves the
//          display to that half, and waits for HDMI vertical sync. Core 0
//          also takes Circle's peripheral interrupts (the system timer).
//  Core 1: event intake. Polls the DMA producer position, decodes new ring
//          words into the state model, restarts SMI transfers and resyncs
//          on H-bit mismatches. It takes no interrupts and never blocks.
//  Cores 2 and 3: unused; they return from Run () and halt.
//
// The two cores share the state model (written by core 1, read by core 0)
// and the counters in TIntakeShared (written by core 1 with atomic stores,
// read by core 0 with atomic loads).
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_sppieapp_h
#define _sppie_sppieapp_h

#include "smieventreader.h"
#include "spectrumstate.h"
#include <circle/bcmframebuffer.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/types.h>

namespace sppie {

// Cumulative counters, written only by the intake core.
struct TIntakeShared
{
	u64	Events;
	u64	EmptyPairs;
	u64	Overflows;
	u64	Mismatches;
	u64	Malformed;
	u64	BusyTicks;		// time spent on ring words, restarts and resyncs
	u64	Restarts;		// SMI transfers started after the first
	u64	MaxRestartGapTicks;	// upper bound of the read gap at a restart
	u64	MaxBacklogWords;	// largest producer - consumer distance seen
	u64	BadProducer;		// DMA destination samples outside the ring
	u64	Running;		// set once reads have started
};

class CSpPieApp : public CMultiCoreSupport
{
public:
	CSpPieApp (CMemorySystem *pMemorySystem, CBcmFrameBuffer *pFrameBuffer,
		   CSmiEventReader *pReader, unsigned nArmClockMHz);

	void Run (unsigned nCore) override;

private:
	void RunRenderer (void);
	void RunIntake (void);
	void Publish (u64 *pField, u64 nValue);
	u64 Fetch (const u64 *pField);

private:
	CBcmFrameBuffer	*m_pFrameBuffer;
	CSmiEventReader	*m_pReader;
	unsigned	m_nArmClockMHz;

	CSpectrumState	m_State;
	TIntakeShared	m_Shared;
};

}

#endif
