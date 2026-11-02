//
// intake.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "intake.h"

namespace sppie {

size_t ProcessWords (const volatile uint32_t *pWords, size_t nCount,
		     IEventSink &rSink, TIntakeCounters &rCounters,
		     bool &bMismatch)
{
	bMismatch = false;

	for (size_t i = 0; i < nCount; i++)
	{
		TEvent Event;
		switch (DecodeWord (pWords[i], Event))
		{
		case WordEmpty:
			rCounters.EmptyPairs++;
			break;

		case WordEvent:
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
			rSink.Event (Event, i);
			rCounters.Events++;
			break;

		case WordMalformed:
			rCounters.Malformed++;
			break;

		case WordMismatch:
		default:
			rCounters.Mismatches++;
			bMismatch = true;
			return i + 1;
		}
	}

	return nCount;
}

}
