//
// timeline.h
//
// The intake core's side of the hand-off. It puts a Spectrum frame number
// and T-state on every event and writes it to the event log.
//
// The intake core calls, for each run of new ring words:
//
//     BeginBatch (time the words were first seen)
//     ProcessWords (..., timeline, ...)      -> Event () for each event
//     EndBatch ()
//
// and Advance (now) on every pass of its loop. Every event of a batch gets
// the batch time. F events also go to the frame lock. Advance () marks a
// frame complete once the time is past its end: an event seen later has a
// later time, so it belongs to a later frame.
//
// Positions in the log never go backwards. When the frame lock moves the
// line back a little, an event that would land before the last logged
// position, or in a completed frame, gets that last position instead.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_timeline_h
#define _sppie_timeline_h

#include "eventlog.h"
#include "framelock.h"
#include "intake.h"
#include <stdint.h>

namespace sppie {

class CTimeline final : public IEventSink
{
public:
	CTimeline (CEventLog *pLog, uint64_t nTickFrequency, TMachineChoice Choice = ChooseAuto);

	// Starts the frame lock free-running and the log at frame 0.
	void Start (uint64_t nNow);

	void BeginBatch (uint64_t nStamp);
	void Event (const TEvent &rEvent) override;
	void EndBatch (void);

	void Advance (uint64_t nNow);

	const CFrameLock &Lock (void) const	{ return m_Lock; }
	uint32_t CurrentFrame (void) const	{ return m_nCompleted; }	// first incomplete frame
	uint64_t Clamped (void) const		{ return m_nClamped; }

private:
	void Locate (uint64_t nTicks, uint32_t &rFrame, uint32_t &rTState);

private:
	CEventLog	*m_pLog;
	CFrameLock	m_Lock;

	uint64_t	m_nStamp;
	bool		m_bPositioned;		// m_nFrame/m_nTState valid for this batch
	uint32_t	m_nFrame;
	uint32_t	m_nTState;

	uint32_t	m_nLastFrame;		// last logged position
	uint32_t	m_nLastTState;
	uint32_t	m_nCompleted;		// frames before this are complete
	uint64_t	m_nNextCheck;		// Advance () does nothing before this tick
	uint32_t	m_nLockVersion;		// line version m_nNextCheck was computed for
	uint64_t	m_nClamped;
};

}

#endif
