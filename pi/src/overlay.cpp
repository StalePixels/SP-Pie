//
// overlay.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "overlay.h"
#include "machine.h"
#include "picture.h"
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
		bool		bHex;
	};

	const TItem Left[] =
	{
		{ "EV",   rReport.Events },
		{ "EMP",  rReport.EmptyPairs },
		{ "FEV",  rReport.FrameEvents },
		{ "OVF",  rReport.Overflows },
		{ "HS",   rReport.Mismatches },
		{ "BAD",  rReport.Malformed },
		{ "RST",  rReport.ResetBursts },
		{ "IN",   rReport.IntakeBusyUs },
		{ "IDI",  rReport.IntakeIdleUs },
		{ "RN",   rReport.RenderUs },
		{ "IDR",  rReport.RenderIdleUs },
		{ "BL",   rReport.MaxBacklogWords },
		{ "RS",   rReport.Restarts },
		{ "GAP",  rReport.MaxRestartGapUs },
		{ "PB",   rReport.BadProducer },
		{ "CLK",  rReport.ClockStopped ? 1u : 0u },
	};

	const TItem Right[] =
	{
		{ "LOCK", rReport.Aligned ? 2u : rReport.Locked ? 1u : 0u },
		{ "ACQ",  rReport.Acquisitions },
		{ "ALN",  rReport.Alignments },
		{ "CPF",  rReport.FrameClocks },
		{ "CMIN", rReport.MinFrameClocks },
		{ "CMAX", rReport.MaxFrameClocks },
		{ "SF",   rReport.SpectrumFrame },
		{ "SKIP", rReport.Skipped },
		{ "REP",  rReport.Repeated },
		{ "LD",   rReport.LogDropped },
		{ "CLMP", rReport.Clamped },
		{ "TAMB", rReport.Ambiguous },
		{ "P3W",  rReport.Port1FFDWrite, true },
		{ "RDPS", rReport.ReadPicoseconds },
		{ "AUR",  rReport.AudioUnderruns },
	};

	const unsigned nColumnWidth = PictureLeft - 2 * Margin;
	const unsigned nRightX = PictureLeft + PictureWidth + Margin;

	// Each item is two lines: the label, then the value right-aligned in
	// nine characters, which fits the side area.
	unsigned nY = PictureTop + Margin;
	for (const TItem &rItem : Left)
	{
		CString Value;
		Value.Format ("%9u", rItem.nValue);
		DrawText (rItem.pLabel, Margin, nY, nColumnWidth, pPixels, nPitch);
		DrawText (Value, Margin, nY + LineHeight, nColumnWidth, pPixels, nPitch);
		nY += 2 * LineHeight;
	}

	nY = PictureTop + Margin;
	{
		CString Value;
		Value.Format ("%9s", MachineNames[rReport.Machine < MachineCount ? rReport.Machine : 0]);
		DrawText ("MACH", nRightX, nY, nColumnWidth, pPixels, nPitch);
		DrawText (Value, nRightX, nY + LineHeight, nColumnWidth, pPixels, nPitch);
		nY += 2 * LineHeight;
	}
	for (const TItem &rItem : Right)
	{
		CString Value;
		if (rItem.bHex)
		{
			Value.Format ("%9X", rItem.nValue);
		}
		else
		{
			Value.Format ("%9u", rItem.nValue);
		}
		DrawText (rItem.pLabel, nRightX, nY, nColumnWidth, pPixels, nPitch);
		DrawText (Value, nRightX, nY + LineHeight, nColumnWidth, pPixels, nPitch);
		nY += 2 * LineHeight;
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
