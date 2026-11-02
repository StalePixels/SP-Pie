//
// framelock.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "framelock.h"

namespace sppie {

static const int64_t One = 65536;		// 1.0 in Q16

static inline int64_t FloorDiv (int64_t a, int64_t b)
{
	int64_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0)))
	{
		q--;
	}
	return q;
}

static inline int64_t RoundDiv (int64_t a, int64_t b)
{
	return FloorDiv (a + b / 2, b);
}

static inline int64_t Abs (int64_t a)
{
	return a < 0 ? -a : a;
}

CFrameLock::CFrameLock (uint64_t nTickFrequency, TMachineChoice Choice)
:	m_nFrequency (nTickFrequency),
	m_Choice (Choice)
{
	Start (0);
}

int64_t CFrameLock::NsToQ (uint64_t nNs) const
{
	return (int64_t) ((nNs * m_nFrequency / 1000) * (uint64_t) One / 1000000);
}

void CFrameLock::Start (uint64_t nNow)
{
	m_nBaseTicks = nNow;
	m_nBaseFrac = 0;
	m_nBaseFrame = 0;
	// Until the first lock, the 128K class: its paging decode keeps the RAM
	// copy right on a 128K-class machine, and a 48K does not write its ports.
	m_nPeriodQ = NsToQ (m_Choice == Choose48K ? FramePeriodNs48K : FramePeriodNs128K);

	m_bLocked = false;
	m_nVersion = 0;
	m_nAcquisitions = 0;
	m_nOutliers = 0;

	m_bHaveLast = false;
	m_nLastFrame = 0;
	m_nOutlierRun = 0;
	m_nFarBlocks = 0;
	m_bBlockOpen = false;
	m_nHistoryCount = 0;
	m_nHistoryNext = 0;

	UpdateMachine ();
	StartAcquisition ();
}

uint64_t CFrameLock::PeriodNs (void) const
{
	return (((uint64_t) m_nPeriodQ >> 8) * 1000000000ull / m_nFrequency) >> 8;
}

// nTicks - E(m_nBaseFrame), Q16
int64_t CFrameLock::RelQ (uint64_t nTicks) const
{
	return (int64_t) (nTicks - m_nBaseTicks) * One - (int64_t) m_nBaseFrac;
}

// nTicks - E(nFrame), Q16
int64_t CFrameLock::ResidualQ (uint32_t nFrame, uint64_t nTicks) const
{
	return RelQ (nTicks) - (int64_t) (int32_t) (nFrame - m_nBaseFrame) * m_nPeriodQ;
}

// E(nFrame) := nTicks + nOffsetQ / 65536
void CFrameLock::SetBase (uint32_t nFrame, uint64_t nTicks, int64_t nOffsetQ)
{
	int64_t nWhole = FloorDiv (nOffsetQ, One);
	m_nBaseTicks = nTicks + (uint64_t) nWhole;
	m_nBaseFrac = (uint32_t) (nOffsetQ - nWhole * One);
	m_nBaseFrame = nFrame;
}

void CFrameLock::UpdateMachine (void)
{
	m_Machine = ResolveMachine (PeriodNs () < MachineThresholdNs ? Machine48K : Machine128K,
				    m_Choice);

	// FrameEventTStates in ticks, minus the transport calibration.
	m_nStartOffsetQ =   (int64_t) FrameEventTStates * m_nPeriodQ
			  / (int64_t) Timing ().FrameTStates
			- NsToQ (TransportCalibrationNs);
}

void CFrameLock::Position (uint64_t nTicks, uint32_t &rFrame, uint32_t &rTState) const
{
	// time since S(m_nBaseFrame)
	int64_t nSinceQ = RelQ (nTicks) + m_nStartOffsetQ;
	int64_t k = FloorDiv (nSinceQ, m_nPeriodQ);
	int64_t nRemQ = nSinceQ - k * m_nPeriodQ;		// 0 <= nRemQ < period

	rFrame = m_nBaseFrame + (uint32_t) k;
	rTState = (uint32_t) (nRemQ * (int64_t) Timing ().FrameTStates / m_nPeriodQ);
}

