//
// tstateclock.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "tstateclock.h"

namespace sppie {

CTStateClock::CTStateClock (ITimedEventSink *pSink, uint64_t nTickFrequency, uint32_t nClockHz)
:	m_pSink (pSink),
	m_Clock (nTickFrequency),
	m_nTickFrequency (nTickFrequency)
{
	SetClockHz (nClockHz);
	Reset ();
}

void CTStateClock::Reset (void)
{
	m_Clock.Reset ();
	m_nSeen = 0;
	m_bHaveCount = false;
	m_nCount = CountBase;
	m_nStamp = 0;
	m_nPrevCount = CountBase;
	m_nLastEvent = 0;
	m_nPending = 0;
	m_nAmbiguous = 0;
}

void CTStateClock::SetClockHz (uint32_t nClockHz)
{
	m_nRateQ32 = m_nTickFrequency != 0 ? ((uint64_t) nClockHz << 32) / m_nTickFrequency : 0;
}

void CTStateClock::StartTransfer (uint64_t nWord, uint64_t nTicks)
{
	ResolveForward ();
	m_Clock.StartTransfer (nWord, nTicks);
	m_pSink->ReadsRestarted ();
}

void CTStateClock::Empty (uint8_t nCount, bool bStall, uint64_t nWord)
{
	// The CPLD loaded the pair's R0, and took its count, at the end of the
	// read before it. For the first pair of a transfer that read is the
	// last one before the stop.
	uint64_t nLoadWord = nWord != 0 ? nWord - 1 : 0;
	uint64_t nStamp = m_Clock.Stamp (nLoadWord, m_nSeen);
	double nLoadTicks = m_Clock.ReadTime (nLoadWord);

	if (!m_bHaveCount)
	{
		m_nCount = CountBase + nCount;
		m_nPrevCount = m_nCount;
		m_nStamp = nStamp;
		m_bHaveCount = true;
	}
	else
	{
		uint64_t nElapsed = nStamp - m_nStamp;
		uint64_t nPredicted = (uint64_t) (((unsigned __int128) nElapsed * m_nRateQ32) >> 32);
		uint64_t nStep = (nCount - m_nCount) & CountMask;
		if (nPredicted > nStep)
		{
			nStep += ((nPredicted - nStep + CountModulus / 2) / CountModulus) * CountModulus;
		}

		m_nPrevCount = m_nCount;
		m_nCount += nStep;
		m_nStamp = nStamp;
	}

	ResolveBackward ();

	m_pSink->Clock (m_nCount, nLoadTicks, bStall);
}

void CTStateClock::Event (const TEvent &rEvent, uint64_t nWord)
{
	(void) nWord;

	if (m_nPending == MaxPending)
	{
		ResolveForward ();
	}
	m_Pending[m_nPending++] = rEvent;
}

// Every waiting event against the empty pair just read: the latest count at
// or below it with the event's 5 bits.
void CTStateClock::ResolveBackward (void)
{
	// The events lie between the previous empty pair (less the capture
	// lag) and this one. When that span is 32 clocks or more, the 5 bits
	// do not fix the count.
	bool bAmbiguous = m_nCount - m_nPrevCount + CaptureLagClocks >= CountModulus;

	for (unsigned i = 0; i < m_nPending; i++)
	{
		uint64_t nEvent = m_nCount - ((m_nCount - m_Pending[i].Count) & CountMask);
		if (bAmbiguous)
		{
			m_nAmbiguous++;
		}
		Deliver (m_Pending[i], nEvent);
	}
	m_nPending = 0;
}

// Every waiting event against the empty pair before it: the earliest count
// at or after that pair's count, less the capture lag, with the event's 5
// bits. Events come in capture order, so each one is at or after the one
// before.
void CTStateClock::ResolveForward (void)
{
	// Before the first empty pair there is no count to place them by.
	if (!m_bHaveCount)
	{
		m_nPending = 0;
		return;
	}

	uint64_t nFrom = m_nCount - CaptureLagClocks;
	for (unsigned i = 0; i < m_nPending; i++)
	{
		uint64_t nEvent = nFrom + ((m_Pending[i].Count - nFrom) & CountMask);
		Deliver (m_Pending[i], nEvent);
		nFrom = nEvent;
	}
	m_nPending = 0;
}

void CTStateClock::Deliver (const TEvent &rEvent, uint64_t nCount)
{
	// Writes come in time order. A count before the last one can only come
	// from an event placed with the wrong number of wraps.
	if (nCount < m_nLastEvent)
	{
		nCount = m_nLastEvent;
		m_nAmbiguous++;
	}
	m_nLastEvent = nCount;

	m_pSink->TimedEvent (rEvent, nCount);
}

}
