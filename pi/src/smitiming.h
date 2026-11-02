//
// smitiming.h
//
// SMI read timing and transfer size. Used by the SMI reader on the Pi and,
// for the read period, by the ring clock (ringclock.h). No platform
// dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_smitiming_h
#define _sppie_smitiming_h

namespace sppie {

// SMI timing. The SMI clock is PLLD (500 MHz, as Circle's CSMIMaster
// assumes) divided by SmiCycleNs / 2, so one SMI clock is SmiCycleNs.
//
// One read = (setup + strobe + hold + pace) clocks
//          = (3 + 4 + 3 + 0) x 20 ns = 200 ns, SOE low for 80 ns.
// One pair (an event or an empty pair) = two reads = 400 ns, so 2.5 million
// pairs per second. In 18-bit mode each read is one ring word.
static const unsigned SmiCycleNs	= 20;
static const unsigned SmiSetup		= 3;
static const unsigned SmiStrobe		= 4;
static const unsigned SmiHold		= 3;
static const unsigned SmiPace		= 0;

static const unsigned SmiReadNs		= (SmiSetup + SmiStrobe + SmiHold + SmiPace) * SmiCycleNs;
static const unsigned SmiPairNs		= 2 * SmiReadNs;

// Reads per programmed transfer. Must be even so that a transfer always ends
// on a pair boundary. The width of the SMI length register is not
// documented; this value fits in 16 bits. At 200 ns per read one transfer
// lasts about 13.1 ms.
static const unsigned SmiBatchTransfers	= 65534;
static const unsigned SmiTransferWords	= SmiBatchTransfers;	// ring words per transfer

}

#endif