uint64_t CFrameLock::FrameStart (uint32_t nFrame) const
{
	// S(nFrame) - m_nBaseTicks, Q16, rounded up to a whole tick
	int64_t nQ =   (int64_t) m_nBaseFrac
		     + (int64_t) (int32_t) (nFrame - m_nBaseFrame) * m_nPeriodQ
		     - m_nStartOffsetQ;
	return m_nBaseTicks + (uint64_t) -FloorDiv (-nQ, One);
}

void CFrameLock::Rebase (uint64_t nTicks)
{
	int64_t k = FloorDiv (RelQ (nTicks), m_nPeriodQ);
	if (k > 0)
	{
		SetBase (m_nBaseFrame + (uint32_t) k, m_nBaseTicks,
			 (int64_t) m_nBaseFrac + k * m_nPeriodQ);
	}
}

void CFrameLock::StartAcquisition (void)
{
	m_bLocked = false;
	m_nAcqCount = 0;
}

void CFrameLock::FrameEvent (uint64_t nTicks)
{
	if (!m_bLocked)
	{
		Acquire (nTicks);
		return;
	}

	int64_t k = RoundDiv (RelQ (nTicks), m_nPeriodQ);
	uint32_t nFrame = m_nBaseFrame + (uint32_t) k;

	if (Abs (ResidualQ (nFrame, nTicks)) > m_nPeriodQ / 8)
	{
		m_nOutliers++;
		if (++m_nOutlierRun >= OutlierRunLimit)
		{
			StartAcquisition ();
		}
		return;
	}
	m_nOutlierRun = 0;

	if (m_bHaveLast && (int32_t) (nFrame - m_nLastFrame) > (int32_t) GapFrames)
	{
		m_nHistoryCount = 0;
		m_bBlockOpen = false;
	}
	m_bHaveLast = true;
	m_nLastFrame = nFrame;

	if (m_bBlockOpen && (int32_t) (nFrame - m_nBlockStart) >= (int32_t) BlockFrames)
	{
		CloseBlock ();
		if (!m_bLocked)
		{
			Acquire (nTicks);
			return;
		}
	}

	// The line may have moved: residual against the current one.
	int64_t nResidualQ = ResidualQ (nFrame, nTicks);

	if (!m_bBlockOpen)
	{
		m_bBlockOpen = true;
		m_nBlockStart = nFrame;
		m_nBlockMinQ = nResidualQ;
		m_nBlockMinFrame = nFrame;
		m_nBlockMinTicks = nTicks;
	}
	else if (nResidualQ < m_nBlockMinQ)
	{
		m_nBlockMinQ = nResidualQ;
		m_nBlockMinFrame = nFrame;
		m_nBlockMinTicks = nTicks;
	}
}

