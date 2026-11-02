//
// sppieapp.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "sppieapp.h"
#include "intake.h"
#include "picture.h"
#include "timebase.h"
#include "../fuseula/fuseula.h"
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

// Every read stop longer than 32 clocks (9 us) can cost an event its exact
// count, and the CPLD queue holds only four events. So from this long
// before a transfer is due to end, the intake core waits for its end and
// starts the next one at once, and it never handles more than
// BatchWords words between two looks at the transfer.
static const u64 RestartLeadNs = 40000;
static const unsigned BatchWords = 256;

// The shared counters are published at most this often.
static const u64 PublishNs = 1000000;

// The clock rate the intake core uses to count wraps of the 5-bit clock
// count across a stop in the reads: the 128K-class Z80 clock. The 48K
// clock is 1.3 % slower, which matters only for stops of hundreds of
// microseconds.
static const u32 NominalClockHz = 3546900;

// Frame lengths of the models, from libspectrum's timing tables.
static const uint32_t *FuseFrameTStates (void)
{
	static uint32_t FrameTStates[MachineCount];
	for (unsigned i = 0; i < MachineCount; i++)
	{
		FrameTStates[i] = fuseula_frame_tstates ((fuseula_machine) i);
	}
	return FrameTStates;
}

CSpPieApp::CSpPieApp (CMemorySystem *pMemorySystem, CBcmFrameBuffer *pFrameBuffer,
		      CSmiEventReader *pReader, unsigned nArmClockMHz, TMachineChoice Choice)
:	CMultiCoreSupport (pMemorySystem),
	m_pFrameBuffer (pFrameBuffer),
	m_pReader (pReader),
	m_nArmClockMHz (nArmClockMHz),
	m_FrameClock (&m_Log, FuseFrameTStates (), Choice),
	m_UlaClock (&m_FrameClock, &m_FrameClock, TickFrequency ()),
	m_TStateClock (&m_Queue, TickFrequency (), NominalClockHz),
	m_Presenter (&m_Log),
	m_pDraw (0)
{
	memset (&m_Shared, 0, sizeof m_Shared);
	for (unsigned i = 0; i < MachineCount; i++)
	{
		m_pContention[i] = 0;
	}
}

