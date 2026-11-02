//
// interlace.h
//
// Interlace mixing (control-port feature 3). Some programs show two
// pictures in turn, one each frame, so that the eye mixes them: more
// colours, or finer detail, than one Spectrum frame holds. They either
// flip between the normal and the shadow screen (port 0x7FFD bit 3) or
// rewrite one screen each frame. On a CRT the eye mixes them; on HDMI the
// alternation shows as flicker, and a skipped or repeated frame shows one
// of the two pictures twice. Mixing shows the average of the last two
// complete Spectrum frames instead: half each, the CRT-style weight, or
// the two frames on alternate lines of the frame buffer (the line weave).
//
// CInterlaceDetector decides from the pictures themselves, whichever
// screen they come from. A frame alternates when its picture equals the
// picture two frames back and differs from the one just before. Mixing
// starts after OnFrames alternating frames in a row and stops after
// OffFrames frames in a row that do not alternate. Normal animation,
// double-buffered at 50 frames per second included, shows a new picture
// each frame, which is never the one two frames back, so it does not
// start mixing. A screen flip alone is not taken as alternation for this
// reason. The pictures are compared by a 64-bit hash.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_interlace_h
#define _sppie_interlace_h

#include <stdint.h>

namespace sppie {

// A hash of nCount pixels.
inline uint64_t PictureHash (const uint32_t *pPixels, unsigned nPitch, unsigned nWidth,
			     unsigned nHeight)
{
	// Four independent multiply-xor lanes, so the multiplies overlap.
	const uint64_t Prime = 0x100000001B3ull;
	uint64_t h[4] = { 0xCBF29CE484222325ull, 0x84222325CBF29CE4ull,
			  0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full };
	for (unsigned y = 0; y < nHeight; y++)
	{
		const uint32_t *pRow = pPixels + y * nPitch;
		unsigned x = 0;
		for (; x + 4 <= nWidth; x += 4)
		{
			for (unsigned i = 0; i < 4; i++)
			{
				h[i] = (h[i] ^ pRow[x + i]) * Prime;
			}
		}
		for (; x < nWidth; x++)
		{
			h[0] = (h[0] ^ pRow[x]) * Prime;
		}
	}
	return h[0] ^ (h[1] * 3) ^ (h[2] * 5) ^ (h[3] * 7);
}

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
	static const unsigned OnFrames	= 8;	// alternating frames in a row to start
	static const unsigned OffFrames	= 4;	// other frames in a row to stop

	CInterlaceDetector (void)	{ Reset (); }

	void Reset (void)
	{
		m_nHashes = 0;
		m_nRun = 0;
		m_nMiss = 0;
		m_bOn = false;
	}

	// The hash of each complete Spectrum frame's picture, in order.
	void Frame (uint64_t nHash)
	{
		bool bAlternates = m_nHashes >= 2 && nHash == m_Hash[1] && nHash != m_Hash[0];

		m_Hash[1] = m_Hash[0];
		m_Hash[0] = nHash;
		if (m_nHashes < 2)
		{
			m_nHashes++;
		}

		if (bAlternates)
		{
			m_nMiss = 0;
			if (m_nRun < OnFrames)
			{
				m_nRun++;
			}
			if (m_nRun >= OnFrames)
			{
				m_bOn = true;
			}
		}
		else
		{
			m_nRun = 0;
			if (m_nMiss < OffFrames)
			{
				m_nMiss++;
			}
			if (m_nMiss >= OffFrames)
			{
				m_bOn = false;
			}
		}
	}

	bool On (void) const	{ return m_bOn; }

private:
	uint64_t	m_Hash[2];	// the last frame's, and the one before
	unsigned	m_nHashes;
	unsigned	m_nRun;
	unsigned	m_nMiss;
	bool		m_bOn;
};

}

#endif
