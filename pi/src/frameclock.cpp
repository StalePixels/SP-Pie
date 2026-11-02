//
// frameclock.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "frameclock.h"

namespace sppie {

CFrameClock::CFrameClock (CEventLog *pLog, const uint32_t pFrameTStates[MachineCount],
			  TMachineChoice Choice)
:	m_pLog (pLog),
	m_Choice (Choice)
{
	for (unsigned i = 0; i < MachineCount; i++)
	{
		m_FrameTStates[i] = pFrameTStates[i];
		m_pContention[i] = 0;
	}
	Reset ();
}

void CFrameClock::Reset (void)
{
	m_Class = Machine128K;
	m_bPort1FFDSeen = false;
	m_Machine = ResolveMachine (m_Class, m_Choice, false);
	m_nFrameTStates = m_FrameTStates[m_Machine];

	m_bHaveStart = false;
	m_bLocked = false;
	m_nStart = 0;
	m_nStartFrame = 0;
	m_nOffEvents = 0;
	m_nAcquisitions = 0;

	m_bHaveFrameEvent = false;
	m_nLastFrameEvent = 0;
	m_nVotes = 0;
	m_VoteClass = m_Class;

	m_nLastClocks = 0;
	m_nClocks = 0;
	m_nClocksNext = 0;

	m_nLastFrame = 0;
	m_nLastTState = 0;
	m_nCompleted = 0;
	m_nClamped = 0;

	m_bInReset = false;
	m_nResetEnd = 0;
	m_nResets = 0;
	m_nPort1FFDWrite = 0;
	m_bCompleteAtValid = false;

	ClearAlignment ();
	m_nAlignments = 0;
	m_bCheckContention = true;
}

void CFrameClock::EnableContentionCheck (bool bEnable)
{
	m_bCheckContention = bEnable;
	ClearAlignment ();
}

void CFrameClock::SetContention (TMachine Machine, const uint8_t *pTable)
{
	if (Machine < MachineCount)
	{
		m_pContention[Machine] = pTable;
	}
}

int CFrameClock::ContentionAt (uint64_t nCount) const
{
	const uint8_t *pTable = m_pContention[m_Machine];
	if (pTable == 0 || !m_bHaveStart)
	{
		return -1;
	}
	uint32_t nFrame, nTState;
	Locate (nCount, nFrame, nTState);
	return pTable[nTState];
}

void CFrameClock::ClearAlignment (void)
{
	for (unsigned i = 0; i < 8; i++)
	{
		m_Misfits[i] = 0;
	}
	m_nFitWrites = 0;
	m_bAligned = false;
}

// nTState: where the current frame start puts a write to contended memory.
void CFrameClock::CheckContention (uint32_t nTState)
{
	const uint8_t *pTable = m_pContention[m_Machine];
	if (pTable == 0 || nTState < 3 + 4 || nTState + 4 >= m_nFrameTStates)
	{
		return;
	}

	// Candidate k puts the write's cycle, after its delay, at
	// nTState - 3 + (k - 4). Only writes inside the screen fetch count.
	const uint8_t *pDelay = pTable + nTState - 3 - 4;
	bool bInFetch = false;
	for (unsigned k = 0; k < 8; k++)
	{
		bInFetch = bInFetch || pDelay[k] != 0;
	}
	if (!bInFetch)
	{
		return;
	}
	for (unsigned k = 0; k < 8; k++)
	{
		if (pDelay[k] != 0)
		{
			m_Misfits[k]++;
		}
	}
	if (++m_nFitWrites < AlignWrites)
	{
		return;
	}

	// The candidate with the fewest misfits; on a tie the one nearest the
	// current start.
	static const unsigned Order[8] = { 4, 3, 5, 2, 6, 1, 7, 0 };
	unsigned nBest = Order[0];
	for (unsigned i = 1; i < 8; i++)
	{
		if (m_Misfits[Order[i]] < m_Misfits[nBest])
		{
			nBest = Order[i];
		}
	}

	bool bFits = m_Misfits[nBest] * 16 <= m_nFitWrites;
	for (unsigned i = 0; i < 8; i++)
	{
		m_Misfits[i] = 0;
	}
	m_nFitWrites = 0;
	if (!bFits)
	{
		return;
	}

	// The write is really at nTState + d, so the frame start is d earlier.
	int64_t d = (int64_t) nBest - 4;
	if (d != 0)
	{
		m_nStart = (uint64_t) ((int64_t) m_nStart - d);
		m_nAlignments++;
	}
	m_bAligned = true;
}

uint64_t CFrameClock::FrameStart (uint32_t nFrame) const
{
	return m_nStart + (uint64_t) (int32_t) (nFrame - m_nStartFrame) * m_nFrameTStates;
}

void CFrameClock::Locate (uint64_t nCount, uint32_t &rFrame, uint32_t &rTState) const
{
	if (nCount < m_nStart)
	{
		// Only just before a new frame start; the clamp below keeps the
		// order.
		rFrame = m_nStartFrame - 1;
		rTState = m_nFrameTStates - 1;
		return;
	}

	uint64_t nOffset = nCount - m_nStart;
	rFrame = m_nStartFrame + (uint32_t) (nOffset / m_nFrameTStates);
	rTState = (uint32_t) (nOffset % m_nFrameTStates);
}

// A new frame start at count nStart. It becomes the start of the first
// frame that has not been completed, or of a later one, so frame numbers
// keep increasing.
void CFrameClock::SetStart (uint64_t nStart)
{
	uint32_t nFrame = m_nCompleted;

	if (m_bHaveStart)
	{
		uint32_t nOldFrame, nOldTState;
		Locate (nStart, nOldFrame, nOldTState);
		nFrame = nOldFrame + (nOldTState >= m_nFrameTStates / 2 ? 1 : 0);
		if ((int32_t) (nFrame - m_nCompleted) < 0)
		{
			nFrame = m_nCompleted;
		}
	}

	m_nStart = nStart;
	m_nStartFrame = nFrame;
	m_bHaveStart = true;
	ClearAlignment ();
}

void CFrameClock::UpdateMachine (void)
{
	TMachine Machine = ResolveMachine (m_Class, m_Choice, m_bPort1FFDSeen);
	if (Machine == m_Machine)
	{
		return;
	}

	uint32_t nFrameTStates = m_FrameTStates[Machine];
	m_Machine = Machine;
	if (nFrameTStates != m_nFrameTStates && m_bHaveStart)
	{
		// The frame start stays; the frames from it get the new length.
		// The next F events set the phase.
		uint64_t nStart = FrameStart (m_nCompleted);
		m_nFrameTStates = nFrameTStates;
		m_nStart = nStart;
		m_nStartFrame = m_nCompleted;
		m_bLocked = false;
		m_nOffEvents = 0;
	}
	m_nFrameTStates = nFrameTStates;
	ClearAlignment ();
}

uint32_t CFrameClock::MinFrameClocks (void) const
{
	uint32_t nMin = 0;
	for (unsigned i = 0; i < m_nClocks; i++)
	{
		if (i == 0 || m_Clocks[i] < nMin)
		{
			nMin = m_Clocks[i];
		}
	}
	return nMin;
}

uint32_t CFrameClock::MaxFrameClocks (void) const
{
	uint32_t nMax = 0;
	for (unsigned i = 0; i < m_nClocks; i++)
	{
		if (m_Clocks[i] > nMax)
		{
			nMax = m_Clocks[i];
		}
	}
	return nMax;
}

void CFrameClock::FrameEvent (uint64_t nCount)
{
	// Frame length: only between the F events of two frames in a row.
	if (m_bHaveFrameEvent && nCount > m_nLastFrameEvent)
	{
		uint64_t nClocks = nCount - m_nLastFrameEvent;
		if (nClocks > m_nFrameTStates / 2 && nClocks < m_nFrameTStates * 3 / 2)
		{
			m_nLastClocks = (uint32_t) nClocks;
			m_Clocks[m_nClocksNext] = m_nLastClocks;
			m_nClocksNext = (m_nClocksNext + 1) % StatsFrames;
			if (m_nClocks < StatsFrames)
			{
				m_nClocks++;
			}

			TMachine Vote = MachineCount;
			if (nClocks + MaxAckDelayTStates > m_FrameTStates[Machine48K] &&
			    nClocks < m_FrameTStates[Machine48K] + MaxAckDelayTStates)
			{
				Vote = Machine48K;
			}
			else if (nClocks + MaxAckDelayTStates > m_FrameTStates[Machine128K] &&
				 nClocks < m_FrameTStates[Machine128K] + MaxAckDelayTStates)
			{
				Vote = Machine128K;
			}

			if (Vote == MachineCount || Vote == m_Class)
			{
				m_nVotes = 0;
			}
			else
			{
				m_nVotes = Vote == m_VoteClass ? m_nVotes + 1 : 1;
				m_VoteClass = Vote;
				if (m_nVotes >= ClassVotes)
				{
					m_Class = Vote;
					m_nVotes = 0;
					m_bPort1FFDSeen = false;
					UpdateMachine ();
				}
			}
		}
	}
	m_nLastFrameEvent = nCount;
	m_bHaveFrameEvent = true;

	// Frame start.
	uint64_t nStart = nCount - FrameEventTStates;

	if (!m_bLocked)
	{
		SetStart (nStart);
		m_bLocked = true;
		m_nOffEvents = 0;
		m_nAcquisitions++;
		return;
	}

	// Distance from the nearest frame start, -L/2 to L/2.
	int64_t nL = m_nFrameTStates;
	int64_t nDelay = (int64_t) ((nStart - m_nStart) % (uint64_t) nL);
	if (nStart < m_nStart)
	{
		nDelay = -(int64_t) ((m_nStart - nStart) % (uint64_t) nL);
	}
	if (nDelay > nL / 2)
	{
		nDelay -= nL;
	}
	else if (nDelay < -nL / 2)
	{
		nDelay += nL;
	}

	if (nDelay < 0 && nDelay > -(int64_t) MaxAckDelayTStates)
	{
		if (!m_bAligned)
		{
			m_nStart += nDelay;	// earlier
		}
		m_nOffEvents = 0;
	}
	else if (nDelay >= 0 && nDelay < (int64_t) MaxAckDelayTStates)
	{
		m_nOffEvents = 0;
	}
	else if (++m_nOffEvents >= RelockEvents)
	{
		SetStart (nStart);
		m_nOffEvents = 0;
		m_nAcquisitions++;
	}
}

void CFrameClock::TimedEvent (const TEvent &rEvent, uint64_t nCount)
{
	if (m_bInReset && nCount >= m_nResetEnd)
	{
		m_bInReset = false;
	}

	if (rEvent.Kind == KindReset)
	{
		// After a reset the board may sit on another Spectrum: the
		// +2A/+3 is detected again from its ROM's first 0x1FFD write, and
		// the class from the next frames.
		if (!m_bInReset)
		{
			m_nResets++;
		}
		m_bInReset = true;
		m_nResetEnd = nCount + ResetBurstTStates;
		m_nVotes = 0;
		if (m_bPort1FFDSeen)
		{
			m_bPort1FFDSeen = false;
			UpdateMachine ();
		}
	}

	if (   !m_bInReset && rEvent.Kind == KindIo && IsPort1FFD (rEvent.Address)
	    && !m_bPort1FFDSeen)
	{
		m_bPort1FFDSeen = true;
		m_nPort1FFDWrite = (uint32_t) rEvent.Address << 8 | rEvent.Data;
		UpdateMachine ();
	}

	if (rEvent.Frame && !rEvent.Overflow && !m_bInReset)
	{
		FrameEvent (nCount);
	}

	if (!m_bHaveStart)
	{
		SetStart (nCount);
	}

	uint64_t nEffect = nCount;
	if (rEvent.Kind == KindIo)
	{
		nEffect -= IoWriteLeadTStates;
	}

	uint32_t nFrame, nTState;
	Locate (nEffect, nFrame, nTState);

	if (   m_bLocked && m_bCheckContention && rEvent.Kind == KindMemory
	    && rEvent.Address >= 0x4000 && rEvent.Address < 0x8000)
	{
		CheckContention (nTState);
	}

	if ((int32_t) (nFrame - m_nCompleted) < 0)
	{
		nFrame = m_nCompleted;
		nTState = 0;
	}

	int32_t nFrameDiff = (int32_t) (nFrame - m_nLastFrame);
	if (nFrameDiff < 0 || (nFrameDiff == 0 && nTState < m_nLastTState))
	{
		nFrame = m_nLastFrame;
		nTState = m_nLastTState;
		m_nClamped++;
	}

	TTimedEvent Timed;
	Timed.Frame = nFrame;
	Timed.TState = nTState;
	Timed.Address = rEvent.Address;
	Timed.Kind = rEvent.Kind;
	Timed.Data = rEvent.Data;

	if (m_pLog->Push (Timed))
	{
		m_nLastFrame = nFrame;
		m_nLastTState = nTState;
	}
}

void CFrameClock::Clock (uint64_t nCount, double nTicks)
{
	(void) nTicks;

	if (!m_bHaveStart)
	{
		SetStart (nCount);
	}

	if (nCount < CTStateClock::CaptureLagClocks)
	{
		return;
	}

	// Most pairs complete nothing: compare with the count that completes
	// the next frame, found again when the frame start or length moves.
	uint64_t nStartNext = FrameStart (m_nCompleted + 1) + CTStateClock::CaptureLagClocks;
	if (m_bCompleteAtValid && nStartNext == m_nCompleteAt && nCount < m_nCompleteAt)
	{
		return;
	}
	m_nCompleteAt = nStartNext;
	m_bCompleteAtValid = true;
	if (nCount < m_nCompleteAt)
	{
		return;
	}

	uint32_t nFrame, nTState;
	Locate (nCount - CTStateClock::CaptureLagClocks, nFrame, nTState);

	// Frames before nFrame are complete, but never a frame that already
	// holds logged events at a later position.
	if ((int32_t) (nFrame - m_nLastFrame) < 0)
	{
		nFrame = m_nLastFrame;
	}

	if ((int32_t) (nFrame - m_nCompleted) > 0)
	{
		m_nCompleted = nFrame;
		m_pLog->Complete (m_nCompleted, m_Machine, m_bLocked);
	}
}

}