boolean CSpPieApp::InitializeRenderer (void)
{
	// The whole display in RAM: the Spectrum picture in the middle, black
	// (or the overlay) at the sides.
	m_pDraw = new u32[DisplayWidth * DisplayHeight];
	if (m_pDraw == 0)
	{
		return FALSE;
	}
	for (unsigned i = 0; i < DisplayWidth * DisplayHeight; i++)
	{
		m_pDraw[i] = 0xFF000000u;
	}

	if (!m_Presenter.Initialize (m_pDraw + PictureTop * DisplayWidth + PictureLeft,
				     DisplayWidth))
	{
		return FALSE;
	}

	// Fuse's contention tables for the frame clock's contention check.
	// Core 1 only reads them.
	for (unsigned i = 0; i < MachineCount; i++)
	{
		u32 nSize = fuseula_frame_tstates ((fuseula_machine) i);
		m_pContention[i] = new u8[nSize];
		if (m_pContention[i] == 0)
		{
			return FALSE;
		}
		fuseula_contention ((fuseula_machine) i, m_pContention[i], nSize);
		m_FrameClock.SetContention ((TMachine) i, m_pContention[i]);
	}

	return TRUE;
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

	case 2:
		RunUla ();
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

static u64 DoubleBits (double nValue)
{
	u64 nBits;
	memcpy (&nBits, &nValue, sizeof nBits);
	return nBits;
}

static double BitsDouble (u64 nBits)
{
	double nValue;
	memcpy (&nValue, &nBits, sizeof nValue);
	return nValue;
}

// Core 2: the ULA clock and the frame clock, fed from core 1's queue. It
// publishes its counters, and for core 1's restart planning where the next
// frame starts in Pi time.
void CSpPieApp::RunUla (void)
{
	const u64 nFrequency = TickFrequency ();
	const u64 nPublishTicks = PublishNs * nFrequency / 1000000000;
	u64 nLastPublish = 0;

	while (1)
	{
		if (m_Queue.Drain (m_UlaClock) != 0)
		{
			m_Log.Publish ();
		}

		u64 nNow = ReadTicks ();
		if (nNow - nLastPublish < nPublishTicks)
		{
			continue;
		}
		nLastPublish = nNow;

		double nStart;
		boolean bPlan = m_UlaClock.NextFrameStartTicks (nStart);
		Publish (&m_Shared.PlanFrameStart, DoubleBits (bPlan ? nStart : 0.0));
		Publish (&m_Shared.PlanRate, DoubleBits (m_UlaClock.Rate ()));
		Publish (&m_Shared.PlanFrameTStates, m_FrameClock.FrameTStates ());
		Publish (&m_Shared.PlanValid, bPlan ? 1 : 0);

		Publish (&m_Shared.Port1FFDWrite, m_FrameClock.Port1FFDWrite ());
		Publish (&m_Shared.ResetBursts, m_FrameClock.Resets ());
		Publish (&m_Shared.LogDropped, m_Log.Dropped ());
		Publish (&m_Shared.Clamped, m_FrameClock.Clamped ());
		Publish (&m_Shared.Ambiguous, m_UlaClock.Ambiguous () + m_UlaClock.PhaseSlips ());
		Publish (&m_Shared.ClockStopped, m_UlaClock.Stopped () ? 1 : 0);
		Publish (&m_Shared.Acquisitions, m_FrameClock.Acquisitions ());
		Publish (&m_Shared.Alignments, m_FrameClock.Alignments ());
		Publish (&m_Shared.Aligned, m_FrameClock.Aligned () ? 1 : 0);
		Publish (&m_Shared.Locked, m_FrameClock.Locked () ? 1 : 0);
		Publish (&m_Shared.FrameClocks, m_UlaClock.LastFrameClocks ());
		Publish (&m_Shared.MinFrameClocks, m_UlaClock.MinFrameClocks ());
		Publish (&m_Shared.MaxFrameClocks, m_UlaClock.MaxFrameClocks ());
	}
}

// Reads for the next SMI transfer, starting now. In stopped mode it ends
// in the border, where the ULA clock can hold the stall across the
// restart (PlanTransferReads); otherwise it is the longest transfer.
unsigned CSpPieApp::PlanReads (void)
{
	if (Fetch (&m_Shared.PlanValid) == 0)
	{
		return SmiBatchTransfers;
	}
	double nStart = BitsDouble (Fetch (&m_Shared.PlanFrameStart));
	double nRate = BitsDouble (Fetch (&m_Shared.PlanRate));
	double nL = (double) Fetch (&m_Shared.PlanFrameTStates);
	if (nRate <= 0 || nL <= 0)
	{
		return SmiBatchTransfers;
	}

	double t = ((double) ReadTicks () - nStart) * nRate;
	t -= nL * (double) (int64_t) (t / nL);
	if (t < 0)
	{
		t += nL;
	}
	return PlanTransferReads (t, m_TStateClock.ReadTicks () * nRate, (uint32_t) nL,
				  SmiBatchTransfers);
}

void CSpPieApp::RunIntake (void)
{
	const u64 nFrequency = TickFrequency ();
	const u64 nLeadTicks = RestartLeadNs * nFrequency / 1000000000;
	const u64 nRestartSpinTicks = 2 * RestartLeadNs * nFrequency / 1000000000;
	const u64 nPublishTicks = PublishNs * nFrequency / 1000000000;
	u64 nLastPublish = 0;
	boolean bSpun = FALSE;		// waited for the end of the running transfer
	const volatile u32 *pRing = m_pReader->Ring ();

	TIntakeCounters Counters;
	memset (&Counters, 0, sizeof Counters);
	TIntakeState State;
	ResetIntake (State);

	u64 nBusyTicks = 0;
	u64 nRestarts = 0;
	u64 nMaxGap = 0;
	u64 nMaxBacklog = 0;
	u64 nBadProducer = 0;

	// Word numbers count every ring word since the start; nWord is the
	// number of the word at nRead.
	unsigned nRead = m_pReader->Start ();
	u64 nTransferStart = ReadTicks ();
	u64 nLastNotDone = nTransferStart;
	u64 nWord = 0;
	u64 nTransferWord = 0;		// first word of the running transfer
	u64 nTransferWordsOff = 0;
	unsigned nTransferReads = SmiBatchTransfers;	// reads of the running transfer
	m_TStateClock.StartTransfer (nTransferWord, nTransferStart);
	Publish (&m_Shared.Running, 1);

	while (1)
	{
		u64 nLoopStart = ReadTicks ();
		boolean bBusy = FALSE;

		// Near the end of an SMI transfer, wait for it and start the next
		// one at once.
		// The running transfer lasts nTransferReads reads.
		u64 nTransferTicks = (u64) (nTransferReads * m_TStateClock.ReadTicks ());
		if (!bSpun && nLoopStart - nTransferStart + nLeadTicks >= nTransferTicks)
		{
			bSpun = TRUE;
			u64 nSpinStart = ReadTicks ();
			u64 nNow = nSpinStart;
			while (!m_pReader->TransferDone () && nNow - nSpinStart < nRestartSpinTicks)
			{
				nLastNotDone = nNow;
				nNow = ReadTicks ();
			}
		}

		// End of an SMI transfer: start the next one.
		if (nLoopStart - nTransferStart >= nTransferTicks / 2)
		{
			if (m_pReader->TransferDone ())
			{
				// The next transfer's first word: a transfer is
				// SmiTransferWords words. The DMA position must agree,
				// allowing for words still on their way to memory.
				u64 nExpected = nTransferWord + nTransferReads;
				u64 nFirst = nExpected;
				unsigned nEnd = m_pReader->ProducerIndex ();
				if (nEnd != RingWords)
				{
					u64 nAt = nWord + (nEnd + RingWords - nRead) % RingWords;
					if (nAt > nExpected || nAt + 4 < nExpected)
					{
						nFirst = nAt;
						nTransferWordsOff++;
					}
				}

				nTransferReads = PlanReads ();
				m_pReader->Restart (nTransferReads);
				nTransferStart = ReadTicks ();
				bSpun = FALSE;
				nTransferWord = nFirst;
				m_TStateClock.StartTransfer (nTransferWord, nTransferStart);

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

		// New ring words: decode them, place every event in time and log it.
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
			m_TStateClock.BeginBatch (nSeen, nWord + nBacklog);
			unsigned nLeft = BatchWords;
			while (nRead != nProducer && !bMismatch && nLeft != 0)
			{
				unsigned nEnd = nProducer > nRead ? nProducer : RingWords;
				if (nEnd - nRead > nLeft)
				{
					nEnd = nRead + nLeft;
				}
				nLeft -= nEnd - nRead;
				m_pReader->InvalidateWords (nRead, nEnd);
				size_t nUsed = ProcessWords (pRing + nRead, nEnd - nRead, nWord,
							     m_TStateClock, State, Counters, bMismatch);
				nRead = (unsigned) ((nRead + nUsed) % RingWords);
				nWord += nUsed;
			}
			m_Queue.Publish ();

			if (bMismatch)
			{
				// Resync skips the ring up to the producer and starts a
				// new transfer there.
				unsigned nNew = m_pReader->Resync ();
				nWord += (nNew + RingWords - nRead) % RingWords;
				nRead = nNew;
				nTransferStart = ReadTicks ();
				nLastNotDone = nTransferStart;
				nTransferWord = nWord;
				nTransferReads = SmiBatchTransfers;
				m_TStateClock.StartTransfer (nTransferWord, nTransferStart);
				ResetIntake (State);
				bSpun = FALSE;
			}
		}

		if (bBusy)
		{
			nBusyTicks += ReadTicks () - nLoopStart;
		}
		if (bBusy && nLoopStart - nLastPublish >= nPublishTicks)
		{
			nLastPublish = nLoopStart;
			Publish (&m_Shared.ReadPicoseconds,
				 (u64) (m_TStateClock.ReadTicks () * 1e12 / (double) nFrequency));
			Publish (&m_Shared.FrameEvents, Counters.FrameEvents);
			Publish (&m_Shared.CountAmbiguous, m_TStateClock.Ambiguous ());
			Publish (&m_Shared.QueueDropped, m_Queue.Dropped ());
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
			Publish (&m_Shared.TransferWordsOff, nTransferWordsOff);
		}
		Publish (&m_Shared.BadProducer, nBadProducer);
	}
}

void CSpPieApp::RunRenderer (void)
{
	u32 *pDraw = m_pDraw;

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
		m_Presenter.Present ();
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
		Report.LogDropped = (unsigned) (Fetch (&m_Shared.LogDropped) + Fetch (&m_Shared.QueueDropped));
		Report.Clamped = (unsigned) Fetch (&m_Shared.Clamped);
		Report.Ambiguous = (unsigned) (Fetch (&m_Shared.Ambiguous) + Fetch (&m_Shared.CountAmbiguous));
		Report.Acquisitions = (unsigned) Fetch (&m_Shared.Acquisitions);
		Report.Alignments = (unsigned) Fetch (&m_Shared.Alignments);
		Report.Aligned = Fetch (&m_Shared.Aligned) != 0;
		Report.Locked = Fetch (&m_Shared.Locked) != 0;
		Report.FrameClocks = (unsigned) Fetch (&m_Shared.FrameClocks);
		Report.MinFrameClocks = (unsigned) Fetch (&m_Shared.MinFrameClocks);
		Report.MaxFrameClocks = (unsigned) Fetch (&m_Shared.MaxFrameClocks);
		Report.Skipped = m_Presenter.Skipped ();
		Report.Repeated = m_Presenter.Repeated ();
		Report.SpectrumFrame = m_Presenter.LastFrame ();
		Report.Machine = m_Presenter.LastRecord ().Machine;
		Report.Frames = nFrame;
		Report.ClockStopped = Fetch (&m_Shared.ClockStopped) != 0;
		Report.ReadPicoseconds = (unsigned) Fetch (&m_Shared.ReadPicoseconds);
		Report.Port1FFDWrite = (unsigned) Fetch (&m_Shared.Port1FFDWrite);
		Report.ResetBursts = (unsigned) Fetch (&m_Shared.ResetBursts);

		nPrevEvents = nEvents;
		nPrevEmpty = nEmpty;
		nPrevFrameEvents = nFrameEvents;
		nPrevBusy = nBusy;
		nPrevFrameEnd = nFrameEnd;
#endif
	}
}

}
