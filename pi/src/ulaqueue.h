//
// ulaqueue.h
//
// The hand-off from the intake core (core 1) to the ULA core (core 2): a
// single-producer, single-consumer ring of what the clock count gives on,
// in order: events with their counts, empty-pair counts with their read
// times, and restarts of the reads.
//
//  - The producer (core 1, behind CTStateClock) stores entries and
//    publishes its write index with a release store after each batch of
//    ring words. It never waits: when the ring is full the entry is dropped
//    and counted.
//  - The consumer (core 2) loads the write index with an acquire load,
//    replays the entries into its own sink (the ULA clock), and publishes
//    its read index with a release store.
//
// Uses the GCC/Clang __atomic builtins. No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_ulaqueue_h
#define _sppie_ulaqueue_h

#include "tstateclock.h"
#include <stdint.h>

namespace sppie {

class CUlaQueue final : public ITimedEventSink
{
public:
	// Three empty pairs a microsecond at most: about 26 ms of entries.
	static const uint32_t Entries	= 1u << 16;
	static const uint32_t Mask	= Entries - 1;

	CUlaQueue (void)	{ Reset (); }

	void Reset (void)
	{
		m_nWrite = m_nWriteLocal = m_nReadCached = m_nDropped = 0;
		m_nRead = 0;
	}

	// --- producer (core 1) ---------------------------------------------

	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override
	{
		TEntry *pEntry = Next ();
		if (pEntry)
		{
			pEntry->Type = TypeEvent;
			pEntry->Event = rEvent;
			pEntry->Count = nCount;
		}
	}

	void Clock (uint64_t nCount, double nTicks) override
	{
		TEntry *pEntry = Next ();
		if (pEntry)
		{
			pEntry->Type = TypeClock;
			pEntry->Count = nCount;
			pEntry->Ticks = nTicks;
		}
	}

	void ReadsRestarted (void) override
	{
		TEntry *pEntry = Next ();
		if (pEntry)
		{
			pEntry->Type = TypeRestart;
		}
	}

	void Publish (void)
	{
		__atomic_store_n (&m_nWrite, m_nWriteLocal, __ATOMIC_RELEASE);
	}

	uint32_t Dropped (void) const	{ return m_nDropped; }

	// --- consumer (core 2) ---------------------------------------------

	// Replays every published entry into rSink. Returns how many.
	uint32_t Drain (ITimedEventSink &rSink)
	{
		uint32_t nWrite = __atomic_load_n (&m_nWrite, __ATOMIC_ACQUIRE);
		uint32_t nRead = m_nRead;
		uint32_t nCount = nWrite - nRead;
		for (; nRead != nWrite; nRead++)
		{
			const TEntry &rEntry = m_Entries[nRead & Mask];
			switch (rEntry.Type)
			{
			case TypeEvent:		rSink.TimedEvent (rEntry.Event, rEntry.Count);	break;
			case TypeClock:		rSink.Clock (rEntry.Count, rEntry.Ticks);	break;
			default:		rSink.ReadsRestarted ();			break;
			}
		}
		__atomic_store_n (&m_nRead, nRead, __ATOMIC_RELEASE);
		return nCount;
	}

private:
	enum : uint8_t { TypeEvent, TypeClock, TypeRestart };

	struct TEntry
	{
		uint64_t	Count;
		double		Ticks;
		TEvent		Event;
		uint8_t		Type;
	};

	TEntry *Next (void)
	{
		if (m_nWriteLocal - m_nReadCached >= Entries)
		{
			m_nReadCached = __atomic_load_n (&m_nRead, __ATOMIC_ACQUIRE);
			if (m_nWriteLocal - m_nReadCached >= Entries)
			{
				m_nDropped++;
				return 0;
			}
		}
		return &m_Entries[m_nWriteLocal++ & Mask];
	}

private:
	// Each core's indices in their own 64-byte cache line.
	alignas (64) uint32_t	m_nWrite;
	uint32_t		m_nWriteLocal;
	uint32_t		m_nReadCached;
	uint32_t		m_nDropped;
	alignas (64) uint32_t	m_nRead;
	alignas (64) TEntry	m_Entries[Entries];
};

}

#endif
