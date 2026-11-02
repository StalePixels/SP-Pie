//
// ulaclock.h
//
// Turns the CPLD's clock count into the ULA's T-state count on models where
// the edge-connector clock stops during contention.
//
// On the +2A/+3 the edge-connector clock (CKEXT) runs without stopping: the
// count is the ULA's T-state count. On the 48K and the 128K the
// edge-connector clock is the Z80 clock, which the ULA holds during
// contention (Sinclair Wiki, "ZX Spectrum 16K/48K edge connector": the
// clock "is interrupted during contended memory access"). There the count
// is Z80 time: the ULA time of a write is its count plus every T-state the
// ULA held the Z80 before it, the stall.
//
// The ULA clock rebuilds the stall from the Pi's own time:
//
//  - Each empty pair gives the count c at a known moment τ (the end of the
//    read before it, from its position in the ring, ringclock.h). The ULA
//    time at τ is u(τ) = phase + rate × τ. The count is the ULA edges so far
//    less the stall, so the stall at τ is the s with
//    c + s <= u(τ) < c + s + 1.
//  - The stall never goes down and grows only in whole T-states. The ULA
//    stops the Z80 only while it fetches the screen, 128 T-states of each
//    display line. From the interrupt to the first display line, over
//    14,000 T-states, it stops it not at all: after each F event the stall
//    is held and every pair narrows down the phase. In the display, every
//    line still has about 100 T-states with no stall.
//  - A restart of the reads moves the read times by an amount the Pi only
//    knows to its timer and the transfer's start latency. In the border,
//    where nothing stalls, the stall is held for RestartHoldTStates after a
//    restart and the pairs set the phase again; in stopped mode the intake
//    core ends transfers only in the border (machine.h, InBorder). A
//    restart in the display keeps the phase, widened, and is counted. The clock keeps the phase as
//    the interval of values that fit every pair since the last stall
//    (widened a little for drift and for each restart of the reads), and a
//    pair that fits only with a larger stall is a stall. A pair that fits
//    two stalls (it lies at a T-state edge, within the phase interval)
//    decides nothing; the events before it wait for the next pair that
//    does.
//  - The rate is the ULA clock per Pi timer tick: the model's frame length
//    in T-states over the frame period, measured from the read times of the
//    last RateFrames F events. The phase is held once LockFrames of them
//    have come.
//
// An event lies between the empty pair before it and the one after it, so
// its stall is one of theirs or in between. When they differ:
//
//  - A write to 0x4000 to 0x7FFF is contended on every model. Fuse lets a
//    contended write end only 3 T-states after a T-state with no
//    contention delay (memory_pages.c, writebyte), so of the candidates the
//    one that fits Fuse's contention table is taken (CFrameClock::
//    ContentionAt), preferring a cycle that waited.
//  - An I/O write takes the stall before it: Fuse writes the port before
//    the I/O contention that follows (periph.c, writeport).
//  - Any other write takes the stall after it. Either way the choice is
//    counted as ambiguous.
//
// Model facts. The frame period gives the class: 19.968 ms is a 48K,
// 19.9915 ms the 128K class; the count cannot, as a stopping clock makes
// frames short. Any short frame since the last reset rules out the
// +2A/+3, which has a clock that never stops. Both go to the frame clock
// (CFrameClock::SetClockFacts).
//
// Clock behaviour. The clock watches the count between the F events of two
// frames in a row. Two such frames more than MaxAckDelayTStates shorter
// than the frame length put it in stopped mode; FreeFrames frames in a row
// at the frame length put it back in free mode, where the count passes
// through unchanged. In stopped mode the stall a frame loses is the
// difference, and the counts it gives on are ULA T-states, so the frame
// clock sees frames of the right length. The frame clock's contention check
// is off in stopped mode: the writes it would check were placed by the
// same contention table.
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
	static const unsigned StoppedFrames	= 2;
	static const unsigned FreeFrames	= 50;
	static const unsigned MaxPending	= 64;
	static const unsigned StatsFrames	= 50;
	static const unsigned RateFrames	= 16;	// F events in the rate measurement
	static const unsigned LockFrames	= 4;	// F events before the phase is held

	// pFrames gives the frame length, and the contention delay at a count
	// (it is also pSink in use). nTickFrequency: the Pi timer's rate.
	CUlaClock (ITimedEventSink *pSink, CFrameClock *pFrames, uint64_t nTickFrequency);

	void Reset (void);

	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override;
	void Clock (uint64_t nCount, double nTicks) override;
	void ReadsRestarted (void) override;

	bool Stopped (void) const		{ return m_bStopped; }
	uint64_t Ambiguous (void) const		{ return m_nAmbiguous; }
	uint64_t PhaseSlips (void) const	{ return m_nSlips; }
	uint64_t RestartsInDisplay (void) const	{ return m_nRestartsInDisplay; }

	// Counts between the F events of two frames in a row, in the CPLD's
	// count: the last, and the smallest and largest of the last
	// StatsFrames. 0 before the first.
	uint32_t LastFrameClocks (void) const	{ return m_nLastClocks; }
	uint32_t MinFrameClocks (void) const;
	uint32_t MaxFrameClocks (void) const;

	// ULA T-states per Pi timer tick in use.
	double Rate (void) const		{ return m_nRate; }

	// When the next frame starts, in Pi timer ticks, in stopped mode with
	// the phase held; FALSE otherwise. For planning SMI restarts.
	bool NextFrameStartTicks (double &rTicks) const;

