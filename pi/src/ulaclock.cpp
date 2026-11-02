//
// ulaclock.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "ulaclock.h"
#include "frameclock.h"

namespace sppie {

// Frame lengths and Z80 clocks of the two classes (libspectrum timings.c
// and the World of Spectrum FAQ).
static const double Clock48KHz		= 3500000.0;
static const double Clock128KHz		= 3546900.0;
static const uint32_t Frame48K		= 69888;
static const uint32_t Frame128K		= 70908;

CUlaClock::CUlaClock (ITimedEventSink *pSink, CFrameClock *pFrames, uint64_t nTickFrequency)
:	m_pSink (pSink),
	m_pFrames (pFrames),
	m_nTickFrequency ((double) nTickFrequency)
{
	Reset ();
}

void CUlaClock::Reset (void)
{
	m_bStoppedSinceReset = false;
	m_bHaveClass = false;
	m_Class = Machine128K;

	m_bPendingFrame = false;
	m_nPendingFrameCount = 0;
	m_bHaveFrameEvent = false;
	m_nLastFrameCount = 0;
	m_nFrameTicks = 0;
	m_nFrameTicksNext = 0;

	m_nLastClocks = 0;
	m_nClocks = 0;
	m_nClocksNext = 0;
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

void CUlaClock::ClockStopped (void)
{
	if (!m_bStoppedSinceReset)
	{
		m_bStoppedSinceReset = true;
		m_pFrames->SetClockFacts (m_Class, false);
	}
}

void CUlaClock::FrameEvent (uint64_t nCount, double nTicks)
{
	// The class, from the frame period over the last RateFrames F events.
	if (m_nFrameTicks != 0)
	{
		double nLast = m_FrameTicks[(m_nFrameTicksNext + RateFrames - 1) % RateFrames];
		if (nTicks <= nLast)
		{
			m_nFrameTicks = 0;
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
		double nSpan = (nTicks - nFirst) / m_nTickFrequency;		// seconds
		double n48K = Frame48K / Clock48KHz, n128K = Frame128K / Clock128KHz;
		double nFrames = (double) (int64_t) (nSpan / n128K + 0.5);
		if (nFrames >= 1)
		{
			double nPeriod = nSpan / nFrames;
			TMachine Class =   (nPeriod - n48K) * (nPeriod - n48K)
					 < (nPeriod - n128K) * (nPeriod - n128K) ? Machine48K : Machine128K;
			if (!m_bHaveClass || Class != m_Class)
			{
				m_Class = Class;
				m_bHaveClass = true;
				m_pFrames->SetClockFacts (m_Class, !m_bStoppedSinceReset);
			}
		}
	}

	// Clocks per frame, and a short frame.
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
				ClockStopped ();
			}
		}
	}
	m_nLastFrameCount = nCount;
	m_bHaveFrameEvent = true;
}

void CUlaClock::TimedEvent (const TEvent &rEvent, uint64_t nCount)
{
	if (rEvent.Kind == KindReset && m_bStoppedSinceReset)
	{
		// The board may sit on another Spectrum now.
		m_bStoppedSinceReset = false;
		m_pFrames->SetClockFacts (m_Class, true);
	}

	if (rEvent.Frame && !rEvent.Overflow)
	{
		// Its read time is that of the next empty pair, within a few reads.
		m_bPendingFrame = true;
		m_nPendingFrameCount = nCount;
	}

	m_pSink->TimedEvent (rEvent, nCount);
}

void CUlaClock::Clock (uint64_t nCount, double nTicks, bool bStall)
{
	if (bStall)
	{
		ClockStopped ();
	}
	if (m_bPendingFrame)
	{
		m_bPendingFrame = false;
		FrameEvent (m_nPendingFrameCount, nTicks);
	}
	m_pSink->Clock (nCount, nTicks, bStall);
}

}
