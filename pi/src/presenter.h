//
// presenter.h
//
// The renderer core's side of the hand-off. Once per HDMI frame it runs the
// complete Spectrum frames waiting in the event log through the Fuse ULA
// model (fuseula.h), which draws them into the picture.
//
// For each frame it selects the frame's model in Fuse, applies every event
// at its T-state (memory writes, I/O writes and resets) and ends the frame,
// at which point the picture holds that whole frame.
//
// The Spectrum frame rate (50.08 Hz on a 48K, 50.02 Hz on the 128K class)
// is not the HDMI rate (50.00 Hz), so the number of complete frames waiting
// drifts. Present () runs the oldest waiting frame. When none is waiting it
// leaves the picture as it is, so the last frame shows again. When
// SkipThreshold or more are waiting it runs the oldest ones too, until one
// is left after the one shown; those are skipped: they go through Fuse, as
// every frame must, but the picture is shown only after the last. The
// threshold gives a frame of slack, so a late vertical sync does not cause
// a skip followed by a repeat.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_presenter_h
#define _sppie_presenter_h

#include "eventlog.h"
#include "machine.h"
#include <stdint.h>

namespace sppie {

class CFramePresenter
{
public:
	static const uint32_t SkipThreshold = 3;

	explicit CFramePresenter (CEventLog *pLog);

	// Starts Fuse, drawing into the 352 x 270 picture at pPicture with
	// nPitch pixels between rows. FALSE when Fuse does not start.
	bool Initialize (uint32_t *pPicture, unsigned nPitch);

	// Clears the counters; the next frame shown is frame nFirstFrame.
	void Reset (uint32_t nFirstFrame = 0);

	// Runs the next Spectrum frame, and any skipped before it. FALSE when
	// no frame was complete: the picture is not changed.
	bool Present (void);

	uint32_t LastFrame (void) const		{ return m_nLastFrame; }
	const TFrameRecord &LastRecord (void) const { return m_LastRecord; }
	uint32_t Presented (void) const		{ return m_nPresented; }
	uint32_t Skipped (void) const		{ return m_nSkipped; }
	uint32_t Repeated (void) const		{ return m_nRepeated; }

private:
	void RunFrame (uint32_t nFrame);

private:
	CEventLog	*m_pLog;

	uint32_t	m_nNext;		// next frame to run
	uint32_t	m_nLastFrame;
	TFrameRecord	m_LastRecord;
	uint32_t	m_nPresented;
	uint32_t	m_nSkipped;
	uint32_t	m_nRepeated;
};

}

#endif
