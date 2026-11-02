//
// presenter.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "presenter.h"
#include "event.h"
#include "../fuseula/fuseula.h"

namespace sppie {

CFramePresenter::CFramePresenter (CEventLog *pLog)
:	m_pLog (pLog)
{
	Reset ();
}

bool CFramePresenter::Initialize (uint32_t *pPicture, unsigned nPitch)
{
	return fuseula_init (pPicture, nPitch) == 0;
}

void CFramePresenter::Reset (uint32_t nFirstFrame)
{
	m_nNext = nFirstFrame;
	m_nLastFrame = nFirstFrame - 1;
	m_LastRecord.Frame = m_nLastFrame;
	m_LastRecord.Machine = Machine48K;
	m_LastRecord.Locked = 0;
	m_nPresented = 0;
	m_nSkipped = 0;
	m_nRepeated = 0;
}

void CFramePresenter::RunFrame (uint32_t nFrame)
{
	TFrameRecord Record = m_pLog->Record (nFrame);
	fuseula_select ((fuseula_machine) Record.Machine);

	TFrameSpan Span = m_pLog->TakeFrame (nFrame);
	for (uint32_t i = 0; i < Span.nCount; i++)
	{
		const TTimedEvent &rEvent = Span.At (i);
		switch (rEvent.Kind)
		{
		case KindMemory:
			fuseula_memory_write (rEvent.TState, rEvent.Address, rEvent.Data);
			break;

		case KindIo:
			fuseula_io_write (rEvent.TState, rEvent.Address, rEvent.Data);
			break;

		case KindReset:
			fuseula_reset (rEvent.TState);
			break;

		default:
			break;
		}
	}
	m_pLog->Release (Span);

	fuseula_end_frame ();

	m_nLastFrame = nFrame;
	m_LastRecord = Record;
}

bool CFramePresenter::Present (void)
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
			RunFrame (m_nNext);
			m_nSkipped++;
		}
	}

	RunFrame (m_nNext);
	m_nNext++;
	m_nPresented++;

	return true;
}

}
