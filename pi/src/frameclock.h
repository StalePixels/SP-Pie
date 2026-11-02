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
// Contention check. In Fuse's model a memory write to contended memory
// ends 3 T-states after a T-state where the contention delay is 0
// (memory_pages.c, writebyte: the delay, then 3 T-states). Inside the
// screen fetch of a display line that is one T-state in 8 on the +2A/+3
// and two in 8 on the 48K and 128K. Writes to 0x4000 to 0x7FFF are
// contended on every model, and a multicolour engine makes many of them
// while the ULA fetches. For each such write, the clock counts which of
// the frame starts 4 T-states before to 3 after the current one would put
// the write where Fuse's contention table (SetContention) says it cannot
// end. After AlignWrites such writes it moves the frame start to the one
// that fits, the nearest when two fit. From then on F events only check the
// frame start; they no longer move it back. This makes the frame start
// exact while a program writes contended memory during the display, also
// when every acknowledge comes late or an F event comes early.
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
// has been seen since the last reset event and the last class change give
// the model (ResolveMachine). The frame length of the
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
	static const unsigned AlignWrites	= 256;

	// pFrameTStates: T-states per frame of each TMachine.
	CFrameClock (CEventLog *pLog, const uint32_t pFrameTStates[MachineCount],
		     TMachineChoice Choice = ChooseAuto);

	void Reset (void);

	// Fuse's contention delay for each T-state of a model's frame
	// (fuseula_contention). The table is kept, not copied.
	void SetContention (TMachine Machine, const uint8_t *pTable);

	// The contention check on or off (on after Reset ()).
	void EnableContentionCheck (bool bEnable);

	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override;
	void Clock (uint64_t nCount, double nTicks) override;

	bool Locked (void) const		{ return m_bLocked; }
	uint32_t Acquisitions (void) const	{ return m_nAcquisitions; }
	TMachine Machine (void) const		{ return m_Machine; }
	uint32_t FrameTStates (void) const	{ return m_nFrameTStates; }
	uint32_t CompletedFrames (void) const	{ return m_nCompleted; }
	uint64_t Clamped (void) const		{ return m_nClamped; }
	uint32_t Alignments (void) const	{ return m_nAlignments; }	// frame start moves by the contention check
	bool Aligned (void) const		{ return m_bAligned; }

	// Clock counts between the F events of two frames in a row: the last
	// one, and the smallest and largest of the last StatsFrames. 0 before
	// the first.
	uint32_t LastFrameClocks (void) const	{ return m_nLastClocks; }
	uint32_t MinFrameClocks (void) const;
	uint32_t MaxFrameClocks (void) const;

	// Fuse's contention delay at the T-state a count falls on, or -1 when
	// there is no table or no frame start yet.
	int ContentionAt (uint64_t nCount) const;

	// The count at which frame nFrame starts (for the tests).
	uint64_t FrameStart (uint32_t nFrame) const;

private:
	void FrameEvent (uint64_t nCount);
	void SetStart (uint64_t nStart);
	void UpdateMachine (void);
	void Locate (uint64_t nCount, uint32_t &rFrame, uint32_t &rTState) const;
	void CheckContention (uint32_t nTState);
	void ClearAlignment (void);

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

	const uint8_t	*m_pContention[MachineCount];
	uint32_t	m_Misfits[8];		// writes that do not fit, per frame start -4..3
	uint32_t	m_nFitWrites;
	bool		m_bAligned;
	uint32_t	m_nAlignments;
	bool		m_bCheckContention;

	uint32_t	m_nLastFrame;		// last logged position
	uint32_t	m_nLastTState;
	uint32_t	m_nCompleted;		// frames before this are complete
	uint64_t	m_nClamped;
};

}

#endif
