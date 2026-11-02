//
// overlay.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "overlay.h"
#include "machinetiming.h"
#include "renderer.h"
#include <circle/font.h>
#include <circle/string.h>

namespace sppie {

static const u32 TextColour = 0xFFFFFFFFu;
static const u32 TextBackground = 0xFF000000u;
static const unsigned LineHeight = 8;
static const unsigned Margin = 4;

COverlay::COverlay (void)
:	m_Font (Font6x7)
{
}

void COverlay::Draw (const TFrameReport &rReport, u32 *pPixels, unsigned nPitch)
{
	static const char *const MachineNames[MachineCount] = { "48K", "128K", "+2A/+3" };

	struct TItem
	{
		const char	*pLabel;
		unsigned	nValue;
	};

	const TItem Left[] =
	{
		{ "EV",   rReport.Events },
		{ "EMP",  rReport.EmptyPairs },
		{ "FEV",  rReport.FrameEvents },
		{ "OVF",  rReport.Overflows },
		{ "HS",   rReport.Mismatches },
		{ "BAD",  rReport.Malformed },
		{ "RST",  rReport.Resets },
		{ "IN",   rReport.IntakeBusyUs },
		{ "IDI",  rReport.IntakeIdleUs },
		{ "RN",   rReport.RenderUs },
		{ "IDR",  rReport.RenderIdleUs },
		{ "BL",   rReport.MaxBacklogWords },
		{ "RS",   rReport.Restarts },
		{ "GAP",  rReport.MaxRestartGapUs },
		{ "PB",   rReport.BadProducer },
		{ "NE",   rReport.NotEmpty ? 1u : 0u },
	};

	const TItem Right[] =
	{
		{ "LOCK", rReport.Locked ? 1u : 0u },
		{ "PER",  rReport.PeriodNs },
		{ "ACQ",  rReport.Acquisitions },
		{ "SF",   rReport.SpectrumFrame },
		{ "SKIP", rReport.Skipped },
		{ "REP",  rReport.Repeated },
		{ "LD",   rReport.LogDropped },
		{ "CLMP", rReport.Clamped },
		{ "F",    rReport.Frames },
		{ "C",    rReport.ArmClockMHz },
	};

	const unsigned nColumnWidth = PictureLeft - 2 * Margin;
	const unsigned nRightX = PictureLeft + PictureWidth + Margin;

	unsigned nY = PictureTop;
	for (const TItem &rItem : Left)
	{
		CString Line;
		Line.Format ("%-5s%10u", rItem.pLabel, rItem.nValue);
		DrawText (Line, Margin, nY, nColumnWidth, pPixels, nPitch);
		nY += LineHeight;
	}

	nY = PictureTop;
	{
		CString Line;
		Line.Format ("%-5s%10s", "MACH",
			     MachineNames[rReport.Machine < MachineCount ? rReport.Machine : 0]);
		DrawText (Line, nRightX, nY, nColumnWidth, pPixels, nPitch);
		nY += LineHeight;
	}
	for (const TItem &rItem : Right)
	{
		CString Line;
		Line.Format ("%-5s%10u", rItem.pLabel, rItem.nValue);
		DrawText (Line, nRightX, nY, nColumnWidth, pPixels, nPitch);
		nY += LineHeight;
	}
}

// Draws one line of text, clipped to nWidth pixels from nX.
void COverlay::DrawText (const char *pText, unsigned nX, unsigned nY, unsigned nWidth,
			 u32 *pPixels, unsigned nPitch)
{
	unsigned nCharWidth = m_Font.GetCharWidth ();
	unsigned nHeight = m_Font.GetCharHeight ();
	if (nHeight > LineHeight)
	{
		nHeight = LineHeight;
	}

	unsigned nEnd = nX + nWidth;
	for (; *pText != '\0' && nX + nCharWidth <= nEnd; pText++, nX += nCharWidth)
	{
		for (unsigned y = 0; y < nHeight && nY + y < DisplayHeight; y++)
		{
			CCharGenerator::TPixelLine Line = m_Font.GetPixelLine (*pText, y);
			u32 *pRow = pPixels + (nY + y) * nPitch + nX;
			for (unsigned x = 0; x < nCharWidth; x++)
			{
				pRow[x] = m_Font.GetPixel (x, Line) ? TextColour : TextBackground;
			}
		}
	}
}

}
