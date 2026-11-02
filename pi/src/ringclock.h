//
// ringclock.h
//
// Read times of ring words from their position in the ring.
//
// SMI does one read every SmiReadNs at a fixed rate, and every read lands
// in the ring as one word, empty pairs included. So within one SMI transfer
// the word number is a clock: word w of a transfer that started at time T0
// with word w0 was read at
//
//     T0 + (w - w0 + 1) * SmiReadNs
//
// Reads stop between transfers, so each transfer has its own start: the
// intake core calls StartTransfer () with the first word number and the
// time it started the transfer. Word numbers count every word since the
// first start; they do not wrap with the ring.
//
// This gives the read time however late the intake core sees the words.
// A stamp is never later than the time the core saw the word, and never
// earlier than the stamp before it.
//
// Before the first StartTransfer () a word's stamp is the time it was seen.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_ringclock_h
#define _sppie_ringclock_h

#include "smitiming.h"
#include <stdint.h>

namespace sppie {

class CRingClock
{
public:
	// More transfer starts than this ahead of the words being stamped are
	// not kept; the ring holds about eight transfers.
	static const unsigned MaxStarts = 16;

	explicit CRingClock (uint64_t nTickFrequency)
	:	m_nReadQ ((uint64_t) SmiReadNs * nTickFrequency * 65536 / 1000000000),
		m_nReadTicks ((double) SmiReadNs * (double) nTickFrequency / 1e9),
		m_nNominalTicks (m_nReadTicks)
	{
		Reset ();
	}

	void Reset (void)
	{
		m_nCount = 0;
		m_nFirst = 0;
		m_nLast = 0;
		m_nReadIndex = 0;
		m_bObserved = false;
		m_nCandidate = 0;
		m_bMeasured = false;
	}

	// The transfer whose first word is nWord started at nTicks. Word
	// numbers of successive calls must not decrease.
	void StartTransfer (uint64_t nWord, uint64_t nTicks)
	{
		if (m_nCount == MaxStarts)
		{
			m_nFirst = (m_nFirst + 1) % MaxStarts;
			m_nCount--;
			if (m_nReadIndex > 0)
			{
				m_nReadIndex--;
			}
		}
		TStart &rStart = m_Starts[(m_nFirst + m_nCount) % MaxStarts];
		rStart.nWord = nWord;
		rStart.nTicks = nTicks;
		m_nCount++;

		// The period measured in the transfer that ended, into the
		// running value (a measurement off by more than 1 % is a fault
		// and is not used).
		if (m_nCandidate > 0)
		{
			double nRatio = m_nCandidate / m_nNominalTicks;
			if (nRatio > 0.99 && nRatio < 1.01)
			{
				m_nReadTicks += (m_nCandidate - m_nReadTicks)
						/ (m_bMeasured ? PeriodWeight : 1);
				m_bMeasured = true;
			}
		}
		m_nCandidate = 0;
		m_bObserved = false;
	}

	// Read time of word nWord, which the core saw at nSeen. Word numbers
	// of successive calls must not decrease.
	uint64_t Stamp (uint64_t nWord, uint64_t nSeen)
	{
		// Drop starts that only cover earlier words.
		while (m_nCount > 1 && m_Starts[(m_nFirst + 1) % MaxStarts].nWord <= nWord)
		{
			m_nFirst = (m_nFirst + 1) % MaxStarts;
			m_nCount--;
			if (m_nReadIndex > 0)
			{
				m_nReadIndex--;
			}
		}

		uint64_t nStamp = nSeen;
		if (m_nCount != 0)
		{
			const TStart &rStart = m_Starts[m_nFirst];
			if (nWord >= rStart.nWord)
			{
				nStamp = rStart.nTicks + (((nWord - rStart.nWord + 1) * m_nReadQ) >> 16);
			}
			if (nStamp > nSeen)
			{
				nStamp = nSeen;
			}
		}

		if (nStamp < m_nLast)
		{
			nStamp = m_nLast;
		}
		m_nLast = nStamp;

		return nStamp;
	}

	// Read time of word nWord in ticks, with no rounding and no limits:
	// the start of its transfer plus its distance from the transfer's
	// first word, at the measured read period. For the ULA clock
	// (ulaclock.h), which needs the read times to a fraction of a T-state.
	// Word numbers of successive calls must not decrease.
	double ReadTime (uint64_t nWord)
	{
		if (m_nCount == 0)
		{
			return 0.0;
		}
		if (m_nReadIndex >= m_nCount)
		{
			m_nReadIndex = 0;
		}
		// The latest start at or before nWord, from the last one used.
		while (m_nReadIndex + 1 < m_nCount
		       && m_Starts[(m_nFirst + m_nReadIndex + 1) % MaxStarts].nWord <= nWord)
		{
			m_nReadIndex++;
		}
		while (m_nReadIndex > 0 && m_Starts[(m_nFirst + m_nReadIndex) % MaxStarts].nWord > nWord)
		{
			m_nReadIndex--;
		}
		const TStart &rStart = m_Starts[(m_nFirst + m_nReadIndex) % MaxStarts];
		double nReads = (double) (int64_t) (nWord - rStart.nWord + 1);
		return (double) rStart.nTicks + nReads * m_nReadTicks;
	}

	// The DMA had written words up to nWord (exclusive) at nTicks. Over a
	// long run within one transfer these give the true read period, which
	// ReadTime () uses from the next transfer on. The SMI clock is not
	// measured anywhere else.
	void Observe (uint64_t nWord, uint64_t nTicks)
	{
		if (m_nCount == 0)
		{
			return;
		}
		const TStart &rLast = m_Starts[(m_nFirst + m_nCount - 1) % MaxStarts];
		if (nWord <= rLast.nWord)
		{
			return;
		}
		if (!m_bObserved)
		{
			m_nObservedWord = nWord;
			m_nObservedTicks = nTicks;
			m_bObserved = true;
			return;
		}
		if (nWord - m_nObservedWord >= MinObservedWords)
		{
			m_nCandidate = (double) (nTicks - m_nObservedTicks)
				       / (double) (nWord - m_nObservedWord);
		}
	}

	double ReadTicks (void) const		{ return m_nReadTicks; }

	// Words in a run before Observe () gives a read period.
	static const uint64_t MinObservedWords = 32768;

private:
	struct TStart
	{
		uint64_t	nWord;
		uint64_t	nTicks;
	};

	const uint64_t	m_nReadQ;	// ticks per read, Q16
	double		m_nReadTicks;	// ticks per read, measured
	const double	m_nNominalTicks;
	unsigned	m_nReadIndex;	// start ReadTime () used last, from m_nFirst
	bool		m_bObserved;
	uint64_t	m_nObservedWord;
	uint64_t	m_nObservedTicks;
	double		m_nCandidate;	// period measured in the running transfer
	bool		m_bMeasured;	// m_nReadTicks has a measurement

	static constexpr double PeriodWeight = 8;
	TStart		m_Starts[MaxStarts];
	unsigned	m_nFirst;
	unsigned	m_nCount;
	uint64_t	m_nLast;
};

}

#endif
