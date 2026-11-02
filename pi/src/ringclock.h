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
		m_nReadTicks ((double) SmiReadNs * (double) nTickFrequency / 1e9)
	{
		Reset ();
	}

	void Reset (void)
	{
		m_nCount = 0;
		m_nFirst = 0;
		m_nLast = 0;
	}

	// The transfer whose first word is nWord started at nTicks. Word
	// numbers of successive calls must not decrease.
	void StartTransfer (uint64_t nWord, uint64_t nTicks)
	{
		if (m_nCount == MaxStarts)
		{
			m_nFirst = (m_nFirst + 1) % MaxStarts;
			m_nCount--;
		}
		TStart &rStart = m_Starts[(m_nFirst + m_nCount) % MaxStarts];
		rStart.nWord = nWord;
		rStart.nTicks = nTicks;
		m_nCount++;
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
	// first word. For the ULA clock (ulaclock.h), which needs the read
	// times to a fraction of a T-state. Uses the same starts as Stamp ()
	// and must be called for the same or later words.
	double ReadTime (uint64_t nWord) const
	{
		if (m_nCount == 0)
		{
			return 0.0;
		}
		// The latest start at or before nWord.
		unsigned nIndex = m_nFirst;
		for (unsigned i = 1; i < m_nCount; i++)
		{
			const TStart &rNext = m_Starts[(m_nFirst + i) % MaxStarts];
			if (rNext.nWord > nWord)
			{
				break;
			}
			nIndex = (m_nFirst + i) % MaxStarts;
		}
		const TStart &rStart = m_Starts[nIndex];
		double nReads = (double) (int64_t) (nWord - rStart.nWord + 1);
		return (double) rStart.nTicks + nReads * m_nReadTicks;
	}

private:
	struct TStart
	{
		uint64_t	nWord;
		uint64_t	nTicks;
	};

	const uint64_t	m_nReadQ;	// ticks per read, Q16
	const double	m_nReadTicks;	// ticks per read
	TStart		m_Starts[MaxStarts];
	unsigned	m_nFirst;
	unsigned	m_nCount;
	uint64_t	m_nLast;
};

}

#endif
