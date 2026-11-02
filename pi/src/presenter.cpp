//
// presenter.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "presenter.h"

namespace sppie {

CFramePresenter::CFramePresenter (CEventLog *pLog)
:	m_pLog (pLog)
{
	Reset ();
}

void CFramePresenter::Reset (uint32_t nFirstFrame)
{
	m_State.Reset ();
	m_nNext = nFirstFrame;
	m_nLastFrame = nFirstFrame - 1;
	m_LastRecord.Frame = m_nLastFrame;
	m_LastRecord.Machine = Machine48K;
	m_LastRecord.Locked = 0;
	m_nPresented = 0;
	m_nSkipped = 0;
	m_nRepeated = 0;
}

bool CFramePresenter::Present (uint32_t *pPixels, unsigned nPitch)
{
	uint32_t nCompleted = m_pLog->Completed ();
	int32_t nWaiting = (int32_t) (nCompleted - m_nNext);

	if (nWaiting <= 0)
	{
		m_nRepeated++;
		return false;
	}

	if ((uint32_t) nWaiting >= SkipThreshold)
	{
		for (; nWaiting > 2; nWaiting--, m_nNext++)
		{
			TFrameRecord Record = m_pLog->Record (m_nNext);
			TFrameSpan Span = m_pLog->TakeFrame (m_nNext);
			ApplyFrame (m_State, Span, (TMachine) Record.Machine);
			m_pLog->Release (Span);
			m_nSkipped++;
		}
	}

	TFrameRecord Record = m_pLog->Record (m_nNext);
	TFrameSpan Span = m_pLog->TakeFrame (m_nNext);
	RenderTimedFrame (m_State, Span, (TMachine) Record.Machine, m_nNext, pPixels, nPitch);
	m_pLog->Release (Span);

	m_nLastFrame = m_nNext;
	m_LastRecord = Record;
	m_nNext++;
	m_nPresented++;

	return true;
}

}
