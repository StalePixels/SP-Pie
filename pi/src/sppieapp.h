//
// sppieapp.h
//
// The work of each core.
//
//  Core 0: renderer. Once per HDMI frame it runs the next complete
//          Spectrum frames from the event log through the Fuse ULA model
//          (CFramePresenter), copies the picture to the hidden half of a
//          double-height frame buffer, moves the display to that half, and
//          waits for HDMI vertical sync. With each Spectrum frame it
//          makes the frame's sound (fusesound.h) and writes the samples
//          to the audio ring. Until the Spectrum's frames arrive, and
//          whenever they stop, it shows the splash screen (splash.h) in
//          place of the picture. Core 0 also takes Circle's peripheral
//          interrupts (the system timer).
//  Core 1: event intake. Polls the DMA producer position, decodes the new
//          ring words into pairs, rebuilds the CPLD clock count of every
//          event (CTStateClock) and hands counts, read times and events to
//          core 2 (ulaqueue.h). It restarts SMI transfers and resyncs on
//          H-bit mismatches.
//          It takes no interrupts and never waits for another core.
//  Core 2: learns the model from the clock (CUlaClock), gives each event
//          its frame and T-state (CFrameClock) and writes it to the event
//          log.
//  Core 3: audio output. Reads the audio ring through the resampler
//          (audiostream.h) and writes each sample to Circle's HDMI sound
//          device in polling mode, as fast as its FIFO takes them. With
//          no HDMI sound it returns from Run () and halts.
//
// Core 1 hands to core 2 through the queue, core 2 to core 0 through the
// event log, core 0 to core 3 through the audio ring (all lock-free, one
// writer and one reader each). The counters
// in TIntakeShared are written by cores 1 and 2 with atomic stores, each
// field by one core, and read by core 0 with atomic loads.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_sppieapp_h
#define _sppie_sppieapp_h

#include "audiostream.h"
#include "eventlog.h"
#include "frameclock.h"
#include "machine.h"
#include "presenter.h"
#include "smieventreader.h"
#include "tstateclock.h"
#include "ulaclock.h"
#include "ulaqueue.h"
#include <circle/bcmframebuffer.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/types.h>

namespace sppie {

// Cumulative counters and frame state, written only by the intake core.
struct TIntakeShared
{
	u64	Events;
	u64	EmptyPairs;
	u64	FrameEvents;
	u64	Overflows;
	u64	Mismatches;
	u64	Malformed;
	u64	Resets;
	u64	BusyTicks;		// time spent on ring words, restarts and resyncs
	u64	Restarts;		// SMI transfers started after the first
	u64	MaxRestartGapTicks;	// upper bound of the read gap at a restart
	u64	MaxBacklogWords;	// largest producer - consumer distance seen
	u64	TransferWordsOff;	// restarts where the DMA position disagreed with the
					// transfer length by more than the allowance
	u64	BadProducer;		// DMA destination samples outside the ring
	u64	LogDropped;
	u64	Clamped;
	u64	Ambiguous;		// unused (0)
	u64	ClockStopped;		// the edge-connector clock stops during contention
	u64	ReadPicoseconds;	// measured SMI read period
	u64	Port1FFDWrite;		// write that selected the +2A/+3: address << 8 | data
	u64	ResetBursts;		// resets, one per burst of reset events
	u64	CountAmbiguous;		// core 1: events whose count the 5 bits did not fix
	u64	QueueDropped;		// entries lost because core 2's queue was full
	u64	Acquisitions;
	u64	Alignments;		// frame start moves by the contention check
	u64	Aligned;		// the contention check has set the frame start
	u64	Locked;
	u64	FrameClocks;		// clocks between the F events of two frames in a row
	u64	MinFrameClocks;		// ... smallest and largest of the last 50
	u64	MaxFrameClocks;
	u64	Running;		// set once reads have started
};

class CSpPieApp : public CMultiCoreSupport
{
public:
	// pSound is the started HDMI sound device in polling mode, or 0 for
	// none. nHdmiWidth and nHdmiHeight are the HDMI mode's size as the
	// firmware gave it before the frame buffer was set up, 0 when not
	// known; the splash screen shows them. Defaults are the control-port
	// feature defaults (from config.txt); pConfigNote says how config.txt
	// was read, for the splash screen, and bConfigIgnored that some of it
	// was ignored.
	CSpPieApp (CMemorySystem *pMemorySystem, CBcmFrameBuffer *pFrameBuffer,
		   CSmiEventReader *pReader, CHDMISoundBaseDevice *pSound,
		   TMachineChoice Choice,
		   unsigned nHdmiWidth, unsigned nHdmiHeight,
		   const CControlPorts::TDefaults &rDefaults, const char *pConfigNote,
		   boolean bConfigIgnored);

	// Allocates the picture and starts Fuse. Core 0, before Initialize ().
	boolean InitializeRenderer (void);

	void Run (unsigned nCore) override;

private:
	void RunRenderer (void);
	void RunIntake (void);
	void RunUla (void);
	void RunAudio (void);
	void Publish (u64 *pField, u64 nValue);
	u64 Fetch (const u64 *pField);

private:
	CBcmFrameBuffer	*m_pFrameBuffer;
	CSmiEventReader	*m_pReader;
	CHDMISoundBaseDevice *m_pSound;
	unsigned	m_nHdmiWidth;
	unsigned	m_nHdmiHeight;
	const char	*m_pConfigNote;
	boolean		m_bConfigIgnored;

	CEventLog	m_Log;
	CFrameClock	m_FrameClock;	// core 2
	CUlaClock	m_UlaClock;	// core 2
	CUlaQueue	m_Queue;	// core 1 to core 2
	CTStateClock	m_TStateClock;	// core 1
	CAudioFifo	m_Audio;	// core 0 to core 3
	CAudioResampler	m_Resampler;	// core 3
	CFramePresenter	m_Presenter;	// core 0
	u32		*m_pDraw;	// core 0: the whole display in RAM
	u8		*m_pContention[MachineCount];	// Fuse's contention tables
	TIntakeShared	m_Shared;
};

}

#endif
