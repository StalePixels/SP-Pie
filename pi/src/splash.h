//
// splash.h
//
// The start-up screen. It takes the place of the Spectrum picture from
// boot until the Spectrum's frames arrive with the frame start locked, and
// again whenever no Spectrum frame has come for about a second, so the
// display always shows a picture.
//
// CSplashState decides when it shows. The renderer calls Update () once
// per HDMI frame, after running the presenter:
//
//  - It shows from start-up.
//  - It hides once an HDMI frame shows a new Spectrum frame while the
//    frame start is locked.
//  - It shows again after StopFrames HDMI frames in a row with no new
//    Spectrum frame (the Spectrum switched off, held in reset, or the
//    board unplugged).
//
// DrawSplash () draws it in the 352 x 270 picture area, in the Spectrum's
// colours: a title bar with the four-colour stripe of the Spectrum's
// packaging, the firmware version, the HDMI mode, what the Pi has found
// so far, how config.txt was read, and what to do. The text uses an 8 x 8 font given by the caller
// (Circle's Font8x8 on the Pi), not the Sinclair ROM font.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_splash_h
#define _sppie_splash_h

#include "machine.h"
#include <stdint.h>

namespace sppie {

class CSplashState
{
public:
	static const unsigned StopFrames = 50;		// about 1 s of HDMI frames

	CSplashState (void)	{ Reset (); }

	void Reset (void)
	{
		m_bShowing = true;
		m_nIdleFrames = StopFrames;	// no frame yet
	}

	// One HDMI frame. bNewFrame: the presenter ran a new Spectrum frame.
	// bLocked: the frame start is locked. bScreenWritten: the Spectrum has
	// written to its screen memory since the Pi started or since the
	// splash last came back; until then the Pi's copy of the screen holds
	// nothing the Spectrum drew, so the splash stays.
	void Update (bool bNewFrame, bool bLocked, bool bScreenWritten)
	{
		if (bNewFrame)
		{
			m_nIdleFrames = 0;
			if (bLocked && bScreenWritten)
			{
				m_bShowing = false;
			}
			return;
		}

		if (m_nIdleFrames < StopFrames)
		{
			m_nIdleFrames++;
		}
		if (m_nIdleFrames >= StopFrames)
		{
			m_bShowing = true;
		}
	}

	bool Showing (void) const		{ return m_bShowing; }

	// A new Spectrum frame in the last StopFrames HDMI frames.
	bool FramesArriving (void) const	{ return m_nIdleFrames < StopFrames; }

private:
	bool		m_bShowing;
	unsigned	m_nIdleFrames;
};

// The HDMI frame rate, from the times at which the firmware's vertical
// sync returns: the average over the last Frames frames.
class CRefreshMeter
{
public:
	static const unsigned Frames = 50;

	CRefreshMeter (void) : m_nCount (0), m_nNext (0) {}

	void VerticalSync (uint64_t nTicks)
	{
		m_Ticks[m_nNext] = nTicks;
		m_nNext = (m_nNext + 1) % (Frames + 1);
		if (m_nCount < Frames + 1)
		{
			m_nCount++;
		}
	}

	// In 1/100 Hz, rounded; 0 until Frames frames have been timed.
	unsigned CentiHz (uint64_t nTickFrequency) const
	{
		if (m_nCount < Frames + 1)
		{
			return 0;
		}
		uint64_t nOldest = m_Ticks[m_nNext];
		uint64_t nNewest = m_Ticks[(m_nNext + Frames) % (Frames + 1)];
		uint64_t nSpan = nNewest - nOldest;
		if (nSpan == 0)
		{
			return 0;
		}
		return (unsigned) ((100ull * Frames * nTickFrequency + nSpan / 2) / nSpan);
	}

private:
	uint64_t	m_Ticks[Frames + 1];
	unsigned	m_nCount;
	unsigned	m_nNext;
};

// What the splash shows.
struct TSplashFacts
{
	const char	*pVersion;	// firmware version
	unsigned	nHdmiWidth;	// HDMI mode, 0 when not known
	unsigned	nHdmiHeight;
	unsigned	nRefreshCentiHz;	// HDMI frame rate in 1/100 Hz, 0 when not known
	bool		bLinkAlive;	// empty pairs from the CPLD
	bool		bClockSeen;	// Spectrum frames completing
	bool		bLocked;	// frame start locked
	TMachine	Machine;	// valid when bLocked
	const char	*pConfigNote;	// how config.txt was read; 0 for no line
	bool		bConfigIgnored;	// some of it was ignored: the note is red
};

// An 8 x 8 font: 8 bytes per character from nFirst to nLast, the top row
// first, bit 7 the leftmost pixel. Characters outside the range are blank.
struct TSplashFont
{
	const uint8_t	*pData;
	unsigned	nFirst;
	unsigned	nLast;
};

// The colours, 0xAARRGGBB: the Spectrum's, at 0xA0 per channel and 0xFF
// when BRIGHT.
static const uint32_t SplashPaper	= 0xFFFFFFFFu;	// bright white
static const uint32_t SplashInk		= 0xFF000000u;	// black
static const uint32_t SplashBar		= 0xFF000000u;	// black
static const uint32_t SplashTitle	= 0xFFFFFFFFu;	// bright white
static const uint32_t SplashFound	= 0xFF00A000u;	// green
static const uint32_t SplashWaiting	= 0xFFA00000u;	// red
static const uint32_t SplashPrompt	= 0xFF0000A0u;	// blue
static const uint32_t SplashStripes[4] =
{
	0xFFFF0000u, 0xFFFFFF00u, 0xFF00FF00u, 0xFF00FFFFu	// bright red, yellow, green, cyan
};

// The layout, in pixels of the 352 x 270 picture.
static const unsigned SplashBarX	= 16;	// title bar
static const unsigned SplashBarY	= 24;
static const unsigned SplashBarWidth	= 320;
static const unsigned SplashBarHeight	= 32;
static const unsigned SplashStripeBand	= 8;	// width of each colour band
static const unsigned SplashStripeX	= SplashBarX + SplashBarWidth - 8
					  - (4 * SplashStripeBand + SplashBarHeight - 1);
static const unsigned SplashTextX	= 32;
static const unsigned SplashVersionY	= 72;
static const unsigned SplashHdmiY	= 88;
static const unsigned SplashRowsY	= 116;	// what the Pi has found
static const unsigned SplashRowPitch	= 14;
static const unsigned SplashValueX	= SplashTextX + 8 * 16;
static const unsigned SplashNoteY	= 176;	// the config.txt note
static const unsigned SplashPromptY	= 196;
static const unsigned SplashFootY	= 232;	// the stripe along the foot
static const unsigned SplashFootBand	= 4;

// Draws the splash into the 352 x 270 picture at pPicture, nPitch pixels
// between rows.
void DrawSplash (const TSplashFacts &rFacts, const TSplashFont &rFont,
		 uint32_t *pPicture, unsigned nPitch);

}

#endif
