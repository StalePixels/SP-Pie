//
// presenter.h
//
// The renderer core's side of the hand-off. Once per HDMI frame it draws
// one complete Spectrum frame from the event log, or keeps the last one.
//
// The Spectrum frame rate (50.08 Hz on a 48K, 50.02 Hz on the 128K class)
// is not the HDMI rate (50.00 Hz), so the number of complete frames waiting
// drifts. Present () draws the oldest waiting frame. It repeats the last
// frame when none is waiting, and when SkipThreshold or more are waiting it
// applies the oldest ones without drawing them until one is left after the
// drawn one. The threshold gives a frame of slack, so a late vertical sync
// does not cause a skip followed by a repeat. A frame is always drawn from
// start to end; the display never mixes two frames.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_presenter_h
#define _sppie_presenter_h

#include "eventlog.h"
#include "renderer.h"
#include <stdint.h>

namespace sppie {

class CFramePresenter
{
public:
	static const uint32_t SkipThreshold = 3;

	explicit CFramePresenter (CEventLog *pLog);

	// Clears the state; the next frame shown is frame nFirstFrame.
	void Reset (uint32_t nFirstFrame = 0);

	// Draws the next Spectrum frame into the 352 x 270 picture at pPixels.
	// FALSE when no frame was complete: pPixels is not changed.
	bool Present (uint32_t *pPixels, unsigned nPitch);

	CRenderState &RenderState (void)	{ return m_State; }
	const CRenderState &RenderState (void) const { return m_State; }
	uint32_t LastFrame (void) const		{ return m_nLastFrame; }
	const TFrameRecord &LastRecord (void) const { return m_LastRecord; }
	uint32_t Presented (void) const		{ return m_nPresented; }
	uint32_t Skipped (void) const		{ return m_nSkipped; }
	uint32_t Repeated (void) const		{ return m_nRepeated; }

private:
	CEventLog	*m_pLog;
	CRenderState	m_State;

	uint32_t	m_nNext;		// next frame to show
	uint32_t	m_nLastFrame;
	TFrameRecord	m_LastRecord;
	uint32_t	m_nPresented;
	uint32_t	m_nSkipped;
	uint32_t	m_nRepeated;
};

}

#endif
