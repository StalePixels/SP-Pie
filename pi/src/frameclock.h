//
// frameclock.h
//
// Puts a Spectrum frame number and a Fuse T-state on every event, from its
// clock count (tstateclock.h), and writes it to the event log.
//
// Frame start. The CPLD marks with F the first write after each interrupt
// acknowledge, which is the push of the return address. Its count less
// FrameEventTStates (machine.h) is the frame start plus the time the Z80
// took to finish the instruction it was running when the interrupt came.
// The frame start is the earliest of these, so:
//
//  - the first F event sets the frame start;
//  - an F event up to MaxAckDelayTStates before the frame start moves the
//    frame start back to it;
//  - an F event up to MaxAckDelayTStates after it agrees with it;
//  - RelockEvents F events in a row that agree with neither set the frame
//    start again, from the last of them.
//
// With a clock that runs without stopping, the frame start in counts never
// moves by itself, so once one acknowledge has come at the start of an
// instruction boundary the frame start is exact for good. Until the first
// F event (or with interrupts off from the start) the frames run from an
// arbitrary start.
//
// Frame length. The distance between the F events of two frames in a row
// is the clock count of one frame, give or take the instruction time. It
// is kept for the overlay (last, smallest and largest of the last
// StatsFrames frames) and decides the model's frame-length class: 48K or
// 128K, whichever frame length it is within MaxAckDelayTStates of, after
// ClassVotes such frames in a row. A clock that stops during contention
// gives shorter frames that match neither, and the class stays as it is.
//
// Model. The class, the machine choice and whether a write to port 0x1FFD
// has been seen give the model (ResolveMachine). The frame length of the
// model is libspectrum's, given to the constructor. A change of frame
// length sets the frame start again.
//
// T-state. A memory write or reset takes effect at its count; an I/O write
// IoWriteLeadTStates earlier (machine.h). Positions in the log never go
// backwards: an event that would land before the last logged position, or
// in a completed frame, gets the last position instead and is counted.
//
// Completion. When the count passes the end of a frame by more than the
// capture lag, every event of that frame is in the log, and the frame is
// marked complete.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_frameclock_h
#define _sppie_frameclock_h

#include "eventlog.h"
#include "machine.h"
#include "tstateclock.h"
#include <stdint.h>

namespace sppie {

class CFrameClock final : public ITimedEventSink
{
public:
	static const unsigned RelockEvents	= 8;
	static const unsigned ClassVotes	= 4;
	static const unsigned StatsFrames	= 50;

	// pFrameTStates: T-states per frame of each TMachine.
	CFrameClock (CEventLog *pLog, const uint32_t pFrameTStates[MachineCount],
		     TMachineChoice Choice = ChooseAuto);

	void Reset (void);

	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override;
	void Clock (uint64_t nCount) override;

	bool Locked (void) const		{ return m_bLocked; }
	uint32_t Acquisitions (void) const	{ return m_nAcquisitions; }
	TMachine Machine (void) const		{ return m_Machine; }
	uint32_t FrameTStates (void) const	{ return m_nFrameTStates; }
	uint32_t CompletedFrames (void) const	{ return m_nCompleted; }
	uint64_t Clamped (void) const		{ return m_nClamped; }

	// Clock counts between the F events of two frames in a row: the last
	// one, and the smallest and largest of the last StatsFrames. 0 before
	// the first.
	uint32_t LastFrameClocks (void) const	{ return m_nLastClocks; }
	uint32_t MinFrameClocks (void) const;
	uint32_t MaxFrameClocks (void) const;

	// The count at which frame nFrame starts (for the tests).
	uint64_t FrameStart (uint32_t nFrame) const;

private:
	void FrameEvent (uint64_t nCount);
	void SetStart (uint64_t nStart);
	void UpdateMachine (void);
	void Locate (uint64_t nCount, uint32_t &rFrame, uint32_t &rTState) const;

private:
	CEventLog	*m_pLog;
	uint32_t	m_FrameTStates[MachineCount];
	TMachineChoice	m_Choice;

	TMachine	m_Class;		// measured: Machine48K or Machine128K
	bool		m_bPort1FFDSeen;
	TMachine	m_Machine;		// model of the frames being logged
	uint32_t	m_nFrameTStates;

	bool		m_bHaveStart;
	bool		m_bLocked;
	uint64_t	m_nStart;		// count at which frame m_nStartFrame starts
	uint32_t	m_nStartFrame;
	unsigned	m_nOffEvents;		// F events in a row that disagree
	uint32_t	m_nAcquisitions;

	bool		m_bHaveFrameEvent;
	uint64_t	m_nLastFrameEvent;
	unsigned	m_nVotes;
	TMachine	m_VoteClass;

	uint32_t	m_nLastClocks;
	uint32_t	m_Clocks[StatsFrames];
	unsigned	m_nClocks;
	unsigned	m_nClocksNext;

	uint32_t	m_nLastFrame;		// last logged position
	uint32_t	m_nLastTState;
	uint32_t	m_nCompleted;		// frames before this are complete
	uint64_t	m_nClamped;
};

}

#endif
