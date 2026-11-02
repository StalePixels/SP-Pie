//
// timeline.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "timeline.h"

namespace sppie {

CTimeline::CTimeline (CEventLog *pLog, uint64_t nTickFrequency, TMachineChoice Choice)
:	m_pLog (pLog),
	m_Lock (nTickFrequency, Choice),
	m_Clock (nTickFrequency),
	m_nMarginTicks ((uint64_t) SeenMarginNs * nTickFrequency / 1000000000)
{
	Start (0);
}

void CTimeline::Start (uint64_t nNow)
{
	m_Lock.Start (nNow);
	m_Clock.Reset ();

	m_nFirstWord = 0;
	m_nSeen = nNow;
	m_nLastFrame = 0;
	m_nLastTState = 0;
	m_nCompleted = 0;
	m_nNextCheck = 0;
	m_nLockVersion = m_Lock.Version ();
	m_nClamped = 0;
}

void CTimeline::Locate (uint64_t nTicks, uint32_t &rFrame, uint32_t &rTState)
{
	m_Lock.Position (nTicks, rFrame, rTState);

	if ((int32_t) (rFrame - m_nCompleted) < 0)
	{
		rFrame = m_nCompleted;
		rTState = 0;
	}

	int32_t nFrameDiff = (int32_t) (rFrame - m_nLastFrame);
	if (nFrameDiff < 0 || (nFrameDiff == 0 && rTState < m_nLastTState))
	{
		rFrame = m_nLastFrame;
		rTState = m_nLastTState;
		m_nClamped++;
	}
}

void CTimeline::StartTransfer (uint64_t nWord, uint64_t nTicks)
{
	m_Clock.StartTransfer (nWord, nTicks);
}

void CTimeline::BeginBatch (uint64_t nFirstWord, uint64_t nSeen)
{
	m_nFirstWord = nFirstWord;
	m_nSeen = nSeen;
}

void CTimeline::Event (const TEvent &rEvent, size_t nWord)
{
	uint64_t nStamp = m_Clock.Stamp (m_nFirstWord + nWord, m_nSeen);

	if (rEvent.Kind == KindIo && (rEvent.Address & 0xF002) == 0x1000)
	{
		m_Lock.Port1FFDWrite ();
	}

	// The lock first, so that an F event lands just after the frame start
	// it may have moved.
	if (rEvent.Frame)
	{
		m_Lock.FrameEvent (nStamp);
	}

	uint32_t nFrame, nTState;
	Locate (nStamp, nFrame, nTState);

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

void CTimeline::EndBatch (void)
{
	m_pLog->Publish ();
}

void CTimeline::Advance (uint64_t nSeen)
{
	if (nSeen < m_nMarginTicks)
	{
		return;
	}
	uint64_t nNow = nSeen - m_nMarginTicks;

	if (m_Lock.Version () != m_nLockVersion)
	{
		m_nLockVersion = m_Lock.Version ();
		m_nNextCheck = 0;
	}

	if (nNow < m_nNextCheck)
	{
		return;
	}

	m_Lock.Rebase (nNow);

	uint32_t nFrame, nTState;
	m_Lock.Position (nNow, nFrame, nTState);

	// Frames before nFrame are complete, but never a frame that already
	// holds logged events at a later position.
	if ((int32_t) (nFrame - m_nLastFrame) < 0)
	{
		nFrame = m_nLastFrame;
	}

	if ((int32_t) (nFrame - m_nCompleted) > 0)
	{
		m_nCompleted = nFrame;
		m_pLog->Complete (m_nCompleted, m_Lock.Machine (), m_Lock.Locked ());
	}

	m_nNextCheck = m_Lock.FrameStart (nFrame + 1);
}

}
