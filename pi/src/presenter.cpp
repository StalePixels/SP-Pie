//
// presenter.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "presenter.h"
#include "event.h"
#include "../fuseula/fuseula.h"
#include "../fuseula/fusesound.h"

namespace sppie {

static_assert (CControlPorts::DeviceAy == FUSESOUND_AY
	       && CControlPorts::DeviceTurboSound == FUSESOUND_TURBOSOUND
	       && CControlPorts::DeviceBeeper == FUSESOUND_BEEPER
	       && CControlPorts::DeviceCovoxFB == FUSESOUND_COVOX_FB
	       && CControlPorts::DeviceCovoxDD == FUSESOUND_COVOX_DD
	       && CControlPorts::DeviceCovoxDF == FUSESOUND_COVOX_DF
	       && CControlPorts::DevicesDefault == FUSESOUND_DEFAULT,
	       "the device bits of the control port are the sound layer's");

CFramePresenter::CFramePresenter (CEventLog *pLog, CAudioFifo *pAudio)
:	m_pLog (pLog),
	m_pAudio (pAudio)
{
	Reset ();
}

bool CFramePresenter::Initialize (uint32_t *pPicture, unsigned nPitch)
{
	if (fuseula_init (pPicture, nPitch) != 0 || fusesound_init () != 0)
	{
		return false;
	}

	m_Control.Reset ();
	fusesound_devices (0, m_Control.Devices ());

	return true;
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
	fusesound_begin_frame ((fuseula_machine) Record.Machine);

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
			if (m_Control.Write (rEvent.Address, rEvent.Data))
			{
				fusesound_devices (rEvent.TState, m_Control.Devices ());
			}
			fuseula_io_write (rEvent.TState, rEvent.Address, rEvent.Data);
			fusesound_io_write (rEvent.TState, rEvent.Address, rEvent.Data);
			break;

		case KindReset:
			fuseula_reset (rEvent.TState);
			m_Control.Reset ();
			fusesound_devices (rEvent.TState, m_Control.Devices ());
			fusesound_reset (rEvent.TState);
			break;

		default:
			break;
		}
	}
	m_pLog->Release (Span);

	fuseula_end_frame ();

	const int32_t *pSamples;
	unsigned nSamples = fusesound_end_frame (&pSamples);
	if (m_pAudio != 0)
	{
		m_pAudio->Write (pSamples, nSamples);
	}

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
