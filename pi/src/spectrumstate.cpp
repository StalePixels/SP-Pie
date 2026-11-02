//
// spectrumstate.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "spectrumstate.h"

namespace sppie {

// +2A/+3 special paging: banks at 0x0000, 0x4000, 0x8000, 0xC000 for 0x1FFD
// bits 2, 1 [WOS128].
static const uint8_t SpecialBanks[4][4] =
{
	{ 0, 1, 2, 3 },
	{ 4, 5, 6, 7 },
	{ 4, 5, 6, 3 },
	{ 4, 7, 6, 3 }
};

void CSpectrumState::Reset (void)
{
	for (unsigned b = 0; b < BankCount; b++)
	{
		for (unsigned i = 0; i < BankBytes; i++)
		{
			Ram[b][i] = 0;
		}
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

uint8_t CSpectrumState::BankAt (uint16_t nAddress, TMachine Machine) const
{
	unsigned nSlot = nAddress >> 14;

	if (Machine == MachinePlus3 && (Port1FFD & 0x01))
	{
		return SpecialBanks[(Port1FFD >> 1) & 3][nSlot];
	}

	switch (nSlot)
	{
	case 0:
		return NoBank;

	case 1:
		return 5;

	case 2:
		return 2;

	case 3:
	default:
		return Machine == Machine48K ? 0 : (uint8_t) (Port7FFD & 0x07);
	}
}

void CSpectrumState::PortWrite (uint16_t nPort, uint8_t nData, TMachine Machine)
{
	// ULA: every even port [WOS48]
	if ((nPort & 0x0001) == 0)
	{
		Border = nData & 0x07;
	}

	// ULAplus: A15..A14 = 10 or 11, A7..A6 = 00, A2 = 0, A0 = 1
	if ((nPort & 0x80C5) == 0x8001)
	{
		if ((nPort & 0x4000) == 0)
		{
			UlaPlusRegister = nData;
		}
		else
		{
			// Group 0 sets one palette entry; group 1 is the mode
			// register. Groups 2 and 3 are not defined by ULAplus.
			switch (UlaPlusRegister >> 6)
			{
			case 0:
				UlaPlusPalette[UlaPlusRegister & 0x3F] = nData;
				break;

			case 1:
				UlaPlusMode = nData;
				break;

			default:
				break;
			}
		}
	}

	if (Machine == Machine48K || PagingLocked ())
	{
		return;
	}

	// Paging ports [WOS128]. Both decodes are checked against the
	// unchanged lock, so one write that matches both is fully applied.
	if (Machine == Machine128K)
	{
		if ((nPort & 0x8002) == 0)
		{
			Port7FFD = nData;
		}
	}
	else
	{
		bool b7FFD = (nPort & 0xC002) == 0x4000;
		bool b1FFD = (nPort & 0xF002) == 0x1000;
		if (b7FFD)
		{
			Port7FFD = nData;
		}
		if (b1FFD)
		{
			Port1FFD = nData;
		}
	}
}

void CSpectrumState::Apply (const TEvent &rEvent, TMachine Machine)
{
	switch (rEvent.Kind)
	{
	case KindMemory:
		{
			uint8_t nBank = BankAt (rEvent.Address, Machine);
			if (nBank != NoBank)
			{
				Ram[nBank][rEvent.Address & (BankBytes - 1)] = rEvent.Data;
			}
		}
		break;

	case KindIo:
		PortWrite (rEvent.Address, rEvent.Data, Machine);
		break;

	case KindReset:
		// Paging back to 0 and unlocked, ULAplus off. RAM, the border
		// and the ULAplus palette stay.
		Port7FFD = 0;
		Port1FFD = 0;
		UlaPlusRegister = 0;
		UlaPlusMode = 0;
		break;

	default:
		break;
	}
}

}
