//
// ulaclock.h
//
// What the CPLD's clock count tells about the Spectrum, on the core that
// places events in frames (core 2). The count passes through unchanged.
//
// The CPLD counts the ULA's T-states on every model (docs/protocol.md,
// Time base). On the +2A/+3 the edge-connector clock runs without stopping.
// On the 48K and the 128K it is the Z80 clock, which the ULA holds during
// contention; the CPLD measures each held phase against the Pi's 25 MHz
// clock on GPIO 4 and adds the T-states it lasted. An empty pair with STL
// set says that such a stall was measured since the empty pair before.
//
//  - Class. The frame period, measured from the read times of the last
//    RateFrames F events, gives the class: 19.968 ms is a 48K (69,888
//    T-states at 3.5 MHz), 19.9915 ms the 128K class (70,908 at 3.5469 MHz).
//  - Clock. A stall since the last reset rules out the +2A/+3, whose clock
//    never stops, until the next reset. So does a frame shorter than its
//    length, which a CPLD without the fast clock gives on a 48K or 128K.
//    Both go to the frame clock (CFrameClock::SetClockFacts). A 128K
//    program that never touches contended memory gives no stall; see
//    CFrameClock for how a +2A/+3 is then told apart.
//  - Clocks per frame. The count between the F events of two frames in a
//    row, for the overlay: the last, and the smallest and largest of the
//    last StatsFrames. 69,888 on the 48K and 70,908 on the 128K class,
//    give or take the instruction the Z80 was running.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_ulaclock_h
#define _sppie_ulaclock_h

#include "machine.h"
#include "tstateclock.h"
#include <stdint.h>

namespace sppie {

class CFrameClock;

class CUlaClock final : public ITimedEventSink
{
public:
	static const unsigned StatsFrames	= 50;
	static const unsigned RateFrames	= 16;

	// pSink receives everything unchanged; pFrames gets the model facts
	// (in use the two are the same frame clock).
	CUlaClock (ITimedEventSink *pSink, CFrameClock *pFrames, uint64_t nTickFrequency);

	void Reset (void);

	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override;
	void Clock (uint64_t nCount, double nTicks, bool bStall) override;
	void ReadsRestarted (void) override	{ m_pSink->ReadsRestarted (); }

	bool Stopped (void) const		{ return m_bStoppedSinceReset; }
	bool HaveClass (void) const		{ return m_bHaveClass; }
	TMachine Class (void) const		{ return m_Class; }

	uint32_t LastFrameClocks (void) const	{ return m_nLastClocks; }
	uint32_t MinFrameClocks (void) const;
	uint32_t MaxFrameClocks (void) const;

private:
	void FrameEvent (uint64_t nCount, double nTicks);
	void ClockStopped (void);

private:
	ITimedEventSink	*m_pSink;
	CFrameClock	*m_pFrames;
	double		m_nTickFrequency;

	bool		m_bStoppedSinceReset;
	bool		m_bHaveClass;
	TMachine	m_Class;

	bool		m_bPendingFrame;	// an F event waits for its read time
	uint64_t	m_nPendingFrameCount;
	bool		m_bHaveFrameEvent;
	uint64_t	m_nLastFrameCount;
	double		m_FrameTicks[RateFrames];
	unsigned	m_nFrameTicks;
	unsigned	m_nFrameTicksNext;

	uint32_t	m_nLastClocks;
	uint32_t	m_Clocks[StatsFrames];
	unsigned	m_nClocks;
	unsigned	m_nClocksNext;
};

}

#endif
