//
// overlay.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "overlay.h"
#include "renderer.h"
#include <circle/font.h>
#include <circle/string.h>

namespace sppie {

static const u32 TextColour = 0xFFFFFFFFu;
static const u32 TextBackground = 0xFF000000u;

COverlay::COverlay (void)
:	m_Font (Font6x7)
{
}

void COverlay::Draw (const TFrameReport &rReport, u32 *pPixels, unsigned nPitch)
{
	CString Line1, Line2, Line3;

	Line1.Format ("EV%6u EMP%7u OVF%5u HS%5u BAD%5u",
		      rReport.Events, rReport.EmptyPairs, rReport.Overflows,
		      rReport.Mismatches, rReport.Malformed);
	Line2.Format ("IN%6u IDI%6u RN%6u IDR%6u us",
		      rReport.IntakeBusyUs, rReport.IntakeIdleUs,
		      rReport.RenderUs, rReport.RenderIdleUs);
	Line3.Format ("BL%7u RS%6u GAP%5u PB%4u NE%u C%4u F%u",
		      rReport.MaxBacklogWords, rReport.Restarts, rReport.MaxRestartGapUs,
		      rReport.BadProducer, rReport.NotEmpty ? 1 : 0,
		      rReport.ArmClockMHz, rReport.Frames);

	unsigned nLineHeight = 8;
	unsigned nTop = ScreenHeight - 3 * nLineHeight;

	DrawText (Line1, 1, nTop, pPixels, nPitch);
	DrawText (Line2, 1, nTop + nLineHeight, pPixels, nPitch);
	DrawText (Line3, 1, nTop + 2 * nLineHeight, pPixels, nPitch);
}

void COverlay::DrawText (const char *pText, unsigned nX, unsigned nY,
			 u32 *pPixels, unsigned nPitch)
{
	unsigned nWidth = m_Font.GetCharWidth ();
	unsigned nHeight = m_Font.GetCharHeight ();
	if (nHeight > 8)
	{
		nHeight = 8;
	}

	for (; *pText != '\0' && nX + nWidth <= ScreenWidth; pText++, nX += nWidth)
	{
		for (unsigned y = 0; y < nHeight && nY + y < ScreenHeight; y++)
		{
			CCharGenerator::TPixelLine Line = m_Font.GetPixelLine (*pText, y);
			u32 *pRow = pPixels + (nY + y) * nPitch + nX;
			for (unsigned x = 0; x < nWidth; x++)
			{
				pRow[x] = m_Font.GetPixel (x, Line) ? TextColour : TextBackground;
			}
		}
	}
}

}
