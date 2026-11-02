//
// renderer.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "renderer.h"

namespace sppie {

uint32_t SpectrumColour (unsigned nIndex)
{
	uint8_t nLevel = (nIndex & 0x08) ? 0xFF : 0xA0;
	uint8_t r = (nIndex & 0x02) ? nLevel : 0;
	uint8_t g = (nIndex & 0x04) ? nLevel : 0;
	uint8_t b = (nIndex & 0x01) ? nLevel : 0;

	return MakeRGB (r, g, b);
}

uint32_t UlaPlusColour (uint8_t nGRB)
{
	unsigned g = (nGRB >> 5) & 7;
	unsigned r = (nGRB >> 2) & 7;
	unsigned b2 = nGRB & 3;
	unsigned b = (b2 << 1) | ((b2 >> 1) | (b2 & 1));

	// 3-bit level to 8 bits, rounded: 0, 36, 73, 109, 146, 182, 219, 255
	return MakeRGB ((uint8_t) ((r * 255 + 3) / 7),
			(uint8_t) ((g * 255 + 3) / 7),
			(uint8_t) ((b * 255 + 3) / 7));
}

static inline void DrawCell (uint32_t *pOut, uint8_t nBits, uint32_t nInk, uint32_t nPaper)
{
	// bit 7 is the leftmost pixel
	for (unsigned i = 0; i < 8; i++)
	{
		pOut[i] = (nBits & (0x80 >> i)) ? nInk : nPaper;
	}
}

void RenderFrame (const CSpectrumState &rState, bool bFlashSwapped,
		  uint32_t *pPixels, unsigned nPitch)
{
	const uint8_t *pScreen = rState.ShownScreen ();
	bool bUlaPlus = rState.UlaPlusOn ();

	uint32_t Colours[UlaPlusPaletteSize];
	uint32_t nBorder;

	if (bUlaPlus)
	{
		for (unsigned i = 0; i < UlaPlusPaletteSize; i++)
		{
			Colours[i] = UlaPlusColour (rState.UlaPlusPalette[i]);
		}
		nBorder = Colours[8 + (rState.Border & 7)];
	}
	else
	{
		for (unsigned i = 0; i < 16; i++)
		{
			Colours[i] = SpectrumColour (i);
		}
		nBorder = Colours[rState.Border & 7];
	}

	// Top and bottom border bands
	for (unsigned y = 0; y < BorderTop; y++)
	{
		uint32_t *pTop = pPixels + y * nPitch;
		uint32_t *pBottom = pPixels + (BorderTop + PictureHeight + y) * nPitch;
		for (unsigned x = 0; x < ScreenWidth; x++)
		{
			pTop[x] = nBorder;
			pBottom[x] = nBorder;
		}
	}

	for (unsigned y = 0; y < PictureHeight; y++)
	{
		uint32_t *pRow = pPixels + (BorderTop + y) * nPitch;

		for (unsigned x = 0; x < BorderLeft; x++)
		{
			pRow[x] = nBorder;
			pRow[BorderLeft + PictureWidth + x] = nBorder;
		}

		const uint8_t *pBits = pScreen + PixelRowOffset (y);
		const uint8_t *pAttrs = pScreen + AttributeRowOffset (y);
		uint32_t *pOut = pRow + BorderLeft;

		for (unsigned nCol = 0; nCol < 32; nCol++, pOut += 8)
		{
			uint8_t nAttr = pAttrs[nCol];
			uint32_t nInk, nPaper;

			if (bUlaPlus)
			{
				unsigned nGroup = (nAttr >> 6) * 16;
				nInk = Colours[nGroup + (nAttr & 7)];
				nPaper = Colours[nGroup + 8 + ((nAttr >> 3) & 7)];
			}
			else
			{
				unsigned nBright = (nAttr & 0x40) ? 8 : 0;
				nInk = Colours[nBright | (nAttr & 7)];
				nPaper = Colours[nBright | ((nAttr >> 3) & 7)];

				if ((nAttr & 0x80) && bFlashSwapped)
				{
					uint32_t nTemp = nInk;
					nInk = nPaper;
					nPaper = nTemp;
				}
			}

			DrawCell (pOut, pBits[nCol], nInk, nPaper);
		}
	}
}

}
