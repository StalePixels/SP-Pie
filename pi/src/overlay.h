//
// overlay.h
//
// The optional on-screen instrumentation line, drawn into the bottom border
// with Circle's 6 x 7 font. Built when SPPIE_OVERLAY is 1 (Makefile
// option OVERLAY=1). The board has no UART, so this is the only way to read
// the counters.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_overlay_h
#define _sppie_overlay_h

#include <circle/chargenerator.h>
#include <circle/types.h>

namespace sppie {

// Values for one frame, prepared by the renderer core.
struct TFrameReport
{
	// per frame (difference since the previous frame)
	unsigned	Events;
	unsigned	EmptyPairs;
	unsigned	IntakeBusyUs;	// intake core time spent on ring words, restarts, resyncs
	unsigned	IntakeIdleUs;	// intake core time spent polling with nothing to do
	unsigned	RenderUs;	// draw + overlay + copy to the frame buffer
	unsigned	RenderIdleUs;	// flip request + wait for vertical sync

	// since boot
	unsigned	Overflows;
	unsigned	Mismatches;	// equals the number of SYNC pulses after start
	unsigned	Malformed;
	unsigned	MaxBacklogWords;
	unsigned	Restarts;
	unsigned	MaxRestartGapUs;
	unsigned	BadProducer;
	unsigned	Frames;
	unsigned	ArmClockMHz;
	boolean		NotEmpty;
};

class COverlay
{
public:
	COverlay (void);

	// Draws three text lines into the bottom border of a 320 x 240 frame.
	void Draw (const TFrameReport &rReport, u32 *pPixels, unsigned nPitch);

private:
	void DrawText (const char *pText, unsigned nX, unsigned nY, u32 *pPixels, unsigned nPitch);

private:
	CCharGenerator m_Font;
};

}

#endif
