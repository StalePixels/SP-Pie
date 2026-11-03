//
// presenter.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "presenter.h"
#include "event.h"
#include "../fuseula/fuseula.h"
#include "../fuseula/fusesound.h"
#include "picture.h"
#include <string.h>

namespace sppie {

static_assert (CControlPorts::DeviceAy == FUSESOUND_AY
	       && CControlPorts::DeviceTurboSound == FUSESOUND_TURBOSOUND
	       && CControlPorts::DeviceBeeper == FUSESOUND_BEEPER
	       && CControlPorts::DeviceCovoxFB == FUSESOUND_COVOX_FB
	       && CControlPorts::DeviceCovoxDD == FUSESOUND_COVOX_DD
	       && CControlPorts::DeviceCovoxDF == FUSESOUND_COVOX_DF
	       && CControlPorts::DevicesDefault == FUSESOUND_DEFAULT,
	       "the device bits of the control port are the sound layer's");

static const unsigned PicturePixels = PictureWidth * PictureHeight;

CFramePresenter::CFramePresenter (CEventLog *pLog, CAudioFifo *pAudio)
:	m_pLog (pLog),
	m_pAudio (pAudio),
	m_pOutput (0),
	m_nOutputPitch (0),
	m_pFuse (0),
	m_pPrevious (0),
	m_nMixing (-1)
{
	Reset ();
}

void CFramePresenter::SetDefaults (const CControlPorts::TDefaults &rDefaults)
{
	m_Control.SetDefaults (rDefaults);
	m_Control.Reset ();
}

bool CFramePresenter::Initialize (uint32_t *pPicture, unsigned nPitch)
{
	if (m_pFuse == 0)
	{
		m_pFuse = new uint32_t[PicturePixels];
		m_pPrevious = new uint32_t[PicturePixels];
		if (m_pFuse == 0 || m_pPrevious == 0)
		{
			return false;
		}
	}
	for (unsigned i = 0; i < PicturePixels; i++)
	{
		m_pFuse[i] = 0xFF000000u;
		m_pPrevious[i] = 0xFF000000u;
	}
	m_pOutput = pPicture;
	m_nOutputPitch = nPitch;

	if (fuseula_init (m_pFuse, PictureWidth) != 0 || fusesound_init () != 0)
	{
		return false;
	}

	m_Control.Reset ();
	fusesound_devices (0, m_Control.Devices ());
	m_Interlace.Reset ();
	m_nMixing = -1;

	return true;
}

const uint32_t *CFramePresenter::WeavePicture (void) const
{
	return m_nMixing == CControlPorts::DisplayModeWeave ? m_pPrevious : 0;
}

void CFramePresenter::WritePicture (void)
{
	uint8_t nDisplay = m_Control.Display ();
	bool bMix = (nDisplay & CControlPorts::DisplayInterlaceOn)
		    || ((nDisplay & CControlPorts::DisplayInterlaceAuto) && m_Interlace.On ());
	m_nMixing = bMix ? nDisplay & CControlPorts::DisplayModeMask : -1;

	for (unsigned y = 0; y < PictureHeight; y++)
	{
		const uint32_t *pNew = m_pFuse + y * PictureWidth;
		const uint32_t *pOld = m_pPrevious + y * PictureWidth;
		uint32_t *pOut = m_pOutput + y * m_nOutputPitch;

		switch (m_nMixing)
		{
		case CControlPorts::DisplayModeAverage:
			for (unsigned x = 0; x < PictureWidth; x++)
			{
				pOut[x] = MixPixels (pNew[x], pOld[x]);
			}
			break;

		case CControlPorts::DisplayModeCrt:
			for (unsigned x = 0; x < PictureWidth; x++)
			{
				pOut[x] = MixPixelsCrt (pNew[x], pOld[x]);
			}
			break;

		default:	// not mixing, or the line weave
			memcpy (pOut, pNew, PictureWidth * sizeof *pOut);
			break;
		}
	}
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
	// Fuse draws over its picture; keep the frame before for mixing.
	if (m_pFuse != 0)
	{
		memcpy (m_pPrevious, m_pFuse, PicturePixels * sizeof *m_pFuse);
	}

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
			if (rEvent.Address >= 0x4000 && rEvent.Address < 0x5B00)
			{
				m_bScreenWritten = true;
			}
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
	if (m_pFuse != 0)
	{
		m_Interlace.Frame (PictureHash (m_pFuse, PictureWidth, PictureWidth, PictureHeight));
	}

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
		if (m_pOutput != 0)
		{
			WritePicture ();
		}
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
	if (m_pOutput != 0)
	{
		WritePicture ();
	}

	return true;
}

}
