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
// The frame's sound comes from the same events (fusesound.h): the AY,
// TurboSound, beeper and Covox writes go to Fuse's sound generator at
// their T-states, and at the end of each frame, shown or skipped, its
// samples go to the audio ring for the audio core. Writes to SP-Pie's
// control ports (controlports.h) change the attached devices at their
// place in the frame, and the border extension and the HUD from the frame
// they are in; a reset restores the defaults.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_presenter_h
#define _sppie_presenter_h

#include "audiostream.h"
#include "controlports.h"
#include "eventlog.h"
#include "interlace.h"
#include "machine.h"
#include <stdint.h>

namespace sppie {

class CFramePresenter
{
public:
	static const uint32_t SkipThreshold = 3;

	// pAudio receives each frame's samples; 0 drops them.
	explicit CFramePresenter (CEventLog *pLog, CAudioFifo *pAudio = 0);

	// Starts Fuse and its sound. Fuse draws into a picture of the
	// presenter's own; each Present () writes the picture to show, mixed
	// or not, into the 352 x 270 picture at pPicture with nPitch pixels
	// between rows. FALSE when Fuse does not start or memory runs out.
	bool Initialize (uint32_t *pPicture, unsigned nPitch);

	// The feature defaults, from config.txt (bootconfig.h). They apply at
	// once and at every reset. Call before Initialize ().
	void SetDefaults (const CControlPorts::TDefaults &rDefaults);

	// Clears the counters; the next frame shown is frame nFirstFrame.
	void Reset (uint32_t nFirstFrame = 0);

	// Runs the next Spectrum frame, and any skipped before it, and writes
	// the picture to show. FALSE when no frame was complete: the picture
	// of the last frame is written again.
	bool Present (void);

	// Interlace mixing (interlace.h, control-port feature 3) of the
	// picture written by the last Present (): the mode, or -1 when not
	// mixing. In the line weave mode the picture written is the newest
	// frame, and WeavePicture () is the frame before, for the other
	// lines; otherwise WeavePicture () is 0.
	int Mixing (void) const			{ return m_nMixing; }
	const uint32_t *WeavePicture (void) const;

	uint32_t LastFrame (void) const		{ return m_nLastFrame; }
	const TFrameRecord &LastRecord (void) const { return m_LastRecord; }
	uint32_t Presented (void) const		{ return m_nPresented; }
	uint32_t Skipped (void) const		{ return m_nSkipped; }
	uint32_t Repeated (void) const		{ return m_nRepeated; }

	// The control ports as at the end of the last frame run.
	const CControlPorts &Control (void) const { return m_Control; }
	bool Hud (void) const			{ return m_Control.Hud (); }

	// A memory write to the screen, 0x4000 to 0x5AFF, has been run since
	// the start or since ClearScreenWritten ().
	bool ScreenWritten (void) const		{ return m_bScreenWritten; }
	void ClearScreenWritten (void)		{ m_bScreenWritten = false; }
	bool BorderExtended (void) const	{ return m_Control.BorderExtended (); }

private:
	void RunFrame (uint32_t nFrame);
	void WritePicture (void);

private:
	CEventLog	*m_pLog;
	CAudioFifo	*m_pAudio;
	CControlPorts	m_Control;
	bool		m_bScreenWritten = false;

	uint32_t	*m_pOutput;		// where the picture to show goes
	unsigned	m_nOutputPitch;
	uint32_t	*m_pFuse;		// Fuse's picture: the newest frame
	uint32_t	*m_pPrevious;		// the frame before it
	CInterlaceDetector m_Interlace;
	int		m_nMixing;

	uint32_t	m_nNext;		// next frame to run
	uint32_t	m_nLastFrame;
	TFrameRecord	m_LastRecord;
	uint32_t	m_nPresented;
	uint32_t	m_nSkipped;
	uint32_t	m_nRepeated;
};

}

#endif
