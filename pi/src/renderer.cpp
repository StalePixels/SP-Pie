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

void CRenderState::Reset (void)
{
	State.Reset ();
	SyncColours ();
}

void CRenderState::SyncColours (void)
{
	for (unsigned i = 0; i < 16; i++)
	{
		NormalColours[i] = SpectrumColour (i);
	}
	for (unsigned i = 0; i < UlaPlusPaletteSize; i++)
	{
		UlaPlusColours[i] = UlaPlusColour (State.UlaPlusPalette[i]);
	}
}

void CRenderState::Apply (const TTimedEvent &rEvent, TMachine Machine)
{
	TEvent Event;
	Event.Kind = rEvent.Kind;
	Event.Data = rEvent.Data;
	Event.Address = rEvent.Address;
	Event.Overflow = false;
	Event.Frame = false;

	State.Apply (Event, Machine);

	// A ULAplus data-port write may have changed the entry the register
	// points at.
	if (rEvent.Kind == KindIo && (rEvent.Address & 0xC0C5) == 0xC001)
	{
		unsigned nEntry = State.UlaPlusRegister & 0x3F;
		UlaPlusColours[nEntry] = UlaPlusColour (State.UlaPlusPalette[nEntry]);
	}
}

static inline void DrawCell (uint32_t *pOut, uint8_t nBits, uint32_t nInk, uint32_t nPaper)
{
	// bit 7 is the leftmost pixel
	for (unsigned i = 0; i < 8; i++)
	{
		pOut[i] = (nBits & (0x80 >> i)) ? nInk : nPaper;
	}
}

static inline void Fill (uint32_t *pOut, uint32_t nColour)
{
	for (unsigned i = 0; i < BlockPixels; i++)
	{
		pOut[i] = nColour;
	}
}

static const uint32_t NoEvent = 0xFFFFFFFFu;

void RenderTimedFrame (CRenderState &rState, const TFrameSpan &rEvents, TMachine Machine,
		       uint32_t nFrame, uint32_t *pPixels, unsigned nPitch)
{
	const TMachineTiming &rTiming = MachineTiming (Machine);
	const CSpectrumState &rSpectrum = rState.State;
	const bool bFlashSwapped = FlashSwapped (nFrame);

	uint32_t nNext = 0;
	uint32_t nNextT = rEvents.nCount != 0 ? rEvents.At (0).TState : NoEvent;

	for (unsigned y = 0; y < PictureHeight; y++)
	{
		uint32_t *pOut = pPixels + y * nPitch;
		uint32_t nT = BlockTState (rTiming, y, 0);
		bool bScreenLine = y >= BorderTop && y < BorderTop + ScreenPixelHeight;
		unsigned nScreenY = y - BorderTop;

		for (unsigned b = 0; b < BlocksPerLine; b++, pOut += BlockPixels, nT += BorderStepTStates)
		{
			// The state at this block: every event before its T-state.
			while (nNextT < nT)
			{
				rState.Apply (rEvents.At (nNext), Machine);
				nNext++;
				nNextT = nNext < rEvents.nCount ? rEvents.At (nNext).TState : NoEvent;
			}

			bool bUlaPlus = rSpectrum.UlaPlusOn ();
			unsigned nColumn = b - ScreenFirstBlock;

			if (!bScreenLine || nColumn >= ScreenPixelWidth / 8)
			{
				Fill (pOut, bUlaPlus ? rState.UlaPlusColours[8 + rSpectrum.Border]
						     : rState.NormalColours[rSpectrum.Border]);
				continue;
			}

			const uint8_t *pScreen = rSpectrum.ShownScreen (Machine);
			uint8_t nBits = pScreen[PixelRowOffset (nScreenY) + nColumn];
			uint8_t nAttr = pScreen[AttributeRowOffset (nScreenY) + nColumn];
			uint32_t nInk, nPaper;

			if (bUlaPlus)
			{
				unsigned nGroup = (nAttr >> 6) * 16;
				nInk = rState.UlaPlusColours[nGroup + (nAttr & 7)];
				nPaper = rState.UlaPlusColours[nGroup + 8 + ((nAttr >> 3) & 7)];
			}
			else
			{
				unsigned nBright = (nAttr & 0x40) ? 8 : 0;
				nInk = rState.NormalColours[nBright | (nAttr & 7)];
				nPaper = rState.NormalColours[nBright | ((nAttr >> 3) & 7)];

				if ((nAttr & 0x80) && bFlashSwapped)
				{
					uint32_t nTemp = nInk;
					nInk = nPaper;
					nPaper = nTemp;
				}
			}

			DrawCell (pOut, nBits, nInk, nPaper);
		}
	}

	// The rest of the frame: after the last shown block.
	for (; nNext < rEvents.nCount; nNext++)
	{
		rState.Apply (rEvents.At (nNext), Machine);
	}
}

void ApplyFrame (CRenderState &rState, const TFrameSpan &rEvents, TMachine Machine)
{
	for (uint32_t i = 0; i < rEvents.nCount; i++)
	{
		rState.Apply (rEvents.At (i), Machine);
	}
}

void RenderStill (CRenderState &rState, TMachine Machine, uint32_t nFrame,
		  uint32_t *pPixels, unsigned nPitch)
{
	TFrameSpan Empty = { 0, 0, 0, 0 };
	RenderTimedFrame (rState, Empty, Machine, nFrame, pPixels, nPitch);
}

}
