//
// overlay.h
//
// The optional on-screen instrumentation, drawn with Circle's 6 x 7 font
// into the black areas left and right of the Spectrum picture. It never
// touches the picture or its border. Built when SPPIE_OVERLAY is 1
// (Makefile option OVERLAY=1). The board has no UART, so this is the only
// way to read the counters.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_overlay_h
#define _sppie_overlay_h

#include <circle/chargenerator.h>
#include <circle/types.h>

namespace sppie {

// Values for one HDMI frame, prepared by the renderer core.
struct TFrameReport
{
	// per HDMI frame (difference since the previous one)
	unsigned	Events;
	unsigned	EmptyPairs;
	unsigned	FrameEvents;	// events with F = 1
	unsigned	IntakeBusyUs;	// intake core time spent on ring words, restarts, resyncs
	unsigned	IntakeIdleUs;	// intake core time spent polling with nothing to do
	unsigned	RenderUs;	// present + overlay + copy to the frame buffer
	unsigned	RenderIdleUs;	// flip request + wait for vertical sync

	// since boot
	unsigned	Overflows;
	unsigned	Mismatches;	// equals the number of SYNC pulses after start
	unsigned	Malformed;
	unsigned	Resets;		// reset events
	unsigned	MaxBacklogWords;
	unsigned	Restarts;
	unsigned	MaxRestartGapUs;
	unsigned	BadProducer;
	unsigned	LogDropped;	// events lost because the event log was full
	unsigned	Clamped;	// events moved to keep log positions in order
	unsigned	Acquisitions;	// frame lock acquisitions
	unsigned	Skipped;	// Spectrum frames not shown
	unsigned	Repeated;	// HDMI frames that repeated the last Spectrum frame
	unsigned	Frames;		// HDMI frames
	unsigned	SpectrumFrame;	// number of the Spectrum frame shown
	unsigned	PeriodNs;	// measured Spectrum frame period
	unsigned	Machine;	// TMachine of the frame shown
	boolean		Locked;
	unsigned	ArmClockMHz;
	boolean		NotEmpty;
};

class COverlay
{
public:
	COverlay (void);

	// Draws the counters into the side areas of the 480 x 270 display.
	void Draw (const TFrameReport &rReport, u32 *pPixels, unsigned nPitch);

private:
	void DrawText (const char *pText, unsigned nX, unsigned nY, unsigned nWidth,
		       u32 *pPixels, unsigned nPitch);

private:
	CCharGenerator m_Font;
};

}

#endif
