//
// splash.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "splash.h"
#include "picture.h"

namespace sppie {

namespace {

const unsigned Width = PictureWidth;		// 352
const unsigned Height = PictureHeight;		// 270

class CCanvas
{
public:
	CCanvas (uint32_t *pPixels, unsigned nPitch, const TSplashFont &rFont)
	:	m_pPixels (pPixels), m_nPitch (nPitch), m_rFont (rFont)
	{
	}

	void Fill (unsigned x, unsigned y, unsigned w, unsigned h, uint32_t nColour)
	{
		for (unsigned j = y; j < y + h && j < Height; j++)
		{
			for (unsigned i = x; i < x + w && i < Width; i++)
			{
				m_pPixels[j * m_nPitch + i] = nColour;
			}
		}
	}

	// Text with its top left at (x, y), each font pixel nScale x nScale.
	// Returns the x after the last character.
	unsigned Text (const char *pText, unsigned x, unsigned y, uint32_t nColour,
		       unsigned nScale = 1)
	{
		for (; *pText != '\0'; pText++, x += 8 * nScale)
		{
			unsigned c = (unsigned char) *pText;
			if (c < m_rFont.nFirst || c > m_rFont.nLast)
			{
				continue;
			}
			const uint8_t *pGlyph = m_rFont.pData + (c - m_rFont.nFirst) * 8;
			for (unsigned r = 0; r < 8; r++)
			{
				for (unsigned b = 0; b < 8; b++)
				{
					if (pGlyph[r] & (0x80 >> b))
					{
						Fill (x + b * nScale, y + r * nScale, nScale, nScale, nColour);
					}
				}
			}
		}
		return x;
	}

	// The stripe of the Spectrum's packaging: four bands of colour, each
	// leaning right by one pixel per line, across h lines from (x, y).
	void Stripes (unsigned x, unsigned y, unsigned h, unsigned nBand)
	{
		for (unsigned r = 0; r < h; r++)
		{
			unsigned nLean = h - 1 - r;
			for (unsigned s = 0; s < 4; s++)
			{
				Fill (x + nLean + s * nBand, y + r, nBand, 1, SplashStripes[s]);
			}
		}
	}

private:
	uint32_t	*m_pPixels;
	unsigned	m_nPitch;
	const TSplashFont &m_rFont;
};

// Appends text to a fixed buffer.
class CLine
{
public:
	CLine (void) : m_nLength (0)	{ m_Text[0] = '\0'; }

	CLine &operator<< (const char *pText)
	{
		while (*pText != '\0' && m_nLength < sizeof m_Text - 1)
		{
			m_Text[m_nLength++] = *pText++;
		}
		m_Text[m_nLength] = '\0';
		return *this;
	}

	CLine &operator<< (unsigned nValue)
	{
		char Digits[12];
		unsigned n = 0;
		do
		{
			Digits[n++] = (char) ('0' + nValue % 10);
			nValue /= 10;
		}
		while (nValue != 0);
		char Reversed[12];
		for (unsigned i = 0; i < n; i++)
		{
			Reversed[i] = Digits[n - 1 - i];
		}
		Reversed[n] = '\0';
		return *this << Reversed;
	}

	const char *Text (void) const	{ return m_Text; }

private:
	char		m_Text[48];
	unsigned	m_nLength;
};

}

void DrawSplash (const TSplashFacts &rFacts, const TSplashFont &rFont,
		 uint32_t *pPicture, unsigned nPitch)
{
	static const char *const MachineNames[MachineCount] = { "48K", "128K", "+2A/+3" };

	CCanvas Canvas (pPicture, nPitch, rFont);

	Canvas.Fill (0, 0, Width, Height, SplashPaper);

	// Title bar: the name, and the stripe at its right end.
	Canvas.Fill (SplashBarX, SplashBarY, SplashBarWidth, SplashBarHeight, SplashBar);
	Canvas.Text ("SP-Pie", SplashBarX + 12, SplashBarY + 8, SplashTitle, 2);
	Canvas.Stripes (SplashStripeX, SplashBarY, SplashBarHeight, SplashStripeBand);

	CLine Version;
	Version << "Firmware " << (rFacts.pVersion != 0 ? rFacts.pVersion : "unknown");
	Canvas.Text (Version.Text (), SplashTextX, SplashVersionY, SplashInk);

	CLine Hdmi;
	Hdmi << "HDMI ";
	if (rFacts.nHdmiWidth != 0 && rFacts.nHdmiHeight != 0)
	{
		Hdmi << rFacts.nHdmiWidth << " x " << rFacts.nHdmiHeight;
	}
	else
	{
		Hdmi << "size unknown";
	}
	if (rFacts.nRefreshCentiHz != 0)
	{
		unsigned nCents = rFacts.nRefreshCentiHz % 100;
		Hdmi << ", " << rFacts.nRefreshCentiHz / 100 << (nCents < 10 ? ".0" : ".")
		     << nCents << " Hz";
	}
	Canvas.Text (Hdmi.Text (), SplashTextX, SplashHdmiY, SplashInk);

	// What the Pi has found.
	struct TRow
	{
		const char	*pLabel;
		bool		bFound;
		const char	*pFound;
	};
	const TRow Rows[] =
	{
		{ "CPLD link",      rFacts.bLinkAlive, "found" },
		{ "Spectrum clock", rFacts.bClockSeen, "found" },
		{ "Machine",        rFacts.bLocked,
		  rFacts.Machine < MachineCount ? MachineNames[rFacts.Machine] : "unknown" },
		{ "Frame lock",     rFacts.bLocked,    "locked" },
	};
	unsigned y = SplashRowsY;
	for (const TRow &rRow : Rows)
	{
		Canvas.Text (rRow.pLabel, SplashTextX, y, SplashInk);
		Canvas.Text (rRow.bFound ? rRow.pFound : "waiting", SplashValueX, y,
			     rRow.bFound ? SplashFound : SplashWaiting);
		y += SplashRowPitch;
	}

	if (rFacts.pConfigNote != 0)
	{
		CLine Note;
		Note << rFacts.pConfigNote;
		Canvas.Text (Note.Text (), SplashTextX, SplashNoteY,
			     rFacts.bConfigIgnored ? SplashWaiting : SplashInk);
	}

	// What to do, in the middle of its line.
	static const char Prompt[] = "Switch on or reset your Spectrum.";
	unsigned nPromptWidth = 8 * (sizeof Prompt - 1);
	Canvas.Text (Prompt, (Width - nPromptWidth) / 2, SplashPromptY, SplashPrompt);

	// The stripe again, along the foot.
	for (unsigned s = 0; s < 4; s++)
	{
		Canvas.Fill (SplashBarX, SplashFootY + s * SplashFootBand, SplashBarWidth,
			     SplashFootBand, SplashStripes[s]);
	}
}

}
