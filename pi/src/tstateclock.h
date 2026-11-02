//
// tstateclock.h
//
// Rebuilds the CPLD's full clock count from the 5-bit counts in the pairs
// (docs/protocol.md, Time base) and gives every event its count.
//
//  - Empty pairs. Each one carries the count mod 32 at the moment it was
//    loaded, at the end of the read before its R0. The running count
//    advances by the difference mod 32. The number of whole wraps comes
//    from the times of those reads (ringclock.h) and the clock rate: the advance is the value that
//    matches the 5-bit count and is nearest to the elapsed time times the
//    rate. Between two pairs a few reads apart that is the plain
//    difference; across a stop in the reads (a transfer restart or a
//    resync) the read times choose the wraps.
//  - Events. An event is captured before the CPLD loads the next empty
//    pair, so its count is the latest count at or below that pair's count
//    that matches its 5 bits. Events wait here until that pair arrives.
//  - Stops. Before a stop in the reads, the events still waiting are given
//    the earliest count at or after the previous empty pair's count, less
//    the CPLD's two-read delay, that matches their 5 bits: they were read
//    just after that pair.
//
// An event is placed exactly when it waited less than 32 clocks between
// its capture and the reference pair. Events read after a stop of 32
// clocks or more may not be; they are counted (Ambiguous ()).
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_tstateclock_h
#define _sppie_tstateclock_h

#include "intake.h"
#include "ringclock.h"
#include <stdint.h>

namespace sppie {

// Receives every event with its full clock count, in stream order, and
// the progress of the count.
class ITimedEventSink
{
public:
	virtual void TimedEvent (const TEvent &rEvent, uint64_t nCount) = 0;

	// Every event captured before nCount - CaptureLagClocks has been given
	// to TimedEvent ().
	virtual void Clock (uint64_t nCount) = 0;

protected:
	~ITimedEventSink (void) {}
};

class CTStateClock final : public IEventSink
{
public:
	// The read side of the CPLD sees a new event two reads after its
	// capture, and can load an empty pair in that time. Two reads are 400
	// ns, under 1.5 clocks; this allows for a stale pointer sample as well.
	static const uint32_t CaptureLagClocks = 3;

	// Events kept while no empty pair comes. The CPLD queue holds four, and
	// the Pi reads faster than the Z80 writes, so empty pairs come long
	// before this.
	static const unsigned MaxPending = 64;

	// The count starts here, so that counts less a frame are positive.
	static const uint64_t CountBase = 1ull << 32;

	CTStateClock (ITimedEventSink *pSink, uint64_t nTickFrequency, uint32_t nClockHz);

	void Reset (void);

	// The clock rate used to count wraps across a stop in the reads.
	void SetClockHz (uint32_t nClockHz);

	// The SMI transfer whose first ring word is word number nWord started
	// at nTicks. The reads stopped before it, so waiting events are given
	// their counts now.
	void StartTransfer (uint64_t nWord, uint64_t nTicks);

	// The next run of words was seen at nSeen.
	void BeginBatch (uint64_t nSeen)	{ m_nSeen = nSeen; }

	void Empty (uint8_t nCount, uint64_t nWord) override;
	void Event (const TEvent &rEvent, uint64_t nWord) override;

	bool HaveCount (void) const		{ return m_bHaveCount; }
	uint64_t Count (void) const		{ return m_nCount; }
	uint64_t Ambiguous (void) const		{ return m_nAmbiguous; }

private:
	void ResolveBackward (void);
	void ResolveForward (void);
	void Deliver (const TEvent &rEvent, uint64_t nCount);

private:
	ITimedEventSink	*m_pSink;
	CRingClock	m_Clock;
	uint64_t	m_nTickFrequency;
	uint64_t	m_nRateQ32;		// clocks per tick, Q32

	uint64_t	m_nSeen;
	bool		m_bHaveCount;
	uint64_t	m_nCount;		// count of the last empty pair
	uint64_t	m_nStamp;		// its read time
	uint64_t	m_nPrevCount;		// count of the empty pair before it
	uint64_t	m_nLastEvent;		// count of the last event delivered

	TEvent		m_Pending[MaxPending];
	unsigned	m_nPending;

	uint64_t	m_nAmbiguous;
};

}

#endif
