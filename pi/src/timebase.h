//
// timebase.h
//
// Time measurement with the ARM generic timer's physical counter
// (CNTPCT_EL0). Every core reads the same counter, it does not depend on the
// CPU clock (which the firmware may change), and reading it touches no
// device, so the intake core can use it freely. On the BCM2837 it counts at
// the frequency in CNTFRQ_EL0, set by the firmware.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_timebase_h
#define _sppie_timebase_h

#include <circle/types.h>

namespace sppie {

static inline u64 ReadTicks (void)
{
	u64 nValue;
	asm volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (nValue) : : "memory");
	return nValue;
}

static inline u64 TickFrequency (void)
{
	u64 nValue;
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (nValue));
	return nValue;
}

static inline u64 TicksToMicroseconds (u64 nTicks, u64 nFrequency)
{
	return nFrequency != 0 ? nTicks * 1000000 / nFrequency : 0;
}

// Busy-waits for at least nNanoseconds. One extra tick covers the counter's
// granularity.
static inline void DelayNanoseconds (u64 nNanoseconds)
{
	u64 nFrequency = TickFrequency ();
	u64 nTicks = (nNanoseconds * nFrequency + 999999999) / 1000000000 + 1;
	u64 nStart = ReadTicks ();
	while (ReadTicks () - nStart < nTicks)
	{
		// wait
	}
}

}

#endif
