//
// borderextension.h
//
// The border extension (control-port feature 2, bit 0): the areas left and
// right of the 352 x 270 Spectrum picture in the 480 x 270 display take
// the colour of the border at that line's edge, so the border fills the
// 16:9 screen. Each line's left area is its leftmost picture pixel, the
// right area its rightmost.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_borderextension_h
#define _sppie_borderextension_h

#include "picture.h"
#include <stdint.h>

namespace sppie {

// pDisplay is the whole 480 x 270 display, nPitch pixels between rows.
inline void FillBorderExtension (uint32_t *pDisplay, unsigned nPitch)
{
	for (unsigned y = PictureTop; y < PictureTop + PictureHeight; y++)
	{
		uint32_t *pRow = pDisplay + y * nPitch;
		uint32_t nLeft = pRow[PictureLeft];
		uint32_t nRight = pRow[PictureLeft + PictureWidth - 1];
		for (unsigned x = 0; x < PictureLeft; x++)
		{
			pRow[x] = nLeft;
		}
		for (unsigned x = PictureLeft + PictureWidth; x < DisplayWidth; x++)
		{
			pRow[x] = nRight;
		}
	}
}

// Black in the areas left and right of the picture.
inline void ClearSideAreas (uint32_t *pDisplay, unsigned nPitch)
{
	for (unsigned y = 0; y < DisplayHeight; y++)
	{
		uint32_t *pRow = pDisplay + y * nPitch;
		for (unsigned x = 0; x < PictureLeft; x++)
		{
			pRow[x] = 0xFF000000u;
		}
		for (unsigned x = PictureLeft + PictureWidth; x < DisplayWidth; x++)
		{
			pRow[x] = 0xFF000000u;
		}
	}
}

}

#endif
