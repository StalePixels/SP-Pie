//
// eventlog.h
//
// The hand-off from the intake core to the renderer core: a single-producer,
// single-consumer ring of timed events, plus a count of completed Spectrum
// frames.
//
//  - The producer (intake) writes entries in time order and publishes its
//    write index with a release store. It never waits: when the ring is
//    full, the entry is dropped and counted.
//  - When every event of frame n is in the ring, the producer writes the
//    frame's record and then stores the completed-frame count (n + 1) with a
//    release store.
//  - The consumer (renderer) loads the completed count with an acquire
//    load, so it sees every entry and record of the completed frames. It
//    publishes its read index with a release store when it has used them.
//
// Worst case: a Z80 writing continuously (PUSH) makes about 640,000 writes a
// second, about 13,000 per Spectrum frame. The ring holds about ten such
// frames; the renderer keeps at most three frames.
//
// Uses the GCC/Clang __atomic builtins. No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_eventlog_h
#define _sppie_eventlog_h

#include "machinetiming.h"
#include <stdint.h>

namespace sppie {

// One write event at its position in the Spectrum frame.
struct TTimedEvent
{
	uint32_t	Frame;
	uint32_t	TState;		// 0 to FrameTStates - 1, from the frame interrupt
	uint16_t	Address;
	uint8_t		Kind;		// EventKind
	uint8_t		Data;
};

// What the renderer needs to know about one completed frame.
struct TFrameRecord
{
	uint32_t	Frame;
	uint8_t		Machine;	// TMachine: timing of this frame's T-states and decode
	uint8_t		Locked;		// 1 when the frame lock was locked
};

// A run of consecutive ring entries.
struct TFrameSpan
{
	const TTimedEvent	*pRing;
	uint32_t		nMask;		// ring size - 1
	uint32_t		nFirst;
	uint32_t		nCount;

	const TTimedEvent &At (uint32_t i) const { return pRing[(nFirst + i) & nMask]; }
};

class CEventLog
{
public:
	static const uint32_t Entries		= 1u << 17;	// 131,072 events, 1.5 MiB
	static const uint32_t Mask		= Entries - 1;
	static const uint32_t RecordCount	= 64;		// must be a power of 2

	CEventLog (void)	{ Reset (); }

	// Before either core uses the log.
	void Reset (void)
	{
		m_P.nWrite = 0;
		m_P.nWriteLocal = 0;
		m_P.nReadCached = 0;
		m_P.nDropped = 0;
		m_P.nCompleted = 0;
		m_C.nRead = 0;
	}

	// --- producer --------------------------------------------------------

	// Stores an entry, published by the next Publish (). FALSE when the ring
	// is full; the entry is then dropped.
	bool Push (const TTimedEvent &rEvent)
	{
		if (m_P.nWriteLocal - m_P.nReadCached >= Entries)
		{
			m_P.nReadCached = __atomic_load_n (&m_C.nRead, __ATOMIC_ACQUIRE);
			if (m_P.nWriteLocal - m_P.nReadCached >= Entries)
			{
				m_P.nDropped++;
				return false;
			}
		}

		m_Entries[m_P.nWriteLocal & Mask] = rEvent;
		m_P.nWriteLocal++;

		return true;
	}

	void Publish (void)
	{
		__atomic_store_n (&m_P.nWrite, m_P.nWriteLocal, __ATOMIC_RELEASE);
	}

	// Frames before nCount are complete. Publishes pushed entries first.
	void Complete (uint32_t nCount, TMachine Machine, bool bLocked)
	{
		Publish ();

		uint32_t nFrom = m_P.nCompleted;
		if ((int32_t) (nCount - nFrom) > (int32_t) RecordCount)
		{
			nFrom = nCount - RecordCount;
		}
		for (uint32_t n = nFrom; n != nCount; n++)
		{
			TFrameRecord &rRecord = m_Records[n & (RecordCount - 1)];
			rRecord.Frame = n;
			rRecord.Machine = Machine;
			rRecord.Locked = bLocked ? 1 : 0;
		}

		__atomic_store_n (&m_P.nCompleted, nCount, __ATOMIC_RELEASE);
	}

	uint32_t Dropped (void) const	{ return m_P.nDropped; }

	// --- consumer --------------------------------------------------------

	// Frames before the returned count are complete.
	uint32_t Completed (void) const
	{
		return __atomic_load_n (&m_P.nCompleted, __ATOMIC_ACQUIRE);
	}

	// The entries of completed frame nFrame, starting at the read index.
	// Every entry before them must have been released.
	TFrameSpan TakeFrame (uint32_t nFrame) const
	{
		uint32_t nWrite = __atomic_load_n (&m_P.nWrite, __ATOMIC_ACQUIRE);
		uint32_t nRead = m_C.nRead;

		TFrameSpan Span = { m_Entries, Mask, nRead, 0 };
		while (nRead + Span.nCount != nWrite && Span.At (Span.nCount).Frame == nFrame)
		{
			Span.nCount++;
		}

		return Span;
	}

	// Gives the entries of a span back to the producer.
	void Release (const TFrameSpan &rSpan)
	{
		__atomic_store_n (&m_C.nRead, rSpan.nFirst + rSpan.nCount, __ATOMIC_RELEASE);
	}

	// Record of a completed frame. Valid while the consumer is less than
	// RecordCount frames behind the producer.
	TFrameRecord Record (uint32_t nFrame) const
	{
		return m_Records[nFrame & (RecordCount - 1)];
	}

private:
	// The padding keeps each core's indices in its own 64-byte cache line.
	struct TProducerSide
	{
		uint32_t	nWrite;		// published write index
		uint32_t	nWriteLocal;
		uint32_t	nReadCached;
		uint32_t	nDropped;
		uint32_t	nCompleted;	// written by the producer only
		uint32_t	Pad[11];
	};

	struct TConsumerSide
	{
		uint32_t	nRead;
		uint32_t	Pad[15];
	};

	TProducerSide	m_P;
	TConsumerSide	m_C;
	TFrameRecord	m_Records[RecordCount];
	TTimedEvent	m_Entries[Entries];
};

}

#endif
