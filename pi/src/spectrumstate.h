//
// spectrumstate.h
//
// The Pi's model of the Spectrum state that the picture depends on. It is
// changed only by events from the CPLD.
//
// One core writes it (event intake) and another reads it (renderer). Every
// field is a byte, so a reader sees either the old or the new value of each
// byte. The renderer may draw a frame that holds writes from part of a
// Spectrum frame; the real ULA does the same.
//
// No platform dependencies, so it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_spectrumstate_h
#define _sppie_spectrumstate_h

#include "event.h"
#include <stdint.h>

namespace sppie {

static const unsigned UlaPlusPaletteSize = 64;

struct CSpectrumState
{
	uint8_t	Bank5[ScreenBankBytes];	// normal screen: bank 5, offsets 0..0x1AFF
	uint8_t	Bank7[ScreenBankBytes];	// shadow screen: bank 7, offsets 0..0x1AFF
	uint8_t	Border;			// port 0xFE bits 2..0
	uint8_t	Port7FFD;		// last accepted write; bit 3 selects the shown screen
	uint8_t	Port1FFD;		// last accepted write; kept, not used by the picture
	uint8_t	UlaPlusRegister;	// bits 7..6 group, bits 5..0 sub-group
	uint8_t	UlaPlusMode;		// last mode-group write; bit 0 = palette on
	uint8_t	UlaPlusPalette[UlaPlusPaletteSize];	// GGGRRRBB entries

	void Reset (void);

	void Apply (const TEvent &rEvent);

	const uint8_t *ShownScreen (void) const
	{
		return (Port7FFD & 0x08) ? Bank7 : Bank5;
	}

	bool UlaPlusOn (void) const
	{
		return (UlaPlusMode & 0x01) != 0;
	}
};

}

#endif
