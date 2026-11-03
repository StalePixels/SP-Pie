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
// is not exactly the HDMI rate, so the number of complete frames between
// two HDMI frames varies. RunCompleted () runs every complete frame waiting,
// as soon as the caller can, so that its sound reaches the audio ring
// early. Present () does the same and then writes the picture of the
// newest frame run, so each HDMI frame shows the newest complete Spectrum
// frame. Frames run since the last Present () but not the newest are
// skipped: they go through Fuse, as every frame must, but their picture is
// never shown. When no frame has run since the last Present (), it writes
// the picture of the last frame again (a repeat).
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

	// Runs every complete Spectrum frame waiting, and writes the picture
	// of the newest frame run, by this call or by RunCompleted () since the
	// last Present (). FALSE when there was none: the picture of the last
	// frame is written again.
	bool Present (void);

	// Runs every complete Spectrum frame waiting, for its sound, and
	// writes no picture: the picture that Present () wrote last stays as it
	// is. TRUE when a frame was run.
	bool RunCompleted (void);

	// Interlace mixing (interlace.h, control-port feature 3) of the
	// picture written by the last Present (): the mode, or -1 when no
	// line is mixing; whether picture line y is mixing; and the number
	// of lines mixing. In the double high mode the picture written holds
	// the first line of each display line, and SecondPicture () the
	// second (352 x 270, PictureWidth pixels between rows); lines that do
	// not mix are the same in both. Otherwise SecondPicture () is 0. In
	// the line alternate mode the frame number of the last frame run
	// gives the order of the banks (interlace.h).
	int Mixing (void) const			{ return m_nMixing; }
	bool LineMixed (unsigned y) const	{ return y < MixLines && m_LineMixed[y]; }
	unsigned MixedLines (void) const	{ return m_nMixedLines; }
	const uint32_t *SecondPicture (void) const;

	// The frame whose picture the last Present () wrote.
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
	uint32_t	*m_pSecond;		// double high: the second lines
	static const unsigned MixLines = CInterlaceDetector::MaxLines;
	CInterlaceDetector m_Interlace;
	int		m_nMixing;
	bool		m_LineMixed[MixLines] = {};
	unsigned	m_nMixedLines = 0;

	uint32_t	m_nNext;		// next frame to run
	uint32_t	m_nRunUnshown;		// frames run since the last Present ()
	uint32_t	m_nLastFrame;		// shown
	TFrameRecord	m_LastRecord;
	uint32_t	m_nRunFrame;		// run last
	TFrameRecord	m_RunRecord;
	uint32_t	m_nPresented;
	uint32_t	m_nSkipped;
	uint32_t	m_nRepeated;
};

}

#endif
