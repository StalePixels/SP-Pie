//
// sppieapp.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "sppieapp.h"
#include "intake.h"
#include "renderer.h"
#include "timebase.h"
#if SPPIE_OVERLAY
#include "overlay.h"
#endif
#include <circle/util.h>

namespace sppie {

// The intake core checks for the end of an SMI transfer only after half a
// transfer's time has passed since it started one. A transfer always lasts
// SmiBatchTransfers reads, so this costs nothing and keeps a stale DONE
// flag, read just after a start, from restarting a running transfer.
static const u64 TransferNs = (u64) SmiBatchTransfers
			      * (SmiSetup + SmiStrobe + SmiHold + SmiPace) * SmiCycleNs;

CSpPieApp::CSpPieApp (CMemorySystem *pMemorySystem, CBcmFrameBuffer *pFrameBuffer,
		      CSmiEventReader *pReader, unsigned nArmClockMHz, TMachineChoice Choice)
:	CMultiCoreSupport (pMemorySystem),
	m_pFrameBuffer (pFrameBuffer),
	m_pReader (pReader),
	m_nArmClockMHz (nArmClockMHz),
	m_Timeline (&m_Log, TickFrequency (), Choice),
	m_Presenter (&m_Log)
{
	memset (&m_Shared, 0, sizeof m_Shared);
}

void CSpPieApp::Run (unsigned nCore)
{
	switch (nCore)
	{
	case 0:
		RunRenderer ();
		break;

	case 1:
		RunIntake ();
		break;

	default:
		break;
	}
}

void CSpPieApp::Publish (u64 *pField, u64 nValue)
{
	__atomic_store_n (pField, nValue, __ATOMIC_RELAXED);
}

u64 CSpPieApp::Fetch (const u64 *pField)
{
	return __atomic_load_n (pField, __ATOMIC_RELAXED);
}

void CSpPieApp::RunIntake (void)
{
	const u64 nFrequency = TickFrequency ();
	const u64 nRestartGuardTicks = TransferNs / 2 * nFrequency / 1000000000;
	const volatile u32 *pRing = m_pReader->Ring ();

	TIntakeCounters Counters;
	memset (&Counters, 0, sizeof Counters);

	u64 nBusyTicks = 0;
	u64 nRestarts = 0;
	u64 nMaxGap = 0;
	u64 nMaxBacklog = 0;
	u64 nBadProducer = 0;

	unsigned nRead = m_pReader->Start ();
	u64 nTransferStart = ReadTicks ();
	u64 nLastNotDone = nTransferStart;
	m_Timeline.Start (nTransferStart);
	Publish (&m_Shared.Running, 1);

	while (1)
	{
		u64 nLoopStart = ReadTicks ();
		boolean bBusy = FALSE;

		// End of an SMI transfer: start the next one.
		if (nLoopStart - nTransferStart >= nRestartGuardTicks)
		{
			if (m_pReader->TransferDone ())
			{
				m_pReader->Restart ();
				nTransferStart = ReadTicks ();

				// Reads stopped some time after the last poll
				// that saw the transfer running.
				u64 nGap = nTransferStart - nLastNotDone;
				if (nGap > nMaxGap)
				{
					nMaxGap = nGap;
				}
				nRestarts++;
				bBusy = TRUE;
			}
			else
			{
				nLastNotDone = nLoopStart;
			}
		}
		else
		{
			nLastNotDone = nLoopStart;
		}

		// New ring words: stamp, decode and log them.
		unsigned nProducer = m_pReader->ProducerIndex ();
		u64 nSeen = ReadTicks ();
		if (nProducer == RingWords)
		{
			nBadProducer++;
		}
		else if (nProducer != nRead)
		{
			bBusy = TRUE;

			u64 nBacklog = (nProducer + RingWords - nRead) % RingWords;
			if (nBacklog > nMaxBacklog)
			{
				nMaxBacklog = nBacklog;
			}

			bool bMismatch = false;
			m_Timeline.BeginBatch (nSeen);
			while (nRead != nProducer && !bMismatch)
			{
				unsigned nEnd = nProducer > nRead ? nProducer : RingWords;
				m_pReader->InvalidateWords (nRead, nEnd);
				size_t nUsed = ProcessWords (pRing + nRead, nEnd - nRead,
							     m_Timeline, Counters, bMismatch);
				nRead = (unsigned) ((nRead + nUsed) % RingWords);
			}
			m_Timeline.EndBatch ();

			if (bMismatch)
			{
				nRead = m_pReader->Resync ();
				nTransferStart = ReadTicks ();
				nLastNotDone = nTransferStart;
			}
		}

		// Frames whose end has passed are complete.
		m_Timeline.Advance (ReadTicks ());

		if (bBusy)
		{
			nBusyTicks += ReadTicks () - nLoopStart;

			const CFrameLock &rLock = m_Timeline.Lock ();
			Publish (&m_Shared.FrameEvents, Counters.FrameEvents);
			Publish (&m_Shared.LogDropped, m_Log.Dropped ());
			Publish (&m_Shared.Clamped, m_Timeline.Clamped ());
			Publish (&m_Shared.Acquisitions, rLock.Acquisitions ());
			Publish (&m_Shared.PeriodNs, rLock.PeriodNs ());
			Publish (&m_Shared.Locked, rLock.Locked () ? 1 : 0);
			Publish (&m_Shared.Events, Counters.Events);
			Publish (&m_Shared.EmptyPairs, Counters.EmptyPairs);
			Publish (&m_Shared.Overflows, Counters.Overflows);
			Publish (&m_Shared.Mismatches, Counters.Mismatches);
			Publish (&m_Shared.Malformed, Counters.Malformed);
			Publish (&m_Shared.Resets, Counters.Resets);
			Publish (&m_Shared.BusyTicks, nBusyTicks);
			Publish (&m_Shared.Restarts, nRestarts);
			Publish (&m_Shared.MaxRestartGapTicks, nMaxGap);
			Publish (&m_Shared.MaxBacklogWords, nMaxBacklog);
		}
		Publish (&m_Shared.BadProducer, nBadProducer);
	}
}

void CSpPieApp::RunRenderer (void)
{
	// The whole display in RAM: the Spectrum picture in the middle, black
	// (or the overlay) at the sides.
	u32 *pDraw = new u32[DisplayWidth * DisplayHeight];
	for (unsigned i = 0; i < DisplayWidth * DisplayHeight; i++)
	{
		pDraw[i] = 0xFF000000u;
	}
	u32 *pPicture = pDraw + PictureTop * DisplayWidth + PictureLeft;

	uintptr nBuffer = (uintptr) m_pFrameBuffer->GetBuffer ();
	unsigned nPitchBytes = m_pFrameBuffer->GetPitch ();

	// Both halves of the frame buffer start black. The frame buffer is
	// device memory: aligned 64-bit stores, two pixels each.
	for (unsigned y = 0; y < 2 * DisplayHeight; y++)
	{
		volatile u64 *pDest = (volatile u64 *) (nBuffer + y * nPitchBytes);
		for (unsigned x = 0; x < DisplayWidth / 2; x++)
		{
			pDest[x] = 0xFF000000FF000000ull;
		}
	}

#if SPPIE_OVERLAY
	// The overlay changes the side areas, so whole rows are copied.
	const unsigned nCopyX = 0, nCopyWidth = DisplayWidth;
	const unsigned nCopyY = 0, nCopyHeight = DisplayHeight;

	COverlay Overlay;
	TFrameReport Report;
	memset (&Report, 0, sizeof Report);
	Report.ArmClockMHz = m_nArmClockMHz;

	const u64 nFrequency = TickFrequency ();
	u64 nPrevEvents = 0, nPrevEmpty = 0, nPrevFrameEvents = 0, nPrevBusy = 0;
	u64 nPrevFrameEnd = ReadTicks ();
#else
	// Only the picture changes.
	const unsigned nCopyX = PictureLeft, nCopyWidth = PictureWidth;
	const unsigned nCopyY = PictureTop, nCopyHeight = PictureHeight;
#endif

	u32 nFrame = 0;
	unsigned nHidden = 1;		// half 0 is shown after Initialize ()

	while (1)
	{
#if SPPIE_OVERLAY
		u64 nStart = ReadTicks ();
#endif

		// One complete Spectrum frame, or the last one again.
		m_Presenter.Present (pPicture, DisplayWidth);
#if SPPIE_OVERLAY
		Overlay.Draw (Report, pDraw, DisplayWidth);
#endif

		// Copy into the hidden half.
		for (unsigned y = nCopyY; y < nCopyY + nCopyHeight; y++)
		{
			volatile u64 *pDest = (volatile u64 *) (nBuffer
				+ (nHidden * DisplayHeight + y) * nPitchBytes + nCopyX * 4);
			const u64 *pSrc = (const u64 *) (pDraw + y * DisplayWidth + nCopyX);
			for (unsigned x = 0; x < nCopyWidth / 2; x++)
			{
				pDest[x] = pSrc[x];
			}
		}

#if SPPIE_OVERLAY
		u64 nDrawn = ReadTicks ();
#endif

		m_pFrameBuffer->SetVirtualOffset (0, nHidden * DisplayHeight);
		m_pFrameBuffer->WaitForVerticalSync ();

		nHidden ^= 1;
		nFrame++;

#if SPPIE_OVERLAY
		u64 nFrameEnd = ReadTicks ();
		u64 nEvents = Fetch (&m_Shared.Events);
		u64 nEmpty = Fetch (&m_Shared.EmptyPairs);
		u64 nFrameEvents = Fetch (&m_Shared.FrameEvents);
		u64 nBusy = Fetch (&m_Shared.BusyTicks);
		u64 nFrameTicks = nFrameEnd - nPrevFrameEnd;
		u64 nBusyDelta = nBusy - nPrevBusy;

		Report.Events = (unsigned) (nEvents - nPrevEvents);
		Report.EmptyPairs = (unsigned) (nEmpty - nPrevEmpty);
		Report.FrameEvents = (unsigned) (nFrameEvents - nPrevFrameEvents);
		Report.IntakeBusyUs = (unsigned) TicksToMicroseconds (nBusyDelta, nFrequency);
		Report.IntakeIdleUs = (unsigned) TicksToMicroseconds (
			nFrameTicks > nBusyDelta ? nFrameTicks - nBusyDelta : 0, nFrequency);
		Report.RenderUs = (unsigned) TicksToMicroseconds (nDrawn - nStart, nFrequency);
		Report.RenderIdleUs = (unsigned) TicksToMicroseconds (nFrameEnd - nDrawn, nFrequency);
		Report.Overflows = (unsigned) Fetch (&m_Shared.Overflows);
		Report.Mismatches = (unsigned) Fetch (&m_Shared.Mismatches);
		Report.Malformed = (unsigned) Fetch (&m_Shared.Malformed);
		Report.Resets = (unsigned) Fetch (&m_Shared.Resets);
		Report.MaxBacklogWords = (unsigned) Fetch (&m_Shared.MaxBacklogWords);
		Report.Restarts = (unsigned) Fetch (&m_Shared.Restarts);
		Report.MaxRestartGapUs = (unsigned) TicksToMicroseconds (
			Fetch (&m_Shared.MaxRestartGapTicks), nFrequency);
		Report.BadProducer = (unsigned) Fetch (&m_Shared.BadProducer);
		Report.LogDropped = (unsigned) Fetch (&m_Shared.LogDropped);
		Report.Clamped = (unsigned) Fetch (&m_Shared.Clamped);
		Report.Acquisitions = (unsigned) Fetch (&m_Shared.Acquisitions);
		Report.PeriodNs = (unsigned) Fetch (&m_Shared.PeriodNs);
		Report.Locked = Fetch (&m_Shared.Locked) != 0;
		Report.Skipped = m_Presenter.Skipped ();
		Report.Repeated = m_Presenter.Repeated ();
		Report.SpectrumFrame = m_Presenter.LastFrame ();
		Report.Machine = m_Presenter.LastRecord ().Machine;
		Report.Frames = nFrame;
		Report.NotEmpty = m_pReader->NotEmpty ();

		nPrevEvents = nEvents;
		nPrevEmpty = nEmpty;
		nPrevFrameEvents = nFrameEvents;
		nPrevBusy = nBusy;
		nPrevFrameEnd = nFrameEnd;
#endif
	}
}

}
