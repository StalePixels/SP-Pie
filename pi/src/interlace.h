//
// interlace.h
//
// Interlace mixing (control-port feature 3). Some programs show two
// pictures in turn, one each frame, by flipping between the normal and the
// shadow screen (port 0x7FFD bit 3), so that the eye mixes them: more
// colours, or finer detail, than one Spectrum frame holds. On a CRT the eye
// mixes them; on HDMI the alternation shows as flicker, and a skipped or
// repeated frame shows one of the two pictures twice. Mixing shows, on each
// line that flips, that line of the newest complete Spectrum frame blended
// with the same line of the frame before: half each, or the CRT-style
// weight. The line weave mode needs a second frame buffer line for each
// display line, which the frame buffer does not have (picture.h), so it
// shows the newest frame.
//
// CInterlaceDetector decides line by line from the screen bank alone: for
// each picture line of each frame, the bank Fuse showed on that line
// (fuseula_line_screens ()). Programs write 0x7FFD in the middle of a
// frame, so the lines of one frame can come from different banks. A line
// flips when its bank differs from its bank in the frame before. A line
// starts mixing after OnFrames frames in a row in which it flips, and stops
// after OffFrames frames in a row in which it does not. The pictures are
// not compared: a program that rewrites one screen with two pictures in
// turn does not mix, and a program that flips screens every frame does,
// whatever the screens hold.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_interlace_h
#define _sppie_interlace_h

#include <stdint.h>

namespace sppie {

// The average of two 0xAARRGGBB pixels, each channel rounded down.
inline uint32_t MixPixels (uint32_t a, uint32_t b)
{
	return (((a >> 1) & 0x7F7F7F7Fu) + ((b >> 1) & 0x7F7F7F7Fu) + (a & b & 0x01010101u))
	       | 0xFF000000u;
}

// The CRT-style mix: the newest frame weighs 5/8 and the one before 3/8,
// each channel rounded down. On a CRT the eye sees the picture the beam
// has just drawn brighter than the one before it, which has faded; this
// weight leans the mix towards the newest frame while both stay clearly
// visible. It is a chosen weight, not a measured one, and it is exact in
// integer arithmetic.
inline uint32_t MixPixelsCrt (uint32_t a, uint32_t b)
{
	uint32_t nOut = 0xFF000000u;
	for (unsigned nShift = 0; nShift < 24; nShift += 8)
	{
		uint32_t ca = (a >> nShift) & 0xFF, cb = (b >> nShift) & 0xFF;
		nOut |= ((5 * ca + 3 * cb) >> 3) << nShift;
	}
	return nOut;
}

// One frame buffer line of the line weave. The frame buffer has two lines
// for each display line; the first shows the display line as drawn, the
// second the same line with the picture area taken from the frame before
// (pWeaveRow, the picture's line), when the weave is on (pWeaveRow not 0).
// pDisplayRow is the 480-pixel display line, pOut the frame buffer line.
inline void WeaveLine (const uint32_t *pDisplayRow, const uint32_t *pWeaveRow,
		       unsigned nPictureLeft, unsigned nPictureWidth, unsigned nDisplayWidth,
		       unsigned nSubLine, uint32_t *pOut)
{
	for (unsigned x = 0; x < nDisplayWidth; x++)
	{
		bool bOld = nSubLine == 1 && pWeaveRow != 0
			    && x >= nPictureLeft && x < nPictureLeft + nPictureWidth;
		pOut[x] = bOld ? pWeaveRow[x - nPictureLeft] : pDisplayRow[x];
	}
}

class CInterlaceDetector
{
public:
	static const unsigned MaxLines	= 270;	// lines of the picture
	static const unsigned OnFrames	= 2;	// flipping frames in a row to start
	static const unsigned OffFrames	= 4;	// other frames in a row to stop

	CInterlaceDetector (void)	{ Reset (); }

	void Reset (void)
	{
		m_bFirst = true;
		for (unsigned y = 0; y < MaxLines; y++)
		{
			m_Line[y].nBank = 0;
			m_Line[y].nRun = 0;
			m_Line[y].nMiss = 0;
			m_Line[y].bOn = false;
		}
	}

	// The screen bank of each line, pBanks[0] to pBanks[nLines - 1], of
	// each complete Spectrum frame, in order.
	void Frame (const uint8_t *pBanks, unsigned nLines)
	{
		if (nLines > MaxLines)
		{
			nLines = MaxLines;
		}
		for (unsigned y = 0; y < nLines; y++)
		{
			TLine &rLine = m_Line[y];
			bool bFlips = !m_bFirst && pBanks[y] != rLine.nBank;
			rLine.nBank = pBanks[y];

			if (bFlips)
			{
				rLine.nMiss = 0;
				if (rLine.nRun < OnFrames)
				{
					rLine.nRun++;
				}
				if (rLine.nRun >= OnFrames)
				{
					rLine.bOn = true;
				}
			}
			else
			{
				rLine.nRun = 0;
				if (rLine.nMiss < OffFrames)
				{
					rLine.nMiss++;
				}
				if (rLine.nMiss >= OffFrames)
				{
					rLine.bOn = false;
				}
			}
		}
		m_bFirst = false;
	}

	// Line y is mixing.
	bool On (unsigned y) const	{ return y < MaxLines && m_Line[y].bOn; }

private:
	struct TLine
	{
		uint8_t		nBank;		// the bank in the frame before
		uint8_t		nRun;		// flipping frames in a row, up to OnFrames
		uint8_t		nMiss;		// other frames in a row, up to OffFrames
		bool		bOn;
	};

	TLine		m_Line[MaxLines];
	bool		m_bFirst;	// no frame seen yet
};

}

#endif
