//
// timeline.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "timeline.h"

namespace sppie {

CTimeline::CTimeline (CEventLog *pLog, uint64_t nTickFrequency, TMachineChoice Choice)
:	m_pLog (pLog),
	m_Lock (nTickFrequency, Choice)
{
	Start (0);
}

void CTimeline::Start (uint64_t nNow)
{
	m_Lock.Start (nNow);

	m_nStamp = nNow;
	m_bPositioned = false;
	m_nFrame = 0;
	m_nTState = 0;
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

void CTimeline::BeginBatch (uint64_t nStamp)
{
	m_nStamp = nStamp;
	m_bPositioned = false;
}

void CTimeline::Event (const TEvent &rEvent)
{
	if (rEvent.Frame)
	{
		m_Lock.FrameEvent (m_nStamp);
	}

	// Positioned once per batch, after any F event of the batch has moved
	// the line, so that the F event itself lands just after its frame start.
	if (!m_bPositioned)
	{
		Locate (m_nStamp, m_nFrame, m_nTState);
		m_bPositioned = true;
	}

	TTimedEvent Timed;
	Timed.Frame = m_nFrame;
	Timed.TState = m_nTState;
	Timed.Address = rEvent.Address;
	Timed.Kind = rEvent.Kind;
	Timed.Data = rEvent.Data;

	if (m_pLog->Push (Timed))
	{
		m_nLastFrame = m_nFrame;
		m_nLastTState = m_nTState;
	}
}

void CTimeline::EndBatch (void)
{
	m_pLog->Publish ();
}

void CTimeline::Advance (uint64_t nNow)
{
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
