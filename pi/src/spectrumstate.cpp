//
// spectrumstate.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "spectrumstate.h"

namespace sppie {

void CSpectrumState::Reset (void)
{
	for (unsigned i = 0; i < ScreenBankBytes; i++)
	{
		Bank5[i] = 0;
		Bank7[i] = 0;
	}

	Border = 0;
	Port7FFD = 0;
	Port1FFD = 0;
	UlaPlusRegister = 0;
	UlaPlusMode = 0;

	for (unsigned i = 0; i < UlaPlusPaletteSize; i++)
	{
		UlaPlusPalette[i] = 0;
	}
}

void CSpectrumState::Apply (const TEvent &rEvent)
{
	switch (rEvent.Type)
	{
	case EventBank5:
		Bank5[rEvent.Offset] = rEvent.Data;
		break;

	case EventBank7:
		Bank7[rEvent.Offset] = rEvent.Data;
		break;

	case EventPortFE:
		Border = rEvent.Data & 0x07;
		break;

	case EventUlaReg:
		UlaPlusRegister = rEvent.Data;
		break;

	case EventUlaData:
		// Group 0 sets one palette entry; group 1 is the mode register.
		// Groups 2 and 3 are not defined by ULAplus and are ignored.
		switch (UlaPlusRegister >> 6)
		{
		case 0:
			UlaPlusPalette[UlaPlusRegister & 0x3F] = rEvent.Data;
			break;

		case 1:
			UlaPlusMode = rEvent.Data;
			break;

		default:
			break;
		}
		break;

	case EventPort7FFD:
		Port7FFD = rEvent.Data;
		break;

	case EventPort1FFD:
		Port1FFD = rEvent.Data;
		break;

	default:
		break;
	}
}

}
