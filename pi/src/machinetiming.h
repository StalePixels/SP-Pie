//
// machinetiming.h
//
// ULA frame timing of the Spectrum models, and the constants that tie the
// Pi's event arrival times to the Spectrum frame.
//
// All T-state counts are from the frame interrupt: T-state 0 is the moment
// the ULA starts the interrupt. 1 T-state is one Z80 clock and two pixels.
//
// Sources:
//  [WOS48]  World of Spectrum FAQ, "48K ZX Spectrum Technical Reference",
//           https://worldofspectrum.org/faq/reference/48kreference.htm
//           "Each line takes exactly 224 T states"; "a frame is
//           (64+192+56)*224=69888 T states long"; "64 line times (14336 T
//           states ...) pass before the first byte of the screen (16384) is
//           displayed"; "you cannot change the border within a 'byte', an
//           8-pixel chunk"; the interrupt is "3.5MHz/69888=50.08 Hz".
//           It also says some machines are "one T state later for all
//           timings".
//  [WOS128] World of Spectrum FAQ, "128K ZX Spectrum Technical Reference",
//           https://worldofspectrum.org/faq/reference/128kreference.htm
//           "The main processor runs at 3.54690 MHz"; "228 T-states per
//           scanline"; "311 scanlines per frame"; "70908 T states per
//           frame"; "the top-left pixel of the screen is displayed 14364
//           T-states after the 50 Hz interrupt"; "The +2A/+3 share the same
//           timing information ... as the 128K/+2 machines".
//  [Z80UM]  Zilog Z80 CPU User Manual (UM0080), "Interrupt Request/
//           Acknowledge Cycle" and "Interrupt Mode 1": the acknowledge is an
//           M1 cycle with two automatic wait states (7 T-states), followed by
//           the push of PC, high byte first, as two 3 T-state memory writes.
//
// No platform dependencies, so it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_machinetiming_h
#define _sppie_machinetiming_h

#include "smitiming.h"
#include <stdint.h>

namespace sppie {

// The Spectrum models the Pi can emulate. The frame period tells the 48K
// from the 128K class; it cannot tell the 128K/+2 from the +2A/+3, which
// share the timing [WOS128] but decode the paging ports differently (see
// spectrumstate.h). TMachineChoice says how the model is chosen.
enum TMachine : uint8_t
{
	Machine48K	= 0,	// 48K and 16K
	Machine128K	= 1,	// 128K and +2
	MachinePlus3	= 2,	// +2A and +3
	MachineCount
};

enum TMachineChoice : uint8_t
{
	ChooseAuto,		// period: 48K, or for the 128K class 128K/+2 until
				// the first write to port 0x1FFD, then +2A/+3
	ChooseAuto128,		// period: 48K, or 128K/+2 for the 128K class
	Choose48K,		// always 48K
	Choose128K,		// always 128K/+2
	ChoosePlus3		// always +2A/+3
};

// The model for a frame-period class (Machine48K or Machine128K, as measured),
// a choice, and whether a write to port 0x1FFD has been seen. Only the +2A/+3
// has port 0x1FFD; its ROM writes it at start-up and at every ROM change,
// and the 128K and +2 ROMs never write it.
static inline TMachine ResolveMachine (TMachine Measured, TMachineChoice Choice,
				       bool bPort1FFDSeen)
{
	switch (Choice)
	{
	case Choose48K:		return Machine48K;
	case Choose128K:	return Machine128K;
	case ChoosePlus3:	return MachinePlus3;
	case ChooseAuto128:	return Measured == Machine48K ? Machine48K : Machine128K;
	case ChooseAuto:
	default:
		if (Measured == Machine48K)
		{
			return Machine48K;
		}
		return bPort1FFDSeen ? MachinePlus3 : Machine128K;
	}
}

struct TMachineTiming
{
	uint32_t	ClockHz;		// Z80 clock
	uint32_t	TStatesPerLine;
	uint32_t	LinesPerFrame;
	uint32_t	FrameTStates;		// TStatesPerLine * LinesPerFrame
	uint32_t	FirstPixelLine;		// line of the first pixel row of the screen
	uint32_t	FirstPixelLineT;	// T-state within that line of the first
						// pixel (byte 16384)
};

//  48K:      [WOS48]  14336 = 64 * 224 + 0, so line 64, T-state 0 in the line.
//  128K/+2:  [WOS128] 14364 = 63 * 228 + 0, so line 63, T-state 0 in the line.
//  +2A/+3:   [WOS128] the same timing as the 128K/+2.
static const TMachineTiming MachineTimings[MachineCount] =
{
	{ 3500000, 224, 312, 224 * 312, 64, 0 },
	{ 3546900, 228, 311, 228 * 311, 63, 0 },
	{ 3546900, 228, 311, 228 * 311, 63, 0 }
};

// Frame periods: 69888 / 3.5 MHz = 19.968 ms and 70908 / 3.5469 MHz =
// 19.99154 ms. A measured period below the midpoint is the 48K class.
static const uint64_t FramePeriodNs48K		= 19968000;
static const uint64_t FramePeriodNs128K		= 19991542;
static const uint64_t MachineThresholdNs	= (FramePeriodNs48K + FramePeriodNs128K) / 2;

static inline const TMachineTiming &MachineTiming (unsigned nMachine)
{
	return MachineTimings[nMachine < MachineCount ? nMachine : Machine48K];
}

// T-states from the frame interrupt to the write that the CPLD marks with F,
// when the Z80 takes the interrupt at once. [Z80UM]: the 7 T-state
// acknowledge, then the high byte of PC is written in the next 3 T-state
// cycle, with /WR rising in its last T-state, about 9.5 T-states after the
// acknowledge starts. The Z80 samples /INT in the last T-state of an
// instruction, so at least part of one T-state passes before the
// acknowledge. A longer current instruction adds up to about 23 T-states;
// the frame lock takes the earliest arrivals, which have the least of it.
static const uint32_t FrameEventTStates		= 10;

// The one calibration constant. Each event's time is the read time of its
// ring word (see ringclock.h). A Z80 write shows in the next pair the CPLD
// presents, two SMI reads after the capture, so the time from write to read
// varies over one pair period. The frame lock measures the smallest delay
// (from the earliest frame events); an ordinary event has the average
// delay, which is larger by half a pair period. It moves every event later
// in the frame when it is made larger. It has not been measured on
// hardware.
static const uint32_t TransportCalibrationNs	= SmiPairNs / 2;

// The ULA changes the border colour only at 8-pixel boundaries, every 4
// T-states, aligned with the screen bytes [WOS48].
static const uint32_t BorderStepTStates		= 4;

}

#endif