private:
	void FrameEvent (uint64_t nCount, double nTicks);
	void Lock (uint64_t nCount, double nTicks);
	uint64_t Place (const TEvent &rEvent, uint64_t nCount, uint64_t nStallBefore,
			uint64_t nStallAfter);
	void Flush (uint64_t nStallAfter);

private:
	ITimedEventSink	*m_pSink;
	CFrameClock	*m_pFrames;
	double		m_nTickFrequency;

	bool		m_bStopped;
	bool		m_bStoppedSinceReset;	// a frame was short since the last reset
	bool		m_bHaveClass;
	TMachine	m_Class;		// from the frame period
	unsigned	m_nStoppedVotes;
	unsigned	m_nFreeVotes;

	// The phase: u(τ) = m_nPhase + m_nRate × (τ - m_nTicksRef); the true
	// ULA time is u(τ) - e, with e in [m_nLow, m_nHigh].
	double		m_nRate;
	double		m_nTicksRef;
	double		m_nPhase;
	double		m_nLow;
	double		m_nHigh;
	double		m_nLastTicks;
	bool		m_bHavePhase;
	uint64_t	m_nStall;		// stall at the last empty pair
	uint64_t	m_nSlips;

	// F events: in counts (clock behaviour) and in ticks (the rate).
	bool		m_bHaveFrameEvent;
	uint64_t	m_nLastFrameCount;
	double		m_FrameTicks[RateFrames];
	unsigned	m_nFrameTicks;
	unsigned	m_nFrameTicksNext;
	bool		m_bPendingFrame;	// an F event waits for its read time
	uint64_t	m_nPendingFrameCount;
	bool		m_bRestarted;		// reads restarted since the last empty pair
	bool		m_bRestartInBorder;
	uint64_t	m_nRestartsInDisplay;
	uint64_t	m_nLastCount;		// count of the last empty pair
	uint64_t	m_nPrevStall;		// stall at the last pair that decided it
	uint64_t	m_nLastEvent;		// last count given on
	double		m_nHoldUntil;		// no contention before this tick

	uint32_t	m_nLastClocks;
	uint32_t	m_Clocks[StatsFrames];
	unsigned	m_nClocks;
	unsigned	m_nClocksNext;

	struct TPending
	{
		TEvent		Event;
		uint64_t	Count;
	};
	TPending	m_Pending[MaxPending];
	unsigned	m_nPending;

	uint64_t	m_nAmbiguous;
};

}

#endif
