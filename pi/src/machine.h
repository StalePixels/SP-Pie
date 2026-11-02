//
// machine.h
//
// The Spectrum models SP-Pie emulates, how the model is chosen, and the
// constants that tie the CPLD's clock count to Fuse's T-states.
//
// The frame lengths come from libspectrum's timing tables at start-up
// (fuseula_frame_tstates), not from here.
//
// No platform dependencies, so it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_machine_h
#define _sppie_machine_h

#include <stdint.h>

namespace sppie {

// The values are those of enum fuseula_machine.
enum TMachine : uint8_t
{
	Machine48K	= 0,	// 48K and 16K
	Machine128K	= 1,	// 128K and +2
	MachinePlus3	= 2,	// +2A and +3
	MachineCount
};

enum TMachineChoice : uint8_t
{
	ChooseAuto,		// frame length: 48K, or for the 128K class 128K/+2
				// until the first write to port 0x1FFD, then +2A/+3
	ChooseAuto128,		// frame length: 48K, or 128K/+2 for the 128K class
	Choose48K,		// always 48K
	Choose128K,		// always 128K/+2
	ChoosePlus3		// always +2A/+3
};

// The model for a frame-length class (Machine48K or Machine128K, as
// measured), a choice, and whether a write to port 0x1FFD has been seen.
// The frame length cannot tell a 128K or +2 from a +2A or +3: they share
// the timing but decode the paging ports differently. Only the +2A/+3 has
// port 0x1FFD; its ROM writes it at start-up and at every ROM change, and
// the 128K and +2 ROMs never write it.
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

// Fuse's decode of port 0x1FFD on the +2A/+3 (machines_periph.c,
// plus3_memory_ports: mask 0xF002, value 0x1000).
static inline bool IsPort1FFD (uint16_t nPort)
{
	return (nPort & 0xF002) == 0x1000;
}

// The CPLD counts the edge-connector clock and stamps each write with the
// count at the rising edge of /WR (docs/protocol.md). Fuse makes a write
// take effect at a T-state of its own (fuseula.h). The two differ by a
// constant for each kind of write:
//
//  - Memory write. /WR rises half a T-state before the end of the 3
//    T-state write cycle; Fuse writes at the end of it (memory_pages.c,
//    writebyte: contention, then tstates += 3, then the write).
//  - I/O write. /WR rises half a T-state before the end of the 4 T-state
//    I/O cycle; Fuse writes one T-state after its start (periph.c,
//    writeport: ula_contend_port_early adds 1, then the write). So an I/O
//    write takes effect IoWriteLeadTStates earlier than a memory write
//    with the same count.
//
// Frame start. After an interrupt acknowledge the Z80 pushes its return
// address. Fuse (z80.c, z80_interrupt) starts the acknowledge at the first
// instruction boundary at or after T-state 0 of the frame, adds 7
// T-states, and then writes the high byte of PC with writebyte, at
// FrameEventTStates. That push is the event the CPLD marks with F. Its
// count, less FrameEventTStates, is the frame start when the Z80 finished
// an instruction exactly at T-state 0; otherwise it is later by the rest
// of that instruction. The earliest F event therefore gives the frame
// start, in the same terms as the memory writes.
static const uint32_t FrameEventTStates		= 10;
static const uint32_t IoWriteLeadTStates	= 3;

// An F event that lands more than this many T-states after the frame start
// cannot be the push after an acknowledge at that start (Fuse accepts an
// interrupt only while /INT is low, 32 or 36 T-states, and the longest
// instruction is 23 T-states).
static const uint32_t MaxAckDelayTStates	= 64;

}

#endif
