//
// intake.h
//
// Turns a run of packed event words (from the DMA ring) into state changes
// and counters. Platform-free, so the host tests drive it with synthetic
// streams; the Pi kernel calls it on the words between its read position
// and the DMA producer position.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_intake_h
#define _sppie_intake_h

#include "event.h"
#include "spectrumstate.h"
#include <stddef.h>
#include <stdint.h>

namespace sppie {

struct TIntakeCounters
{
	uint64_t	Events;		// valid events applied to the state
	uint64_t	EmptyPairs;	// exact empty pairs (0x0000, 0x8000)
	uint64_t	Overflows;	// events with OVF = 1: one per run of dropped events
	uint64_t	Mismatches;	// words with a wrong H bit
	uint64_t	Malformed;	// words with correct H bits that break the format, dropped
};

// Decodes up to nCount words from pWords and applies each valid event.
//
// Stops at the first word with a wrong H bit: that word and every word after
// it are not used, Mismatches is incremented once, and bMismatch is set. The
// caller must then stop SMI reads, pulse SYNC and discard the rest of the
// ring up to the producer position.
//
// Returns the number of words consumed, including the mismatched word.
size_t ProcessWords (const volatile uint32_t *pWords, size_t nCount,
		     CSpectrumState &rState, TIntakeCounters &rCounters,
		     bool &bMismatch);

}

#endif
