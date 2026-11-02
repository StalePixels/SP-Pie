//
// timeline.h
//
// The intake core's side of the hand-off. It puts a Spectrum frame number
// and T-state on every event and writes it to the event log.
//
// The intake core calls StartTransfer () each time it starts an SMI
// transfer, and for each run of new ring words:
//
//     BeginBatch (word number of the first word, time the words were seen)
//     ProcessWords (..., timeline, ...)      -> Event () for each event
//     EndBatch ()
//
// and Advance (time of its last producer sample) on every pass of its
// loop, after it has handed over every word up to that sample. Each event's
// time is the read time of its ring word, from the word's position (see
// ringclock.h), so it does not depend on how late the core sees the word.
// F events also go to the frame lock. Advance () marks a frame complete once
// the sample time, less SeenMarginNs for words still in the SMI FIFO or the
// DMA, is past the frame's end: every word read before then has been seen.
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
#include "ringclock.h"
#include <stddef.h>
#include <stdint.h>

namespace sppie {

class CTimeline final : public IEventSink
{
public:
	CTimeline (CEventLog *pLog, uint64_t nTickFrequency, TMachineChoice Choice = ChooseAuto);

	// Starts the frame lock free-running and the log at frame 0.
	void Start (uint64_t nNow);

	// The SMI transfer whose first ring word is word number nWord started
	// at nTicks.
	void StartTransfer (uint64_t nWord, uint64_t nTicks);

	// The next run of words starts at word number nFirstWord; the core saw
	// them at nSeen.
	void BeginBatch (uint64_t nFirstWord, uint64_t nSeen);
	void Event (const TEvent &rEvent, size_t nWord) override;
	void EndBatch (void);

	// Every word read up to nSeen - SeenMarginNs has been given to Event ().
	void Advance (uint64_t nSeen);

	static const uint32_t SeenMarginNs = 2000;

	const CFrameLock &Lock (void) const	{ return m_Lock; }
	uint32_t CurrentFrame (void) const	{ return m_nCompleted; }	// first incomplete frame
	uint64_t Clamped (void) const		{ return m_nClamped; }

private:
	void Locate (uint64_t nTicks, uint32_t &rFrame, uint32_t &rTState);

private:
	CEventLog	*m_pLog;
	CFrameLock	m_Lock;
	CRingClock	m_Clock;

	uint64_t	m_nFirstWord;		// word number of the run's first word
	uint64_t	m_nSeen;

	uint32_t	m_nLastFrame;		// last logged position
	uint32_t	m_nLastTState;
	uint32_t	m_nCompleted;		// frames before this are complete
	uint64_t	m_nNextCheck;		// Advance () does nothing before this tick
	uint32_t	m_nLockVersion;		// line version m_nNextCheck was computed for
	uint64_t	m_nMarginTicks;
	uint64_t	m_nClamped;
};

}

#endif