void CFrameLock::CloseBlock (void)
{
	m_bBlockOpen = false;

	m_HistoryFrames[m_nHistoryNext] = m_nBlockMinFrame;
	m_HistoryTicks[m_nHistoryNext] = m_nBlockMinTicks;
	m_nHistoryNext = (m_nHistoryNext + 1) % HistoryBlocks;
	if (m_nHistoryCount < HistoryBlocks)
	{
		m_nHistoryCount++;
	}

	uint32_t nNewest = m_nBlockMinFrame;
	int64_t nNewestQ = ResidualQ (nNewest, m_nBlockMinTicks);

	if (Abs (nNewestQ) > NsToQ (ReacquireNs))
	{
		if (++m_nFarBlocks >= 2)
		{
			StartAcquisition ();
			return;
		}
	}
	else
	{
		m_nFarBlocks = 0;
	}

	// Least-squares line through the block points, x in frames relative to
	// the newest point, y the residual against the current line.
	int64_t nSlopeQ = 0;
	int64_t nInterceptQ = nNewestQ;

	if (m_nHistoryCount >= MinFitBlocks)
	{
		int64_t N = m_nHistoryCount;
		int64_t Sx = 0, Sy = 0, Sxx = 0, Sxy = 0;
		for (unsigned i = 0; i < m_nHistoryCount; i++)
		{
			int64_t x = (int32_t) (m_HistoryFrames[i] - nNewest);
			int64_t y = ResidualQ (m_HistoryFrames[i], m_HistoryTicks[i]);
			Sx += x;
			Sy += y;
			Sxx += x * x;
			Sxy += x * y;
		}

		int64_t nDen = N * Sxx - Sx * Sx;
		if (nDen > 0)
		{
			nSlopeQ = (N * Sxy - Sx * Sy) / nDen;

			// A correction this large means a bad fit; keep the period.
			if (Abs (nSlopeQ) > m_nPeriodQ / 10000)
			{
				nSlopeQ = 0;
			}
			nInterceptQ = (Sy - nSlopeQ * Sx) / N;
		}
	}

	// The new line goes through the fitted point at the newest frame.
	int64_t nEOldQ =   (int64_t) m_nBaseFrac
			 + (int64_t) (int32_t) (nNewest - m_nBaseFrame) * m_nPeriodQ;
	SetBase (nNewest, m_nBaseTicks, nEOldQ + nInterceptQ);
	m_nPeriodQ += nSlopeQ;

	UpdateMachine ();
	m_nVersion++;
}

void CFrameLock::Acquire (uint64_t nTicks)
{
	const uint64_t nNominal = (uint64_t) (NsToQ (20000000) / One);	// 20 ms in ticks

	if (m_nAcqCount > 0)
	{
		uint64_t nDelta = nTicks - m_AcqTicks[m_nAcqCount - 1];
		uint64_t nFrames = (nDelta + nNominal / 2) / nNominal;
		if (nFrames == 0 || nFrames > AcqMaxGapFrames)
		{
			m_nAcqCount = 0;
		}
		else
		{
			m_AcqFrames[m_nAcqCount] = m_AcqFrames[m_nAcqCount - 1] + (uint32_t) nFrames;
			m_AcqTicks[m_nAcqCount] = nTicks;
			m_nAcqCount++;
		}
	}

	if (m_nAcqCount == 0)
	{
		m_AcqFrames[0] = 0;
		m_AcqTicks[0] = nTicks;
		m_nAcqCount = 1;
		return;
	}

	if (m_nAcqCount < AcqSamples)
	{
		return;
	}
	m_nAcqCount = 0;

	const unsigned nLast = AcqSamples - 1;
	int64_t nFrames = m_AcqFrames[nLast];
	int64_t nPeriodQ = (int64_t) (m_AcqTicks[nLast] - m_AcqTicks[0]) * One / nFrames;

	uint64_t nPeriodNs = (((uint64_t) nPeriodQ >> 8) * 1000000000ull / m_nFrequency) >> 8;
	if (nPeriodNs < 19900000 || nPeriodNs > 20100000)
	{
		return;
	}

	// Lowest point relative to the first sample: the earliest-arrival line
	// goes through it.
	int64_t nMinQ = 0;
	for (unsigned i = 1; i < AcqSamples; i++)
	{
		int64_t nQ = (int64_t) (m_AcqTicks[i] - m_AcqTicks[0]) * One
			     - (int64_t) m_AcqFrames[i] * nPeriodQ;
		if (nQ < nMinQ)
		{
			nMinQ = nQ;
		}
	}

	// Frame numbers continue: the frame of the last sample is one after
	// the frame the old line gave it.
	uint32_t nOldFrame, nOldTState;
	Position (nTicks, nOldFrame, nOldTState);

	SetBase (nOldFrame + 1, m_AcqTicks[0], nMinQ + nFrames * nPeriodQ);
	m_nPeriodQ = nPeriodQ;

	m_bLocked = true;
	m_nAcquisitions++;
	m_bHaveLast = false;
	m_nOutlierRun = 0;
	m_nFarBlocks = 0;
	m_bBlockOpen = false;
	m_nHistoryCount = 0;
	m_nHistoryNext = 0;

	UpdateMachine ();
	m_nVersion++;
}

}
