//
// intake.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "intake.h"

namespace sppie {

size_t ProcessWords (const volatile uint32_t *pWords, size_t nCount, uint64_t nFirstWord,
		     IEventSink &rSink, TIntakeState &rState, TIntakeCounters &rCounters,
		     bool &bMismatch)
{
	bMismatch = false;

	for (size_t i = 0; i < nCount; i++)
	{
		uint32_t nWord = pWords[i];

		if (!rState.HaveR0)
		{
			if (!IsR0 (nWord))
			{
				rCounters.Mismatches++;
				bMismatch = true;
				return i + 1;
			}
			rState.R0 = nWord;
			rState.R0Word = nFirstWord + i;
			rState.HaveR0 = true;
			continue;
		}
		rState.HaveR0 = false;

		TEvent Event;
		TPairKind Kind = DecodePair (rState.R0, nWord, Event);

		if (rState.SkipPair && Kind != PairMismatch)
		{
			rState.SkipPair = false;
			continue;
		}

		switch (Kind)
		{
		case PairEmpty:
			rCounters.EmptyPairs++;
			rSink.Empty (Event.Count, rState.R0Word);
			break;

		case PairEvent:
			if (Event.Overflow)
			{
				rCounters.Overflows++;
			}
			if (Event.Kind == KindReset)
			{
				rCounters.Resets++;
			}
			if (Event.Frame)
			{
				rCounters.FrameEvents++;
			}
			rCounters.Events++;
			rSink.Event (Event, rState.R0Word);
			break;

		case PairMalformed:
			rCounters.Malformed++;
			break;

		case PairMismatch:
		default:
			rCounters.Mismatches++;
			bMismatch = true;
			return i + 1;
		}
	}

	return nCount;
}

}
