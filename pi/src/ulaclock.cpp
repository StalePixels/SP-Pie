//
// ulaclock.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "ulaclock.h"
#include "frameclock.h"

namespace sppie {

// Frame lengths and Z80 clocks of the two classes (libspectrum timings.c
// and the World of Spectrum FAQ): the frame period decides the class when
// the count cannot.
static const double Clock48KHz		= 3500000.0;
static const double Clock128KHz		= 3546900.0;
static const uint32_t Frame48K		= 69888;
static const uint32_t Frame128K		= 70908;

// How far the phase can move between two pairs, per ULA T-state elapsed:
// the error of the rate. The rate comes from RateFrames frame periods,
// each known to the acknowledge delay (0 to 3 T-states in HALT), so it is
// good to a few parts per million; the hold sets the phase again in every
// frame.
static const double DriftPerTState	= 5e-6;

// How far it can move at a restart of the reads: the transfer start is
// timed with the Pi's timer, one tick (52 ns at 19.2 MHz) is 0.19 T-states.
static const double RestartWiden	= 0.2;

// T-states after an F event with no contention: the first display line
// starts 14,336 T-states after the interrupt on the 48K and about 14,361 on
// the 128K (Fuse, libspectrum timings.c); the F event comes up to about 40
// T-states late and is seen a few reads later.
static const double HoldTStates		= 14000.0;

CUlaClock::CUlaClock (ITimedEventSink *pSink, CFrameClock *pFrames, uint64_t nTickFrequency)
:	m_pSink (pSink),
	m_pFrames (pFrames),
	m_nTickFrequency ((double) nTickFrequency)
{
	Reset ();
}

void CUlaClock::Reset (void)
{
	m_bStopped = false;
	m_nStoppedVotes = 0;
	m_nFreeVotes = 0;

	m_nRate = Clock128KHz / m_nTickFrequency;
	m_nTicksRef = 0;
	m_nPhase = 0;
	m_nLow = m_nHigh = 0;
	m_nLastTicks = 0;
	m_bHavePhase = false;
	m_nStall = 0;
	m_nSlips = 0;

	m_bHaveFrameEvent = false;
	m_nLastFrameCount = 0;
	m_nFrameTicks = 0;
	m_nFrameTicksNext = 0;
	m_bPendingFrame = false;

	m_nLastClocks = 0;
	m_nClocks = 0;
	m_nClocksNext = 0;

	m_nPending = 0;
	m_nPendingFrameCount = 0;
	m_bRestarted = false;
	m_nPrevStall = 0;
	m_nLastEvent = 0;
	m_nHoldUntil = 0;

	m_nAmbiguous = 0;
}

uint32_t CUlaClock::MinFrameClocks (void) const
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

uint32_t CUlaClock::MaxFrameClocks (void) const
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

void CUlaClock::FrameEvent (uint64_t nCount, double nTicks)
{
	// The rate: the frame period from the read times of the F events, and
	// the class whose clock it fits.
	if (m_nFrameTicks != 0)
	{
		const double nLast = m_FrameTicks[(m_nFrameTicksNext + RateFrames - 1) % RateFrames];
		if (nTicks <= nLast)
		{
			m_nFrameTicks = 0;		// the timer went back: start again
		}
	}
	m_FrameTicks[m_nFrameTicksNext] = nTicks;
	m_nFrameTicksNext = (m_nFrameTicksNext + 1) % RateFrames;
	if (m_nFrameTicks < RateFrames)
	{
		m_nFrameTicks++;
	}
	if (m_nFrameTicks >= 4)
	{
		double nFirst = m_FrameTicks[(m_nFrameTicksNext + RateFrames - m_nFrameTicks) % RateFrames];
		double nSpan = nTicks - nFirst;
		double nNominal = Frame128K / Clock128KHz * m_nTickFrequency;
		double nFrames = (double) (int64_t) (nSpan / nNominal + 0.5);
		if (nFrames >= 1)
		{
			double nPeriod = nSpan / nFrames;	// ticks per frame
			double n48K = Frame48K / Clock48KHz * m_nTickFrequency;
			double n128K = Frame128K / Clock128KHz * m_nTickFrequency;
			double nL = (nPeriod - n48K) * (nPeriod - n48K) < (nPeriod - n128K) * (nPeriod - n128K)
				    ? Frame48K : Frame128K;
			m_nRate = nL / nPeriod;
		}
	}

	// From the interrupt to the first display line the ULA holds nothing.
	m_nHoldUntil = nTicks + HoldTStates / m_nRate;

	// Clock behaviour, from the count per frame.
	uint32_t nL = m_pFrames->FrameTStates ();
	if (m_bHaveFrameEvent && nCount > m_nLastFrameCount)
	{
		uint64_t nClocks = nCount - m_nLastFrameCount;
		if (nClocks > nL / 2 && nClocks < (uint64_t) nL * 3 / 2)
		{
			m_nLastClocks = (uint32_t) nClocks;
			m_Clocks[m_nClocksNext] = m_nLastClocks;
			m_nClocksNext = (m_nClocksNext + 1) % StatsFrames;
			if (m_nClocks < StatsFrames)
			{
				m_nClocks++;
			}

			if (nClocks + MaxAckDelayTStates < nL)
			{
				m_nFreeVotes = 0;
				if (++m_nStoppedVotes >= StoppedFrames && !m_bStopped)
				{
					m_bStopped = true;
					m_bHavePhase = false;
					m_pFrames->EnableContentionCheck (false);
				}
			}
			else if (nClocks < nL + MaxAckDelayTStates)
			{
				m_nStoppedVotes = 0;
				if (++m_nFreeVotes >= FreeFrames && m_bStopped)
				{
					Flush (m_nStall);
					m_nPrevStall = m_nStall;
					m_bStopped = false;
					m_pFrames->EnableContentionCheck (true);
				}
			}
		}
	}
	m_nLastFrameCount = nCount;
	m_bHaveFrameEvent = true;
}

void CUlaClock::TimedEvent (const TEvent &rEvent, uint64_t nCount)
{
	if (rEvent.Frame && !rEvent.Overflow)
	{
		// Its read time is that of the next empty pair, within a few reads.
		m_bPendingFrame = true;
		m_nPendingFrameCount = nCount;
	}

	if (!m_bStopped)
	{
		uint64_t u = nCount + m_nStall;
		m_nLastEvent = u;
		m_pSink->TimedEvent (rEvent, u);
		return;
	}

	if (m_nPending == MaxPending)
	{
		Flush (m_nStall);
	}
	m_Pending[m_nPending].Event = rEvent;
	m_Pending[m_nPending].Count = nCount;
	m_nPending++;
}

void CUlaClock::ReadsRestarted (void)
{
	m_bRestarted = true;
}

void CUlaClock::Lock (uint64_t nCount, double nTicks)
{
	// The ULA time at nTicks is between nCount + stall and one T-state
	// more: start in the middle.
	m_nTicksRef = nTicks;
	m_nPhase = (double) (nCount + m_nStall) + 0.5;
	m_nLow = -0.5;
	m_nHigh = 0.5;
	m_bHavePhase = true;
}

void CUlaClock::Clock (uint64_t nCount, double nTicks)
{
	if (m_bPendingFrame)
	{
		m_bPendingFrame = false;
		FrameEvent (m_nPendingFrameCount, nTicks);
	}

	bool bDecided = true;

	if (m_bStopped && m_nFrameTicks >= LockFrames)
	{
		if (!m_bHavePhase)
		{
			Lock (nCount, nTicks);
		}
		else
		{
			double nElapsed = (nTicks - m_nLastTicks) * m_nRate;
			double nWiden = (nElapsed < 0 ? -nElapsed : nElapsed) * DriftPerTState;
			if (m_bRestarted)
			{
				nWiden += RestartWiden;
			}
			m_nLow -= nWiden;
			m_nHigh += nWiden;

			// The ULA time is u - e with e in [m_nLow, m_nHigh]. A stall s
			// fits the pair when c + s <= u - e < c + s + 1 for some e
			// there: s in (v - 1 - high, v - low].
			double v = m_nPhase + m_nRate * (nTicks - m_nTicksRef) - (double) nCount;
			double nFrom = v - 1.0 - m_nHigh;
			int64_t nMin = (int64_t) nFrom;
			if ((double) nMin <= nFrom)
			{
				nMin++;
			}
			int64_t nMax = (int64_t) (v - m_nLow);
			if ((double) nMax > v - m_nLow)
			{
				nMax--;
			}
			int64_t nStall = (int64_t) m_nStall;

			if (nTicks < m_nHoldUntil)
			{
				// Top border: no contention, so the stall is the same and
				// the pair only narrows the phase.
				double nLowC = v - (double) nStall - 1.0;
				double nHighC = v - (double) nStall;
				if (nLowC > m_nHigh || nHighC < m_nLow)
				{
					m_nLow = nLowC;
					m_nHigh = nHighC;
				}
				else
				{
					m_nLow = nLowC > m_nLow ? nLowC : m_nLow;
					m_nHigh = nHighC < m_nHigh ? nHighC : m_nHigh;
				}
			}
			else if (nMax < nStall)
			{
				// Fits no stall this or larger: the phase was wrong.
				m_nSlips++;
				m_nLow = v - (double) nStall - 1.0;
				m_nHigh = v - (double) nStall;
			}
			else
			{
				if (nMin < nStall)
				{
					nMin = nStall;
				}
				if (nMin == nMax)
				{
					// One stall fits: take it, and narrow the phase.
					m_nStall = (uint64_t) nMin;
					double nLowC = v - (double) nMin - 1.0;
					double nHighC = v - (double) nMin;
					m_nLow = nLowC > m_nLow ? nLowC : m_nLow;
					m_nHigh = nHighC < m_nHigh ? nHighC : m_nHigh;
				}
				else
				{
					// The pair lies at an edge: it decides nothing.
					bDecided = false;
				}
			}

			// Keep the interval round zero.
			double nMid = (m_nLow + m_nHigh) / 2;
			m_nPhase -= nMid;
			m_nLow -= nMid;
			m_nHigh -= nMid;
		}
		m_nLastTicks = nTicks;
	}
	m_bRestarted = false;

	if (bDecided)
	{
		Flush (m_nStall);
		m_nPrevStall = m_nStall;
	}

	m_pSink->Clock (nCount + m_nPrevStall, nTicks);
}

uint64_t CUlaClock::Place (const TEvent &rEvent, uint64_t nCount, uint64_t nStallBefore,
			   uint64_t nStallAfter)
{
	if (nStallBefore >= nStallAfter)
	{
		return nCount + nStallAfter;
	}

	if (rEvent.Kind == KindIo)
	{
		return nCount + nStallBefore;
	}

	if (rEvent.Kind == KindMemory && rEvent.Address >= 0x4000 && rEvent.Address < 0x8000)
	{
		// The candidate where Fuse's contention table lets a contended
		// write end, preferring a cycle that waited; the earliest such, as
		// a later write between the pairs adds its own wait after this one.
		uint64_t nFit = 0, nWaited = 0;
		bool bFit = false, bWaited = false;
		for (uint64_t s = nStallBefore; s <= nStallAfter; s++)
		{
			uint64_t u = nCount + s;
			int nAt = m_pFrames->ContentionAt (u - 3);
			if (nAt < 0)
			{
				break;
			}
			if (nAt == 0)
			{
				if (!bFit)
				{
					bFit = true;
					nFit = u;
				}
				if (!bWaited && m_pFrames->ContentionAt (u - 4) > 0)
				{
					bWaited = true;
					nWaited = u;
				}
			}
		}
		if (bWaited)
		{
			return nWaited;
		}
		if (bFit)
		{
			return nFit;
		}
	}

	m_nAmbiguous++;
	return nCount + nStallAfter;
}

void CUlaClock::Flush (uint64_t nStallAfter)
{
	for (unsigned i = 0; i < m_nPending; i++)
	{
		uint64_t u = Place (m_Pending[i].Event, m_Pending[i].Count, m_nPrevStall, nStallAfter);
		if (u < m_nLastEvent)
		{
			u = m_nLastEvent;
		}
		m_nLastEvent = u;
		m_pSink->TimedEvent (m_Pending[i].Event, u);
	}
	m_nPending = 0;
}

}
