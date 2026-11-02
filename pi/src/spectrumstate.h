//
// spectrumstate.h
//
// The Pi's copy of the Spectrum: all RAM, the paging registers, the border
// and ULAplus. It changes only through write events from the CPLD, which
// sends every memory and I/O write with its full address. This class
// decodes each write the way the chosen model does.
//
// The renderer core owns it. It applies each event at the event's T-state
// while it draws a Spectrum frame (see renderer.h), so the state is always
// the state at one moment of the Spectrum frame.
//
// Decodes. Sources:
//  [WOS48]  World of Spectrum FAQ, 48K reference: "Every even I/O address
//           will address the ULA"; "The lowest three bits specify the
//           border colour".
//  [WOS128] World of Spectrum FAQ, 128K reference,
//           https://worldofspectrum.org/faq/reference/128kreference.htm
//           128K/+2: port 0x7FFD responds to "any port address with bits 1
//           and 15 reset". +2A/+3: 0x7FFD responds to addresses with bit 1
//           reset, bit 14 set and bit 15 reset; 0x1FFD to addresses with bit
//           1 reset, bit 12 set and bits 13 to 15 reset. 0x7FFD bits 2..0:
//           RAM bank at 0xC000; bit 3: screen (bank 5 or bank 7); bit 5:
//           "memory paging will be disabled and further output to this port
//           will be ignored until the computer is reset". 0x1FFD bit 0:
//           special paging; bits 2 and 1 select the special layout.
//  ULAplus  register port 0xBF3B and data port 0xFF3B, decoded on A15..A14
//           = 10 (register) or 11 (data), A7..A6 = 00, A2 = 0, A0 = 1: the
//           decode of the TK-Pie board, which this board follows.
//
//  Port      48K   128K/+2              +2A/+3
//  0xFE      A0 = 0 (all models)
//  0x7FFD    -     A15 = 0, A1 = 0      A15..A14 = 01, A1 = 0
//  0x1FFD    -     -                    A15..A12 = 0001, A1 = 0
//
// One write can match more than one port (for example 0xFE and 0x7FFD on a
// 128K at port 0x7FFC); it then acts on each of them, as on the real
// machine.
//
// Memory map (16 KB slots at 0x0000, 0x4000, 0x8000, 0xC000; "ROM" means
// the write changes nothing):
//  48K:            ROM, 5, 2, 0
//  128K/+2:        ROM, 5, 2, 0x7FFD bits 2..0
//  +2A/+3 normal:  as 128K
//  +2A/+3 special (0x1FFD bit 0 = 1), by 0x1FFD bits 2, 1:
//                  00: 0, 1, 2, 3   01: 4, 5, 6, 7
//                  10: 4, 5, 6, 3   11: 4, 7, 6, 3
//
// The lock (0x7FFD bit 5) stops writes to both paging ports until reset.
//
// No platform dependencies, so it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_spectrumstate_h
#define _sppie_spectrumstate_h

#include "event.h"
#include "machinetiming.h"
#include <stdint.h>

namespace sppie {

static const unsigned BankBytes		= 0x4000;
static const unsigned BankCount		= 8;
static const unsigned ScreenBytes	= 0x1B00;	// 6912: pixels and attributes
static const unsigned UlaPlusPaletteSize = 64;

static const uint8_t Port7FFDLock	= 0x20;
static const uint8_t Port7FFDScreen	= 0x08;
static const uint8_t NoBank		= 0xFF;

struct CSpectrumState
{
	uint8_t	Ram[BankCount][BankBytes];
	uint8_t	Border;			// port 0xFE bits 2..0
	uint8_t	Port7FFD;		// last accepted write
	uint8_t	Port1FFD;		// last accepted write (+2A/+3)
	uint8_t	UlaPlusRegister;	// bits 7..6 group, bits 5..0 sub-group
	uint8_t	UlaPlusMode;		// last mode-group write; bit 0 = palette on
	uint8_t	UlaPlusPalette[UlaPlusPaletteSize];	// GGGRRRBB entries

	// Clears RAM and every register.
	void Reset (void);

	// Applies one event as model Machine decodes it.
	void Apply (const TEvent &rEvent, TMachine Machine);

	// RAM bank at a memory address, or NoBank for ROM.
	uint8_t BankAt (uint16_t nAddress, TMachine Machine) const;

	// The screen the ULA shows: bank 5, or bank 7 when 0x7FFD bit 3 is set
	// on a 128K-class model.
	const uint8_t *ShownScreen (TMachine Machine) const
	{
		return (Machine != Machine48K && (Port7FFD & Port7FFDScreen)) ? Ram[7] : Ram[5];
	}

	bool UlaPlusOn (void) const
	{
		return (UlaPlusMode & 0x01) != 0;
	}

	bool PagingLocked (void) const
	{
		return (Port7FFD & Port7FFDLock) != 0;
	}

private:
	void PortWrite (uint16_t nPort, uint8_t nData, TMachine Machine);
};

}

#endif
