//
// intake.h
//
// Decodes a run of packed event words (from the DMA ring) and hands each
// valid event to a sink. Platform-free, so the host tests drive it with
// synthetic streams; the Pi kernel calls it on the words between its read
// position and the DMA producer position.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_intake_h
#define _sppie_intake_h

#include "event.h"
#include <stddef.h>
#include <stdint.h>

namespace sppie {

// Receives every valid event in stream order.
class IEventSink
{
public:
	virtual void Event (const TEvent &rEvent) = 0;

protected:
	~IEventSink (void) {}
};

struct TIntakeCounters
{
	uint64_t	Events;		// valid events handed to the sink
	uint64_t	EmptyPairs;	// exact empty pairs (0x0000, 0x8000)
	uint64_t	Overflows;	// events with OVF = 1: one per run of dropped events
	uint64_t	Mismatches;	// words with a wrong H bit
	uint64_t	Malformed;	// words with correct H bits that break the format, dropped
	uint64_t	Resets;		// reset events, also counted in Events
	uint64_t	FrameEvents;	// events with F = 1, also counted in Events
};

// Decodes up to nCount words from pWords and gives each valid event to
// rSink.
//
// Stops at the first word with a wrong H bit: that word and every word after
// it are not used, Mismatches is incremented once, and bMismatch is set. The
// caller must then stop SMI reads, pulse SYNC and discard the rest of the
// ring up to the producer position.
//
// Returns the number of words consumed, including the mismatched word.
size_t ProcessWords (const volatile uint32_t *pWords, size_t nCount,
		     IEventSink &rSink, TIntakeCounters &rCounters,
		     bool &bMismatch);

}

#endif
