//
// intake.h
//
// Decodes a run of read words (from the DMA ring) into pairs and hands each
// empty pair and each valid event to a sink. Platform-free, so the host
// tests drive it with synthetic streams; the Pi kernel calls it on the
// words between its read position and the DMA producer position.
//
// A pair can start in one run and end in the next, so the decoder keeps
// its place between calls in a TIntakeState.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_intake_h
#define _sppie_intake_h

#include "event.h"
#include <stddef.h>
#include <stdint.h>

namespace sppie {

// Receives every empty pair and every valid event in stream order. nWord
// is the word number of the pair's R0 (word numbers count every ring word
// since the start).
class IEventSink
{
public:
	virtual void Empty (uint8_t nCount, bool bStall, uint64_t nWord) = 0;
	virtual void Event (const TEvent &rEvent, uint64_t nWord) = 0;

protected:
	~IEventSink (void) {}
};

struct TIntakeCounters
{
	uint64_t	Events;		// valid events handed to the sink
	uint64_t	EmptyPairs;	// valid empty pairs
	uint64_t	Overflows;	// events with OVF = 1: one per run of dropped events
	uint64_t	Mismatches;	// pairs with a wrong H bit
	uint64_t	Malformed;	// pairs with correct H bits that break the format, dropped
	uint64_t	Resets;		// reset events, also counted in Events
	uint64_t	FrameEvents;	// events with F = 1, also counted in Events
};

struct TIntakeState
{
	bool		HaveR0;		// the last word of the previous run was an R0
	uint32_t	R0;
	uint64_t	R0Word;
	bool		SkipPair;	// the next pair is the first after SYNC
};

// The state for the first word after power-up or after SYNC. The first
// pair then is an empty pair with no time count (protocol.md), so it is
// skipped.
static inline void ResetIntake (TIntakeState &rState)
{
	rState.HaveR0 = false;
	rState.R0 = 0;
	rState.R0Word = 0;
	rState.SkipPair = true;
}

// Decodes up to nCount words from pWords, the first of which is word number
// nFirstWord.
//
// Stops at the first pair with a wrong H bit: that pair and every word
// after it are not used, Mismatches is incremented once, and bMismatch is
// set. The caller must then stop SMI reads, pulse SYNC, discard the rest of
// the ring up to the producer position and call ResetIntake ().
//
// Returns the number of words consumed, including the mismatched word.
size_t ProcessWords (const volatile uint32_t *pWords, size_t nCount, uint64_t nFirstWord,
		     IEventSink &rSink, TIntakeState &rState, TIntakeCounters &rCounters,
		     bool &bMismatch);

}

#endif
