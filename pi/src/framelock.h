//
// framelock.h
//
// Locks the Pi's time base to the Spectrum frame, using the arrival times of
// events with F = 1 (see docs/protocol.md, "Frame events").
//
// An F event arrives at
//
//     interrupt time + Z80 delay + transport latency
//
// The Z80 delay is FrameEventTStates plus the rest of the instruction the
// Z80 was running (0 to about 23 T-states). The transport latency varies by
// under a microsecond. Both are never negative, so the earliest arrivals sit
// on a straight line, one point per frame: the "earliest-arrival line"
// E(n) = E(0) + n * period. The lock estimates that line:
//
//  - Acquisition: AcqSamples F events, at most AcqMaxGapFrames frames apart,
//    give a first period (span / frames) and the line through the earliest
//    of them.
//  - Tracking: the F events are grouped in blocks of BlockFrames frames. The
//    earliest arrival of each block is one point. A least-squares line
//    through the last HistoryBlocks points corrects the period and the
//    phase after each block.
//  - Flywheel: with no F events (the Spectrum runs with interrupts off), the
//    line simply continues with the last period and phase.
//  - Re-acquisition: when the newest block points lie more than
//    ReacquireNs off the line twice in a row, or OutlierRunLimit F events in
//    a row are more than an eighth of a frame off, the lock acquires again.
//    The old line keeps running meanwhile.
//
// The frame starts (the interrupt, as seen in arrival time) are
//
//     S(n) = E(n) - FrameEventTStates T-states + TransportCalibrationNs
//
// so that an ordinary event's arrival time minus S(n) is its write time in
// the frame (see machinetiming.h).
//
// The measured period selects the 48K or the 128K class; a TMachineChoice
// turns that into the model (see machinetiming.h).
//
// Times are ticks of the caller's clock (the ARM generic timer on the Pi).
// Fixed-point, Q16 ticks, integer only. No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_framelock_h
#define _sppie_framelock_h

#include "machinetiming.h"
#include <stdint.h>

namespace sppie {

class CFrameLock
{
public:
	static const unsigned AcqSamples	= 16;
	static const unsigned AcqMaxGapFrames	= 8;
	static const unsigned BlockFrames	= 32;
	static const unsigned HistoryBlocks	= 16;
	static const unsigned MinFitBlocks	= 2;
	static const unsigned GapFrames		= 256;	// longer F silence: tracking history restarts
	static const unsigned ReacquireNs	= 100000;
	static const unsigned OutlierRunLimit	= 4;

	explicit CFrameLock (uint64_t nTickFrequency, TMachineChoice Choice = ChooseAuto);

	// Free-running start: nominal 128K-class period (48K period when the
	// choice is Choose48K), frame 0 starting at about nNow.
	void Start (uint64_t nNow);

	// An F event arrived at nTicks. Arrival times must not decrease.
	void FrameEvent (uint64_t nTicks);

	// Frame number and T-state (0 to FrameTStates - 1) of an arrival time.
	void Position (uint64_t nTicks, uint32_t &rFrame, uint32_t &rTState) const;

	// First tick at which Position () gives nFrame or later.
	uint64_t FrameStart (uint32_t nFrame) const;

	// Moves the internal base frame up to the frame of nTicks, so the
	// arithmetic stays small during a long flywheel. The line is unchanged.
	void Rebase (uint64_t nTicks);

	bool Locked (void) const		{ return m_bLocked; }
	TMachine Machine (void) const		{ return m_Machine; }	// the model in use
	const TMachineTiming &Timing (void) const { return MachineTiming (m_Machine); }
	uint64_t PeriodNs (void) const;
	uint32_t Version (void) const		{ return m_nVersion; }	// changes with the line
	uint32_t Acquisitions (void) const	{ return m_nAcquisitions; }
	uint32_t Outliers (void) const		{ return m_nOutliers; }

private:
	int64_t RelQ (uint64_t nTicks) const;
	int64_t ResidualQ (uint32_t nFrame, uint64_t nTicks) const;
	void SetBase (uint32_t nFrame, uint64_t nTicks, int64_t nOffsetQ);
	void UpdateMachine (void);
	void StartAcquisition (void);
	void Acquire (uint64_t nTicks);
	void CloseBlock (void);
	int64_t NsToQ (uint64_t nNs) const;

private:
	uint64_t	m_nFrequency;
	TMachineChoice	m_Choice;

	// The earliest-arrival line: E(m_nBaseFrame) = m_nBaseTicks +
	// m_nBaseFrac / 65536, slope m_nPeriodQ / 65536 ticks per frame.
	uint64_t	m_nBaseTicks;
	uint32_t	m_nBaseFrac;
	uint32_t	m_nBaseFrame;
	int64_t		m_nPeriodQ;
	int64_t		m_nStartOffsetQ;	// E(n) - S(n)

	bool		m_bLocked;
	TMachine	m_Machine;
	uint32_t	m_nVersion;
	uint32_t	m_nAcquisitions;
	uint32_t	m_nOutliers;

	// acquisition
	unsigned	m_nAcqCount;
	uint32_t	m_AcqFrames[AcqSamples];
	uint64_t	m_AcqTicks[AcqSamples];

	// tracking
	bool		m_bHaveLast;
	uint32_t	m_nLastFrame;
	unsigned	m_nOutlierRun;
	unsigned	m_nFarBlocks;
	bool		m_bBlockOpen;
	uint32_t	m_nBlockStart;
	int64_t		m_nBlockMinQ;
	uint32_t	m_nBlockMinFrame;
	uint64_t	m_nBlockMinTicks;
	unsigned	m_nHistoryCount;
	unsigned	m_nHistoryNext;
	uint32_t	m_HistoryFrames[HistoryBlocks];
	uint64_t	m_HistoryTicks[HistoryBlocks];
};

}

#endif
