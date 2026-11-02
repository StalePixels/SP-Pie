//
// test_main.cpp
//
// Host tests for the SP-Pie video service. They build the same intake,
// clock, frame and presentation code the Pi runs, and the Fuse ULA model
// with our layer, and check results down to the pixel.
//
// The timed tests use a simulated CPLD: Z80 writes at known clock counts, a
// four-entry queue that drops events when full, a two-read delay before
// the read side sees a new event, empty pairs that take the count at their
// load, and SMI reads every 200 ns with stops between transfers.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "../src/event.h"
#include "../src/eventlog.h"
#include "../src/frameclock.h"
#include "../src/intake.h"
#include "../src/machine.h"
#include "../src/picture.h"
#include "../src/presenter.h"
#include "../src/tstateclock.h"
#include "../src/ulaclock.h"
#include "../src/ulaqueue.h"
#include "../fuseula/fuseula.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace sppie;

// --- test harness ---------------------------------------------------------

static unsigned s_nChecks;
static unsigned s_nFailures;
static bool s_bTestFailed;

#define CHECK(cond)							\
	do {								\
		s_nChecks++;						\
		if (!(cond)) {						\
			s_nFailures++;					\
			s_bTestFailed = true;				\
			printf ("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}							\
	} while (0)

#define CHECK_EQ(a, b)							\
	do {								\
		s_nChecks++;						\
		unsigned long long _a = (unsigned long long) (a);	\
		unsigned long long _b = (unsigned long long) (b);	\
		if (_a != _b) {						\
			s_nFailures++;					\
			s_bTestFailed = true;				\
			printf ("    FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", \
				__FILE__, __LINE__, #a, #b, _a, _b);	\
		}							\
	} while (0)

typedef void TTestFunc (void);

static unsigned s_nTests;
static unsigned s_nTestsFailed;

static void Run (const char *pName, TTestFunc *pFunc)
{
	s_bTestFailed = false;
	pFunc ();
	s_nTests++;
	if (s_bTestFailed)
	{
		s_nTestsFailed++;
	}
	printf ("%s %s\n", s_bTestFailed ? "FAIL" : "ok  ", pName);
}

// --- colours and picture -------------------------------------------------

static uint32_t Colour (unsigned nIndex)	// BRIGHT * 8 + G R B
{
	unsigned nLevel = (nIndex & 8) ? 0xFF : 0xA0;
	return   0xFF000000u | ((nIndex & 2) ? nLevel << 16 : 0)
	       | ((nIndex & 4) ? nLevel << 8 : 0) | ((nIndex & 1) ? nLevel : 0);
}

static const uint32_t Black = Colour (0);
static const uint32_t Blue = Colour (1);
static const uint32_t Red = Colour (2);
static const uint32_t Green = Colour (4);
static const uint32_t Yellow = Colour (6);
static const uint32_t White = Colour (7);

struct CTestPicture
{
	uint32_t	Pixels[PictureWidth * PictureHeight];

	uint32_t At (unsigned x, unsigned y) const { return Pixels[y * PictureWidth + x]; }

	// Pixel (x, y) of the 256 x 192 screen.
	uint32_t Screen (unsigned x, unsigned y) const { return At (48 + x, 39 + y); }

	// First pixel of 8-pixel chunk p (0 to 43) on picture line q.
	uint32_t Chunk (unsigned p, unsigned q) const { return At (8 * p, q); }
};

static std::unique_ptr<CTestPicture> NewPicture (void)
{
	std::unique_ptr<CTestPicture> pPicture (new CTestPicture);
	memset (pPicture->Pixels, 0, sizeof pPicture->Pixels);
	CHECK_EQ (fuseula_init (pPicture->Pixels, PictureWidth), 0);
	return pPicture;
}

// Fuse's beam rule, extended over the picture: the first T-state of chunk
// p on picture line q. The chunk shows the state at that T-state + 3.
static uint32_t ChunkTState (unsigned p, unsigned q)
{
	return   fuseula_top_left_border_tstate ()
	       + (q - 15) * fuseula_line_tstates () + 4 * (p - 2);
}

// Screen memory offsets (in bank 5 or 7).
static uint16_t PixelOffset (unsigned y, unsigned nCol)
{
	return (uint16_t) ((((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2)) + nCol);
}

static uint16_t AttrOffset (unsigned y, unsigned nCol)
{
	return (uint16_t) (0x1800 + (y >> 3) * 32 + nCol);
}

// Clears the shown screen (bank 5 at 0x4000) and sets the border, at
// T-state 0 of the frame.
static void ClearScreen (uint8_t nAttr, uint8_t nBorder)
{
	for (unsigned i = 0; i < 0x1800; i++)
	{
		fuseula_memory_write (0, (uint16_t) (0x4000 + i), 0);
	}
	for (unsigned i = 0; i < 0x300; i++)
	{
		fuseula_memory_write (0, (uint16_t) (0x5800 + i), nAttr);
	}
	fuseula_io_write (0, 0x00FE, nBorder);
}

// --- decoder and intake --------------------------------------------------

static void TestGrayCode (void)
{
	for (unsigned i = 0; i < CountModulus; i++)
	{
		uint8_t g = BinaryToGray ((uint8_t) i);
		uint8_t gn = BinaryToGray ((uint8_t) ((i + 1) & CountMask));
		CHECK_EQ (GrayToBinary (g), i);
		unsigned nDiff = g ^ gn;
		CHECK (nDiff != 0 && (nDiff & (nDiff - 1)) == 0);	// one bit changes
	}
}

static void TestDecoderEvent (void)
{
	uint32_t w[2];
	EncodeEvent (w, KindIo, 0xC123, 0x5A, 0x13, true, true);

	// Gray 0x13 = 0b11010: T4 = 1, T3 = 1, T2 = 0, T1 = 1, T0 = 0.
	CHECK_EQ (BinaryToGray (0x13), 0x1A);
	CHECK_EQ (w[0], (1u << 17) | (1u << 16) | 0x4000 | 0x2000 | 0x1000 | 0x0400 | 0x0100 | 0x5A);
	CHECK_EQ (w[1], (1u << 17) | 0x8000 | 0x4123);

	TEvent Event;
	CHECK_EQ (DecodePair (w[0], w[1], Event), PairEvent);
	CHECK_EQ (Event.Kind, KindIo);
	CHECK_EQ (Event.Address, 0xC123);
	CHECK_EQ (Event.Data, 0x5A);
	CHECK_EQ (Event.Count, 0x13);
	CHECK (Event.Overflow);
	CHECK (Event.Frame);

	for (unsigned c = 0; c < CountModulus; c++)
	{
		EncodeEvent (w, KindMemory, 0x4000, 0, (uint8_t) c);
		CHECK_EQ (DecodePair (w[0], w[1], Event), PairEvent);
		CHECK_EQ (Event.Count, c);
	}
}

static void TestDecoderEmptyAndMalformed (void)
{
	uint32_t w[2];
	TEvent Event;

	for (unsigned c = 0; c < CountModulus; c++)
	{
		EncodeEmpty (w, (uint8_t) c);
		CHECK_EQ (w[1], 0x08000u);
		CHECK_EQ (DecodePair (w[0], w[1], Event), PairEmpty);
		CHECK_EQ (Event.Count, c);
	}

	EncodeEmpty (w, 5);
	CHECK_EQ (DecodePair (w[0] | (1u << 9), w[1], Event), PairMalformed);
	CHECK_EQ (DecodePair (w[0] | (1u << 16), w[1], Event), PairMalformed);
	CHECK_EQ (DecodePair (w[0], w[1] | (1u << 16), Event), PairMalformed);
	CHECK_EQ (DecodePair (w[0], w[1] | 1, Event), PairMalformed);
	CHECK_EQ (DecodePair (w[0] | 0x8000, w[1], Event), PairMismatch);
	CHECK_EQ (DecodePair (w[0], 0, Event), PairMismatch);

	EncodeEvent (w, KindReserved, 0x1234, 0, 0);
	CHECK_EQ (DecodePair (w[0], w[1], Event), PairMalformed);
	EncodeEvent (w, KindReset, 0x0001, 0, 0);
	CHECK_EQ (DecodePair (w[0], w[1], Event), PairMalformed);
	EncodeEvent (w, KindReset, 0, 0, 7);
	CHECK_EQ (DecodePair (w[0], w[1], Event), PairEvent);
}

class CRecordSink : public IEventSink
{
public:
	void Empty (uint8_t nCount, uint64_t nWord) override
	{
		Empties.push_back (nCount);
		EmptyWords.push_back (nWord);
	}
	void Event (const TEvent &rEvent, uint64_t nWord) override
	{
		Events.push_back (rEvent);
		EventWords.push_back (nWord);
	}

	std::vector<uint8_t> Empties;
	std::vector<uint64_t> EmptyWords;
	std::vector<TEvent> Events;
	std::vector<uint64_t> EventWords;
};

static void TestIntakePairsAcrossRuns (void)
{
	std::vector<uint32_t> Words (2 * 6);
	EncodeEmpty (&Words[0], 0);		// first pair after SYNC: skipped
	EncodeEmpty (&Words[2], 3);
	EncodeEvent (&Words[4], KindMemory, 0x5800, 0x47, 4, false, true);
	EncodeEvent (&Words[6], KindReset, 0, 0, 9, true);
	EncodeEmpty (&Words[8], 0x1F);
	EncodeEmpty (&Words[10], 0);

	CRecordSink Sink;
	TIntakeCounters Counters;
	memset (&Counters, 0, sizeof Counters);
	TIntakeState State;
	ResetIntake (State);

	// Runs of odd lengths, so pairs straddle them.
	size_t nPos = 0;
	const size_t Runs[] = { 1, 3, 2, 5, 1 };
	for (size_t nRun : Runs)
	{
		bool bMismatch = false;
		size_t nUsed = ProcessWords (&Words[nPos], nRun, 100 + nPos, Sink, State, Counters,
					     bMismatch);
		CHECK (!bMismatch);
		CHECK_EQ (nUsed, nRun);
		nPos += nRun;
	}

	CHECK_EQ (Sink.Empties.size (), 3u);
	CHECK_EQ (Sink.Empties[0], 3);
	CHECK_EQ (Sink.Empties[1], 0x1F);
	CHECK_EQ (Sink.EmptyWords[0], 102u);	// word number of the pair's R0
	CHECK_EQ (Sink.Events.size (), 2u);
	CHECK_EQ (Sink.Events[0].Address, 0x5800);
	CHECK_EQ (Sink.Events[0].Count, 4);
	CHECK_EQ (Sink.EventWords[1], 106u);
	CHECK_EQ (Counters.Events, 2u);
	CHECK_EQ (Counters.EmptyPairs, 3u);
	CHECK_EQ (Counters.FrameEvents, 1u);
	CHECK_EQ (Counters.Overflows, 1u);
	CHECK_EQ (Counters.Resets, 1u);
}

static void TestIntakeMismatchStops (void)
{
	std::vector<uint32_t> Words (8);
	EncodeEmpty (&Words[0], 0);
	EncodeEmpty (&Words[2], 1);
	Words[4] = 0x8000;			// R0 position with H = 1
	EncodeEmpty (&Words[5], 2);

	CRecordSink Sink;
	TIntakeCounters Counters;
	memset (&Counters, 0, sizeof Counters);
	TIntakeState State;
	ResetIntake (State);
	bool bMismatch = false;
	size_t nUsed = ProcessWords (&Words[0], 7, 0, Sink, State, Counters, bMismatch);
	CHECK (bMismatch);
	CHECK_EQ (nUsed, 5u);
	CHECK_EQ (Counters.Mismatches, 1u);
	CHECK_EQ (Sink.Empties.size (), 1u);
}

// --- simulated CPLD -------------------------------------------------------

static const uint64_t TickHz = 19200000;	// the Pi's generic timer
static double ReadNs = 200.0;	// the simulated SMI read period
static double ClockNs = 1e9 / 3546900.0;	// the simulated Z80 clock

static uint64_t CountAt (double nNs)
{
	return (uint64_t) (nNs / ClockNs);
}

static double CountNs (uint64_t nCount)	// the middle of clock nCount
{
	return ((double) nCount + 0.5) * ClockNs;
}

static uint64_t Ticks (double nNs)
{
	return (uint64_t) (nNs * (double) TickHz / 1e9);
}

struct TSimWrite
{
	uint64_t	Count;		// clock count at the end of the write
	uint8_t		Kind;
	uint16_t	Address;
	uint8_t		Data;
	bool		Frame;		// first write after an acknowledge
	double		Ns = 0;		// when /WR rises; 0: from Count
};

struct TSimStop
{
	double		AtNs;		// reads stop at the first pair boundary after this
	double		LengthNs;
};

class CCpldSim
{
public:
	static const unsigned Depth = 4;

	struct TStored
	{
		uint64_t	Count;
		TSimWrite	Write;
		bool		Overflow;
	};

	struct TTransfer
	{
		uint64_t	Word;
		double		StartNs;
	};

	void Run (const std::vector<TSimWrite> &rWrites, const std::vector<TSimStop> &rStops,
		  double nEndNs)
	{
		struct TEntry
		{
			TSimWrite	Write;
			bool		Overflow;
			bool		Frame;
			double		VisibleNs;
		};
		std::deque<TEntry> Queue;
		bool bOverflow = false, bFrame = false;

		size_t nWrite = 0, nStop = 0;
		uint32_t nLoaded = 0;		// the empty R0 after SYNC: count 0
		bool bR1 = false, bCurrent = false;
		TEntry Current = {};
		uint32_t w[2];

		Transfers.push_back ({ 0, 0.0 });
		for (double nEnd = ReadNs; nEnd < nEndNs; nEnd += ReadNs)
		{
			// Writes that end before this read ends.
			while (nWrite < rWrites.size () && WriteNs (rWrites[nWrite]) < nEnd)
			{
				const TSimWrite &rWrite = rWrites[nWrite++];
				bFrame = bFrame || rWrite.Frame;
				if (Queue.size () < Depth)
				{
					Queue.push_back ({ rWrite, bOverflow, bFrame,
							   WriteNs (rWrite) + 2 * ReadNs });
					Stored.push_back ({ rWrite.Count, rWrite, bOverflow });
					bOverflow = bFrame = false;
				}
				else
				{
					bOverflow = true;
				}
			}

			Words.push_back (nLoaded);
			WordNs.push_back (nEnd);

			// The SOE rising edge at nEnd loads the next read word.
			if (!bR1)
			{
				if (bCurrent)
				{
					const TSimWrite &rW = Current.Write;
					EncodeEvent (w, rW.Kind, rW.Address, rW.Data,
						     (uint8_t) (rW.Count & CountMask),
						     Current.Overflow, Current.Frame);
					nLoaded = w[1];
				}
				else
				{
					nLoaded = EmptyR1;
				}
				bR1 = true;
				continue;
			}

			bR1 = false;
			if (bCurrent)
			{
				Queue.pop_front ();
			}
			bCurrent = !Queue.empty () && Queue.front ().VisibleNs <= nEnd;
			if (bCurrent)
			{
				Current = Queue.front ();
				const TSimWrite &rW = Current.Write;
				EncodeEvent (w, rW.Kind, rW.Address, rW.Data,
					     (uint8_t) (rW.Count & CountMask),
					     Current.Overflow, Current.Frame);
				nLoaded = w[0];
			}
			else
			{
				EncodeEmpty (w, (uint8_t) (CountAtNs (nEnd) & CountMask));
				nLoaded = w[0];
			}

			// Transfers end on pair boundaries. The Pi times the start of
			// the next transfer when it asks for it; the first read can
			// come up to StartJitterNs later.
			if (nStop < rStops.size () && nEnd >= rStops[nStop].AtNs)
			{
				nEnd += rStops[nStop].LengthNs;
				Transfers.push_back ({ Words.size (), nEnd });
				nJitterSeed = nJitterSeed * 1103515245u + 12345u;
				nEnd += StartJitterNs * (double) ((nJitterSeed >> 16) % 1000) / 1000.0;
				nStop++;
			}
		}
		EndNs = nEndNs;
	}

	static double WriteNs (const TSimWrite &rWrite)
	{
		return rWrite.Ns != 0 ? rWrite.Ns : CountNs (rWrite.Count);
	}

	// The CPLD's count at a moment: the ULA's T-states, or fewer where the
	// clock stops.
	std::function<uint64_t (double)> CountAtNs = CountAt;

	double			StartJitterNs = 0;
	uint32_t		nJitterSeed = 1;

	std::vector<uint32_t>	Words;
	std::vector<double>	WordNs;		// when each read ended
	std::vector<TTransfer>	Transfers;
	std::vector<TStored>	Stored;
	double			EndNs = 0;
};

// Feeds the simulated words to the intake core's code, in batches, with
// each transfer start, as the Pi's intake loop does.
static void Feed (const CCpldSim &rSim, CTStateClock &rClock, TIntakeCounters &rCounters,
		  size_t nBatch = 97, std::function<void (void)> AfterBatch = nullptr)
{
	TIntakeState State;
	ResetIntake (State);
	memset (&rCounters, 0, sizeof rCounters);

	size_t nTransfer = 0;
	size_t nPos = 0;
	while (nPos < rSim.Words.size ())
	{
		while (nTransfer < rSim.Transfers.size () && rSim.Transfers[nTransfer].Word <= nPos)
		{
			rClock.StartTransfer (rSim.Transfers[nTransfer].Word,
					      Ticks (rSim.Transfers[nTransfer].StartNs));
			nTransfer++;
		}
		size_t nEnd = nPos + nBatch;
		if (nEnd > rSim.Words.size ())
		{
			nEnd = rSim.Words.size ();
		}
		if (nTransfer < rSim.Transfers.size () && rSim.Transfers[nTransfer].Word < nEnd)
		{
			nEnd = rSim.Transfers[nTransfer].Word;
		}
		// The batch is seen 1 us after its last word was read.
		rClock.BeginBatch (Ticks (rSim.WordNs[nEnd - 1] + 1000), nEnd);
		bool bMismatch = false;
		ProcessWords (&rSim.Words[nPos], nEnd - nPos, nPos, rClock, State, rCounters, bMismatch);
		CHECK (!bMismatch);
		nPos = nEnd;
		if (AfterBatch)
		{
			AfterBatch ();
		}
	}
}

class CCountSink : public ITimedEventSink
{
public:
	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override
	{
		Events.push_back (rEvent);
		Counts.push_back (nCount);
	}
	void Clock (uint64_t nCount, double nTicks) override
	{
		(void) nTicks;
		Last = nCount;
	}

	std::vector<TEvent> Events;
	std::vector<uint64_t> Counts;
	uint64_t Last = 0;
};

// Every stored event gets its true count, up to one constant (the start of
// the Pi's count).
static void CheckCounts (const CCpldSim &rSim, const CCountSink &rSink, unsigned nSkip = 0)
{
	CHECK_EQ (rSink.Counts.size (), rSim.Stored.size ());
	if (rSink.Counts.size () != rSim.Stored.size () || rSim.Stored.empty ())
	{
		return;
	}
	uint64_t nOffset = rSink.Counts[0] - rSim.Stored[0].Count;
	unsigned nWrong = 0;
	for (size_t i = nSkip; i < rSim.Stored.size (); i++)
	{
		if (rSink.Counts[i] - rSim.Stored[i].Count != nOffset)
		{
			nWrong++;
		}
		CHECK_EQ (rSink.Events[i].Address, rSim.Stored[i].Write.Address);
		CHECK_EQ (rSink.Events[i].Overflow, rSim.Stored[i].Overflow);
	}
	CHECK_EQ (nWrong, 0u);
}

static void TestCountSteadyStream (void)
{
	// LDIR-like writes every 21 clocks and PUSH pairs 3 clocks apart, for
	// 10 ms.
	std::vector<TSimWrite> Writes;
	for (uint64_t c = 100; c < 35000; c += 21)
	{
		Writes.push_back ({ c, KindMemory, (uint16_t) (0x4000 + c % 0x1800), 1, false });
	}
	for (uint64_t c = 200; c < 35000; c += 400)
	{
		Writes.push_back ({ c + 7, KindMemory, 0xFFF0, 2, false });
		Writes.push_back ({ c + 10, KindMemory, 0xFFEF, 3, false });
	}
	std::sort (Writes.begin (), Writes.end (),
		   [] (const TSimWrite &a, const TSimWrite &b) { return a.Count < b.Count; });

	CCpldSim Sim;
	Sim.Run (Writes, {}, 10e6);

	CCountSink Sink;
	CTStateClock Clock (&Sink, TickHz, 3546900);
	TIntakeCounters Counters;
	Feed (Sim, Clock, Counters);

	CheckCounts (Sim, Sink);
	CHECK_EQ (Clock.Ambiguous (), 0u);
	CHECK_EQ (Counters.Overflows, 0u);
}

static void TestCountFullQueueAndRestartGap (void)
{
	// Reads stop for 5 us (a transfer restart) while six writes come: the
	// queue keeps four, drops two, and the next event carries OVF. A later
	// stop of 300 us has no writes in it; the read times count its wraps.
	std::vector<TSimWrite> Writes;
	double nStop1 = 2e6, nStop2 = 4e6;
	uint64_t c1 = CountAt (nStop1 + 400);
	for (unsigned i = 0; i < 6; i++)
	{
		Writes.push_back ({ c1 + 3 * i, KindMemory, (uint16_t) (0x5800 + i), (uint8_t) i, false });
	}
	Writes.push_back ({ CountAt (nStop1 + 9000), KindIo, 0x00FE, 2, false });
	Writes.push_back ({ CountAt (nStop2 + 400e3), KindMemory, 0x4000, 9, false });
	for (uint64_t c = 50; c < 13000; c += 37)
	{
		Writes.push_back ({ c, KindMemory, 0x6000, 0, false });
	}
	std::sort (Writes.begin (), Writes.end (),
		   [] (const TSimWrite &a, const TSimWrite &b) { return a.Count < b.Count; });

	CCpldSim Sim;
	Sim.Run (Writes, { { nStop1, 5000 }, { nStop2, 300e3 } }, 6e6);
	CHECK_EQ (Sim.Transfers.size (), 3u);

	unsigned nDropped = (unsigned) (Writes.size () - Sim.Stored.size ());
	CHECK_EQ (nDropped, 2u);

	CCountSink Sink;
	CTStateClock Clock (&Sink, TickHz, 3546900);
	TIntakeCounters Counters;
	Feed (Sim, Clock, Counters);

	CheckCounts (Sim, Sink);
	CHECK_EQ (Counters.Overflows, 1u);
	CHECK_EQ (Clock.Ambiguous (), 0u);
}

static void TestCountLongWaitIsReported (void)
{
	// An event captured at the start of a 20 us stop waits more than 32
	// clocks. It is counted as ambiguous, and the events read later are
	// placed exactly.
	double nStop = 1e6;
	std::vector<TSimWrite> Writes;
	Writes.push_back ({ CountAt (nStop + 1000), KindMemory, 0x4001, 1, false });
	for (uint64_t c = CountAt (nStop + 40e3); c < CountAt (2e6); c += 50)
	{
		Writes.push_back ({ c, KindMemory, 0x4002, 2, false });
	}

	CCpldSim Sim;
	Sim.Run (Writes, { { nStop, 20e3 } }, 2.5e6);

	CCountSink Sink;
	CTStateClock Clock (&Sink, TickHz, 3546900);
	TIntakeCounters Counters;
	Feed (Sim, Clock, Counters);

	CHECK (Clock.Ambiguous () >= 1);
	CHECK_EQ (Sink.Counts.size (), Sim.Stored.size ());
	if (Sink.Counts.size () == Sim.Stored.size () && Sim.Stored.size () > 2)
	{
		uint64_t nOffset = Sink.Counts[1] - Sim.Stored[1].Count;
		unsigned nWrong = 0;
		for (size_t i = 1; i < Sim.Stored.size (); i++)
		{
			nWrong += Sink.Counts[i] - Sim.Stored[i].Count != nOffset;
		}
		CHECK_EQ (nWrong, 0u);
	}
}

// --- frame clock ----------------------------------------------------------

static const uint32_t *FrameLengths (void)
{
	static uint32_t Lengths[MachineCount];
	for (unsigned i = 0; i < MachineCount; i++)
	{
		Lengths[i] = fuseula_frame_tstates ((fuseula_machine) i);
	}
	return Lengths;
}

static TEvent MakeEvent (uint8_t nKind, uint16_t nAddress, uint8_t nData, bool bFrame = false)
{
	TEvent Event;
	Event.Kind = nKind;
	Event.Address = nAddress;
	Event.Data = nData;
	Event.Overflow = false;
	Event.Frame = bFrame;
	Event.Count = 0;
	return Event;
}

// The logged events of completed frame nFrame.
static std::vector<TTimedEvent> TakeFrame (CEventLog &rLog, uint32_t nFrame)
{
	std::vector<TTimedEvent> Events;
	TFrameSpan Span = rLog.TakeFrame (nFrame);
	for (uint32_t i = 0; i < Span.nCount; i++)
	{
		Events.push_back (Span.At (i));
	}
	rLog.Release (Span);
	return Events;
}

static void TestFrameStartFromEarliestAck (void)
{
	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
	const uint64_t S = 5000000;
	const uint32_t L = 70908;
	// Acknowledge delays per frame: the third frame acknowledges at once.
	const uint32_t Delay[] = { 7, 4, 0, 11, 3, 0, 19, 2 };

	for (unsigned n = 0; n < 8; n++)
	{
		uint64_t nFrameStart = S + n * L;
		Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0x12, true),
				   nFrameStart + FrameEventTStates + Delay[n]);
		Frames.TimedEvent (MakeEvent (KindMemory, 0x5800, n), nFrameStart + 20000);
		Frames.TimedEvent (MakeEvent (KindIo, 0x00FE, n), nFrameStart + 30000);
		Frames.Clock (nFrameStart + L + 10, 0.0);
	}

	CHECK (Frames.Locked ());
	CHECK_EQ (Frames.Machine (), Machine128K);
	CHECK_EQ (Frames.LastFrameClocks (), L + Delay[7] - Delay[6]);
	CHECK_EQ (Frames.MinFrameClocks (), L - 17);
	CHECK_EQ (Frames.MaxFrameClocks (), L + 19);

	// From the frame with the earliest acknowledge on, every write is at
	// its exact T-state: memory writes at their count, I/O writes 3
	// T-states earlier (Fuse's I/O write point).
	uint32_t nCompleted = pLog->Completed ();
	CHECK (nCompleted >= 7);
	unsigned nExact = 0;
	for (uint32_t f = 0; f < nCompleted; f++)
	{
		std::vector<TTimedEvent> Events = TakeFrame (*pLog, f);
		for (const TTimedEvent &rE : Events)
		{
			if (rE.Kind == KindMemory && rE.Address == 0x5800 && rE.Data >= 2)
			{
				CHECK_EQ (rE.TState, 20000u);
				nExact++;
			}
			if (rE.Kind == KindIo && rE.Data >= 2)
			{
				CHECK_EQ (rE.TState, 30000u - 3);
			}
			if (rE.Kind == KindMemory && rE.Address == 0xFFFE && rE.Data == 0x12 && f >= 3)
			{
				CHECK (rE.TState >= FrameEventTStates);
			}
		}
	}
	CHECK (nExact >= 5);
	CHECK_EQ (Frames.FrameStart (Frames.CompletedFrames ()) % L, S % L);
}

static void TestMachineDetection (void)
{
	// 48K frame length, then a +3's port 0x1FFD write in a 128K-class
	// stream; and the cmdline choices.
	{
		// The class comes from the ULA clock's frame period.
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
		Frames.SetClockFacts (Machine48K, false);
		for (unsigned n = 0; n < 12; n++)
		{
			uint64_t s = 1000000 + (uint64_t) n * 69888;
			Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0, true), s + 10 + (n % 3));
			Frames.Clock (s + 69888 + 10, 0.0);
		}
		CHECK_EQ (Frames.Machine (), Machine48K);
		CHECK_EQ (Frames.FrameTStates (), 69888u);
		CHECK_EQ (pLog->Record (pLog->Completed () - 1).Machine, Machine48K);
	}
	{
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
		for (unsigned n = 0; n < 6; n++)
		{
			uint64_t s = 1000000 + (uint64_t) n * 70908;
			Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0, true), s + 10);
			if (n == 3)
			{
				Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), s + 500);
			}
			Frames.Clock (s + 70908 + 10, 0.0);
			CHECK_EQ (Frames.Machine (), n < 3 ? Machine128K : MachinePlus3);
		}
		CHECK_EQ (Frames.FrameTStates (), 70908u);
	}
	{
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto128);
		Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), 1000);
		CHECK_EQ (Frames.Machine (), Machine128K);
	}
	{
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), Choose48K);
		CHECK_EQ (Frames.Machine (), Machine48K);
		CHECK_EQ (Frames.FrameTStates (), 69888u);
	}
	{
		// A 128K whose clock stops: its own 0x1FFD write (the Bifrost* demo
		// writes 0x04 there) does not make it a +2A/+3.
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
		Frames.SetClockFacts (Machine128K, false);
		Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), 1000);
		CHECK_EQ (Frames.Machine (), Machine128K);
		Frames.SetClockFacts (Machine128K, true);	// after a reset, on a +3
		Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), 2000);
		CHECK_EQ (Frames.Machine (), MachinePlus3);
	}
	{
		// A clock that stops during contention: frames shorter than both
		// lengths do not change the class.
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
		uint64_t s = 1000000;
		for (unsigned n = 0; n < 10; n++)
		{
			Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0, true), s + 10);
			s += 70908 - 3000 - 200 * n;
		}
		CHECK_EQ (Frames.Machine (), Machine128K);
		CHECK (Frames.MaxFrameClocks () < 70908 - 2000);
	}
}

// --- the Fuse ULA model ---------------------------------------------------

static void TestFuseStillPicture (void)
{
	auto pPicture = NewPicture ();
	ClearScreen (0x38, 1);			// paper white, ink black; border blue
	fuseula_memory_write (0, 0x4000 + PixelOffset (0, 0), 0xF0);
	fuseula_memory_write (0, 0x4000 + AttrOffset (0, 0), 0x02 | 0x20);	// ink red, paper green
	fuseula_end_frame ();

	CHECK_EQ (pPicture->Screen (0, 0), Red);
	CHECK_EQ (pPicture->Screen (3, 0), Red);
	CHECK_EQ (pPicture->Screen (4, 0), Green);
	CHECK_EQ (pPicture->Screen (8, 0), White);
	CHECK_EQ (pPicture->Screen (0, 1), Green);
	CHECK_EQ (pPicture->At (0, 0), Blue);			// outer border, top left
	CHECK_EQ (pPicture->At (351, 269), Blue);		// outer border, bottom right
	CHECK_EQ (pPicture->At (20, 100), Blue);		// Fuse's left border
	CHECK_EQ (pPicture->At (8, 100), Blue);			// left of Fuse's area
}

static void TestMulticolourPerLine (void)
{
	auto pPicture = NewPicture ();
	ClearScreen (0x38, 0);
	const unsigned nCol = 5;
	for (unsigned y = 0; y < 8; y++)
	{
		fuseula_memory_write (0, 0x4000 + PixelOffset (y, nCol), 0xFF);
	}
	fuseula_end_frame ();

	// Each line of the character row gets its own ink, written at the
	// last T-state that still reaches the chunk (Fuse: the chunk at T
	// shows the state at T + 3). The write for line 4 comes one T-state
	// too late, so line 4 keeps line 3's ink.
	const uint8_t Ink[8] = { 1, 2, 3, 4, 5, 6, 7, 1 };
	for (unsigned y = 0; y < 8; y++)
	{
		uint32_t t = ChunkTState (6 + nCol, 39 + y) + 3 + (y == 4 ? 1 : 0);
		fuseula_memory_write (t, 0x4000 + AttrOffset (0, nCol), (uint8_t) (0x38 | Ink[y]));
	}
	fuseula_end_frame ();

	for (unsigned y = 0; y < 8; y++)
	{
		unsigned nExpect = y == 4 ? Ink[3] : Ink[y];
		CHECK_EQ (pPicture->Screen (8 * nCol, y), Colour (nExpect));
		CHECK_EQ (pPicture->Screen (8 * nCol + 7, y), Colour (nExpect));
	}
	// The cell next to it is untouched.
	CHECK_EQ (pPicture->Screen (8 * nCol + 8, 0), White);
}

static void TestMidLineBorder (void)
{
	auto pPicture = NewPicture ();
	ClearScreen (0x38, 1);
	fuseula_end_frame ();

	// Two border changes inside lines: on picture line 5 (outside Fuse's
	// area, drawn by our layer) and on picture line 20 (Fuse's top
	// border). Each change at T + 3 of chunk 20 reaches chunk 20, not 19.
	fuseula_io_write (0, 0x00FE, 1);
	fuseula_io_write (ChunkTState (20, 5) + 3, 0x00FE, 2);
	fuseula_io_write (ChunkTState (20, 20) + 3, 0x00FE, 4);
	// A change in the left border of screen line 100, at chunk 3.
	fuseula_io_write (ChunkTState (3, 139) + 3, 0x00FE, 6);
	fuseula_end_frame ();

	CHECK_EQ (pPicture->Chunk (19, 5), Blue);
	CHECK_EQ (pPicture->Chunk (20, 5), Red);
	CHECK_EQ (pPicture->Chunk (43, 5), Red);
	CHECK_EQ (pPicture->Chunk (0, 6), Red);
	CHECK_EQ (pPicture->Chunk (19, 20), Red);
	CHECK_EQ (pPicture->Chunk (20, 20), Green);
	CHECK_EQ (pPicture->Chunk (2, 139), Green);
	CHECK_EQ (pPicture->Chunk (3, 139), Yellow);
	CHECK_EQ (pPicture->Chunk (0, 140), Yellow);
	CHECK_EQ (pPicture->Chunk (43, 269), Yellow);
}

static void TestMidFrameScreenSwitch (void)
{
	auto pPicture = NewPicture ();
	fuseula_select (FUSEULA_128K);
	ClearScreen (0x10, 0);			// bank 5: paper red
	// Bank 7 at 0xC000, attributes paper green.
	fuseula_io_write (0, 0x7FFD, 0x07);
	for (unsigned i = 0; i < 0x300; i++)
	{
		fuseula_memory_write (0, (uint16_t) (0xD800 + i), 0x20);
	}
	fuseula_io_write (0, 0x7FFD, 0x00);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (100, 150), Red);

	// Screen 7 from the start of screen line 96.
	fuseula_io_write (ChunkTState (6, 39 + 96), 0x7FFD, 0x08);
	CHECK_EQ (fuseula_screen_bank (), 7u);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (100, 95), Red);
	CHECK_EQ (pPicture->Screen (0, 96), Green);
	CHECK_EQ (pPicture->Screen (100, 191), Green);

	// The next frame is all screen 7.
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (100, 10), Green);

	// A reset goes back to screen 5 and unlocked paging.
	fuseula_io_write (0, 0x7FFD, 0x28);	// screen 7, locked
	fuseula_reset (100);
	CHECK_EQ (fuseula_screen_bank (), 5u);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (100, 10), Red);
}

static void TestPlus3Paging (void)
{
	auto pPicture = NewPicture ();
	fuseula_select (FUSEULA_PLUS3);
	ClearScreen (0x38, 0);
	// Fuse's +2A/+3 decode: 0x7FFD is A15..A14 = 01, A1 = 0; a write to
	// 0xBFFD (A15 = 1) is not a paging write on the +3.
	fuseula_io_write (0, 0xBFFD, 0x08);
	CHECK_EQ (fuseula_screen_bank (), 5u);
	fuseula_io_write (0, 0x7FFD, 0x08);
	CHECK_EQ (fuseula_screen_bank (), 7u);
	// Special paging 0x1FFD = 0x03 maps banks 4, 5, 6, 7: 0x4000 is bank 5.
	fuseula_io_write (0, 0x7FFD, 0x00);
	fuseula_io_write (0, 0x1FFD, 0x03);
	fuseula_memory_write (0, 0x0000, 0xAB);	// bank 4 at 0x0000
	CHECK_EQ (fuseula_ram (4, 0), 0xAB);
	fuseula_io_write (0, 0x1FFD, 0x00);
	fuseula_memory_write (0, 0x0000, 0xCD);	// ROM again: no write
	CHECK_EQ (fuseula_ram (4, 0), 0xAB);
	fuseula_end_frame ();
}

static void TestUlaPlus (void)
{
	auto pPicture = NewPicture ();
	ClearScreen (0x38, 0);
	fuseula_memory_write (0, 0x4000 + PixelOffset (0, 0), 0xFF);
	fuseula_memory_write (0, 0x4000 + PixelOffset (100, 0), 0xFF);
	fuseula_memory_write (0, 0x4000 + AttrOffset (0, 0), 0x41);		// group 1, ink 1
	fuseula_memory_write (0, 0x4000 + AttrOffset (100, 0), 0x41);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), Colour (9));		// bright blue, normal mode

	// Palette: entry 17 (group 1, ink 1) = 0xE0 (green 7), entry 8
	// (border 0) = 0x1C (red 7). Mode on.
	fuseula_io_write (0, 0xBF3B, 17);
	fuseula_io_write (0, 0xFF3B, 0xE0);
	fuseula_io_write (0, 0xBF3B, 8);
	fuseula_io_write (0, 0xFF3B, 0x1C);
	fuseula_io_write (0, 0xBF3B, 0x40);
	fuseula_io_write (0, 0xFF3B, 0x01);
	CHECK (fuseula_ulaplus_on ());
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), 0xFF00FF00u);
	CHECK_EQ (pPicture->At (0, 0), 0xFFFF0000u);
	CHECK_EQ (pPicture->At (8, 100), 0xFFFF0000u);

	// A palette change at screen line 50 shows from there on, and the
	// frame after it shows the new colour everywhere.
	fuseula_io_write (0, 0xBF3B, 17);
	fuseula_io_write (ChunkTState (0, 39 + 50), 0xFF3B, 0x03);	// blue 3
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), 0xFF00FF00u);
	CHECK_EQ (pPicture->Screen (0, 100), 0xFF0000FFu);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), 0xFF0000FFu);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), 0xFF0000FFu);

	// A reset turns ULAplus off and keeps the palette.
	fuseula_reset (0);
	CHECK (!fuseula_ulaplus_on ());
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), Colour (9));
	fuseula_io_write (0, 0xBF3B, 0x40);
	fuseula_io_write (0, 0xFF3B, 0x01);
	fuseula_end_frame ();
	CHECK_EQ (pPicture->Screen (0, 0), 0xFF0000FFu);
	fuseula_io_write (0, 0xFF3B, 0x00);
	fuseula_end_frame ();
}

static void TestMulticolourAcrossFlashFrames (void)
{
	// A multicolour engine rewrites the attribute of a cell for each line,
	// every frame, while another cell has FLASH set. Fuse swaps FLASH every
	// 16 frames inside display_frame (); every frame, including those, must
	// keep its per-line inks.
	auto pPicture = NewPicture ();
	fuseula_select (FUSEULA_128K);
	ClearScreen (0x38, 0);
	const unsigned nCol = 7, nRow = 3;
	for (unsigned y = 0; y < 8; y++)
	{
		fuseula_memory_write (0, 0x4000 + PixelOffset (8 * nRow + y, nCol), 0xFF);
	}
	fuseula_memory_write (0, 0x4000 + AttrOffset (100, 20), 0x80 | 0x38 | 2);	// FLASH
	fuseula_end_frame ();

	const uint8_t Ink[8] = { 1, 2, 3, 4, 5, 6, 7, 2 };
	unsigned nBadFrames = 0;
	for (unsigned f = 0; f < 40; f++)
	{
		for (unsigned y = 0; y < 8; y++)
		{
			uint32_t t = ChunkTState (6 + nCol, 39 + 8 * nRow + y) - 20;
			fuseula_memory_write (t, 0x4000 + AttrOffset (8 * nRow, nCol),
					      (uint8_t) (0x38 | Ink[y]));
		}
		fuseula_end_frame ();

		bool bBad = false;
		for (unsigned y = 0; y < 8; y++)
		{
			bBad = bBad || pPicture->Screen (8 * nCol, 8 * nRow + y) != Colour (Ink[y]);
		}
		nBadFrames += bBad;
	}
	CHECK_EQ (nBadFrames, 0u);
}

// A +3 multicolour engine and a frame start that F events alone get wrong.
// Each attribute write for line y of a cell aims at the chunk's fetch
// T-state + 3 + j (j = -3 to 3, by cell and line) and ends at the last
// T-state at or before that where Fuse's +3 contention lets a contended
// write end. Writes at or before fetch + 3 reach their line; later ones
// reach the next line. Placed a T-state or two off, some land on the wrong
// line.
static uint32_t EngineWriteTState (const std::vector<uint8_t> &rContention, unsigned nCol,
				   unsigned nLine, unsigned nIndex)
{
	int j = (int) (nIndex % 7) - 3;
	uint32_t t = (uint32_t) ((int) ChunkTState (6 + nCol, 39 + nLine) + 3 + j);
	while (rContention[t - 3] != 0)
	{
		t--;
	}
	return t;
}

static void RunContentionAligned (int nAckPhase, bool bEarlyFrameEvent, unsigned &rBadLines,
				  uint32_t &rAlignments)
{
	auto pPicture = NewPicture ();
	std::vector<uint8_t> Contention (70908);
	fuseula_contention (FUSEULA_PLUS3, Contention.data (), (uint32_t) Contention.size ());
	fuseula_select (FUSEULA_PLUS3);
	ClearScreen (0x38, 0);
	for (unsigned y = 0; y < 192; y++)
	{
		for (unsigned c = 0; c < 32; c++)
		{
			fuseula_memory_write (0, 0x4000 + PixelOffset (y, c), 0xFF);
		}
	}
	fuseula_end_frame ();

	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChoosePlus3);
	Frames.SetContention (MachinePlus3, Contention.data ());
	CFramePresenter Presenter (pLog.get ());

	const uint64_t S = 3000000;
	const uint32_t L = 70908;
	const unsigned nFrames = 12, nCol0 = 1, nCols = 4, nRows = 6;
	auto Ink = [] (unsigned y, unsigned r, unsigned c) { return (uint8_t) (1 + (y + r + c) % 7); };

	for (unsigned n = 0; n < nFrames; n++)
	{
		uint64_t s = S + (uint64_t) n * L;

		// The push: the acknowledge always nAckPhase T-states late; with
		// bEarlyFrameEvent, the first F event comes 2 T-states before the
		// true start.
		uint64_t nPush = s + FrameEventTStates + nAckPhase;
		if (bEarlyFrameEvent && n == 0)
		{
			nPush = s + FrameEventTStates - 2;
		}
		Frames.TimedEvent (MakeEvent (KindMemory, 0x5BFE, 0, true), nPush);

		std::vector<std::pair<uint32_t, TEvent>> Writes;
		for (unsigned r = 0; r < nRows; r++)
		{
			for (unsigned c = 0; c < nCols; c++)
			{
				for (unsigned y = 0; y < 8; y++)
				{
					unsigned nLine = 8 * (r + 1) + y;
					uint32_t t = EngineWriteTState (Contention, nCol0 + c, nLine, 5 * y + 3 * r + c);
					Writes.push_back ({ t, MakeEvent (KindMemory,
						(uint16_t) (0x4000 + AttrOffset (nLine, nCol0 + c)),
						(uint8_t) (0x38 | Ink (y, r, c))) });
				}
			}
		}
		std::sort (Writes.begin (), Writes.end (),
			   [] (const std::pair<uint32_t, TEvent> &a, const std::pair<uint32_t, TEvent> &b)
			   { return a.first < b.first; });
		for (const auto &rWrite : Writes)
		{
			Frames.TimedEvent (rWrite.second, s + rWrite.first);
		}
		Frames.Clock (s + L + 10, 0.0);
	}
	while (Presenter.Present ())
	{
	}

	// Fuse's rule at the true T-states: a write at t reaches the chunk at T
	// when t <= T + 3; a later one leaves the line with the ink before it.
	rBadLines = 0;
	for (unsigned r = 0; r < nRows; r++)
	{
		for (unsigned c = 0; c < nCols; c++)
		{
			for (unsigned y = 0; y < 8; y++)
			{
				unsigned nLine = 8 * (r + 1) + y;
				uint32_t t = EngineWriteTState (Contention, nCol0 + c, nLine, 5 * y + 3 * r + c);
				bool bThisLine = t <= ChunkTState (6 + nCol0 + c, 39 + nLine) + 3;
				uint8_t nExpect = bThisLine ? Ink (y, r, c) : Ink ((y + 7) % 8, r, c);
				if (pPicture->Screen (8 * (nCol0 + c), nLine) != Colour (nExpect))
				{
					rBadLines++;
				}
			}
		}
	}
	rAlignments = Frames.Alignments ();
}

static void TestContentionAlignedFrameStart (void)
{
	unsigned nBad;
	uint32_t nAlignments;

	// Every acknowledge 2 T-states late (a program locked to the cycle).
	RunContentionAligned (2, false, nBad, nAlignments);
	CHECK_EQ (nBad, 0u);
	CHECK (nAlignments >= 1);

	// The first F event 2 T-states before the true start.
	RunContentionAligned (0, true, nBad, nAlignments);
	CHECK_EQ (nBad, 0u);
	CHECK (nAlignments >= 1);

	// Exact acknowledges: nothing to move.
	RunContentionAligned (0, false, nBad, nAlignments);
	CHECK_EQ (nBad, 0u);
	CHECK_EQ (nAlignments, 0u);
}

// --- presentation ---------------------------------------------------------

static void PushFrame (CEventLog &rLog, uint32_t nFrame, uint8_t nBorder)
{
	TTimedEvent Event = { nFrame, 100, 0x00FE, KindIo, nBorder };
	CHECK (rLog.Push (Event));
	rLog.Complete (nFrame + 1, Machine48K, true);
}

static void TestPresenterWholeFrames (void)
{
	auto pPicture = NewPicture ();
	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFramePresenter Presenter (pLog.get ());

	// Frames alternate between two border colours. Each HDMI frame shows
	// one whole Spectrum frame; with frames piling up some are skipped,
	// still run through Fuse, and the one shown is whole.
	CHECK (!Presenter.Present ());
	CHECK_EQ (Presenter.Repeated (), 1u);

	PushFrame (*pLog, 0, 2);
	CHECK (Presenter.Present ());
	CHECK_EQ (pPicture->At (0, 0), Red);
	CHECK_EQ (pPicture->At (351, 269), Red);

	for (uint32_t n = 1; n <= 5; n++)
	{
		PushFrame (*pLog, n, n & 1 ? 4 : 2);
	}
	CHECK (Presenter.Present ());
	CHECK_EQ (Presenter.Skipped (), 3u);
	CHECK_EQ (Presenter.LastFrame (), 4u);
	CHECK_EQ (pPicture->At (0, 0), Red);
	CHECK_EQ (pPicture->At (8, 135), Red);
	CHECK (Presenter.Present ());
	CHECK_EQ (Presenter.LastFrame (), 5u);
	CHECK_EQ (pPicture->At (0, 0), Green);
	CHECK_EQ (pPicture->At (351, 269), Green);
	CHECK (!Presenter.Present ());
	CHECK_EQ (pPicture->At (0, 0), Green);
}

// --- end to end -----------------------------------------------------------

static void TestEndToEndMulticolour (void)
{
	// A 128K-class Spectrum: each frame the interrupt push (with a few
	// clocks of acknowledge delay, none in frame 2), then attribute writes
	// that give each line of character row 4, column 10, its own ink, a
	// border change inside a line, and a transfer restart in the middle of
	// the frame. Everything goes through the simulated CPLD, the intake,
	// the clock count, the frame clock, the log and Fuse.
	const uint32_t L = 70908;
	const uint64_t S = 7000;			// frame 0 starts at this count
	const unsigned nFrames = 8;
	auto pPicture = NewPicture ();
	fuseula_select (FUSEULA_128K);
	const uint32_t nLT0 = fuseula_top_left_border_tstate ();
	const uint32_t nLine = fuseula_line_tstates ();
	auto Chunk = [&] (unsigned p, unsigned q) { return nLT0 + (q - 15) * nLine + 4 * (p - 2); };

	const unsigned nCol = 10, nRow = 4;
	const uint8_t Ink[8] = { 2, 4, 6, 1, 3, 5, 7, 2 };

	std::vector<TSimWrite> Writes;
	for (unsigned n = 0; n < nFrames; n++)
	{
		uint64_t s = S + (uint64_t) n * L;
		const uint32_t AckDelay[] = { 6, 3, 0, 9, 2, 5, 1, 4 };
		Writes.push_back ({ s + FrameEventTStates + AckDelay[n], KindMemory, 0x7FFF, 0x12, true });
		Writes.push_back ({ s + FrameEventTStates + AckDelay[n] + 3, KindMemory, 0x7FFE, 0x34, false });
		if (n == 0)
		{
			for (unsigned y = 0; y < 8; y++)
			{
				Writes.push_back ({ s + 200 + 21 * y, KindMemory,
						    (uint16_t) (0x4000 + PixelOffset (8 * nRow + y, nCol)), 0xFF, false });
			}
			Writes.push_back ({ s + 400, KindMemory, (uint16_t) (0x4000 + PixelOffset (8 * nRow + 3, nCol + 1)), 0x00, false });
		}
		for (unsigned y = 0; y < 8; y++)
		{
			uint32_t t = Chunk (6 + nCol, 39 + 8 * nRow + y) + 3;
			Writes.push_back ({ s + t, KindMemory, (uint16_t) (0x4000 + AttrOffset (8 * nRow, nCol)),
					    (uint8_t) (0x38 | Ink[y]), false });
		}
		// OUT (0xFE): its count is the end of the I/O cycle, 3 T-states
		// after Fuse's write point.
		Writes.push_back ({ s + Chunk (30, 250) + 3 + 3, KindIo, 0x00FE, 2, false });
		Writes.push_back ({ s + Chunk (0, 3) + 3 + 3, KindIo, 0x00FE, 1, false });
	}
	std::sort (Writes.begin (), Writes.end (),
		   [] (const TSimWrite &a, const TSimWrite &b) { return a.Count < b.Count; });

	double nEndNs = CountNs (S + (uint64_t) nFrames * L + 2000);
	std::vector<TSimStop> Stops;
	for (unsigned n = 0; n < nFrames; n++)
	{
		Stops.push_back ({ CountNs (S + (uint64_t) n * L + 30000), 4000 });
	}
	CCpldSim Sim;
	Sim.Run (Writes, Stops, nEndNs);
	CHECK_EQ (Sim.Stored.size (), Writes.size ());

	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
	CUlaClock Ula (&Frames, &Frames, TickHz);
	CTStateClock Clock (&Ula, TickHz, 3546900);
	TIntakeCounters Counters;
	Feed (Sim, Clock, Counters);
	pLog->Publish ();

	CHECK (!Ula.Stopped ());
	CHECK_EQ (Ula.MinFrameClocks () + 64 > L, true);
	CHECK (Frames.Locked ());
	CHECK_EQ (Frames.Machine (), Machine128K);
	CHECK_EQ (Clock.Ambiguous (), 0u);
	CHECK (pLog->Completed () >= nFrames - 1);

	CFramePresenter Presenter (pLog.get ());
	while (Presenter.Present ())
	{
	}
	CHECK (Presenter.LastFrame () + 1 == pLog->Completed ());

	for (unsigned y = 0; y < 8; y++)
	{
		CHECK_EQ (pPicture->Screen (8 * nCol, 8 * nRow + y), Colour (Ink[y]));
	}
	CHECK_EQ (pPicture->Screen (8 * nCol + 8, 8 * nRow + 3), White);
	CHECK_EQ (pPicture->Chunk (29, 250), Blue);
	CHECK_EQ (pPicture->Chunk (30, 250), Red);
	CHECK_EQ (pPicture->Chunk (43, 269), Red);
	CHECK_EQ (pPicture->Chunk (0, 2), Red);
	CHECK_EQ (pPicture->Chunk (0, 3), Blue);
}

// --- a 128K: the edge-connector clock stops during contention ---------------

// A recording sink between the ULA clock and the frame clock.
class CTeeSink : public ITimedEventSink
{
public:
	explicit CTeeSink (ITimedEventSink *pNext) : m_pNext (pNext) {}
	void TimedEvent (const TEvent &rEvent, uint64_t nCount) override
	{
		Events.push_back (rEvent);
		Counts.push_back (nCount);
		m_pNext->TimedEvent (rEvent, nCount);
	}
	void Clock (uint64_t nCount, double nTicks) override	{ m_pNext->Clock (nCount, nTicks); }
	void ReadsRestarted (void) override			{ m_pNext->ReadsRestarted (); }

	std::vector<TEvent> Events;
	std::vector<uint64_t> Counts;

private:
	ITimedEventSink *m_pNext;
};

// A Z80 on a 128K whose clock the ULA holds, per Fuse's contention table:
// a memory access to contended memory that starts at ULA T-state s waits
// Contention[s] T-states. The edge-connector clock is the Z80 clock, so it
// does not tick while the Z80 waits.
class CStoppingZ80
{
public:
	explicit CStoppingZ80 (const std::vector<uint8_t> &rContention, uint32_t nFrameTStates)
	:	m_rContention (rContention), m_nL (nFrameTStates) {}

	// A contended access whose cycle starts at ULA time s (absolute).
	// Returns the ULA time it ends, for a 3 T-state cycle.
	uint64_t Contended (uint64_t s)
	{
		uint8_t nWait = m_rContention[s % m_nL];
		if (nWait != 0)
		{
			m_Stalls.push_back ({ s, nWait });
		}
		return s + nWait + 3;
	}

	// The count at ULA time u: ULA T-states less those the Z80 waited
	// before u.
	uint64_t CountAt (double u) const
	{
		uint64_t nEdges = (uint64_t) u;
		uint64_t nStall = 0;
		for (const auto &rStall : m_Stalls)
		{
			if (rStall.first >= nEdges)
			{
				break;
			}
			uint64_t nEnd = rStall.first + rStall.second;
			nStall += (nEnd <= nEdges ? nEnd : nEdges) - rStall.first;
		}
		return nEdges - nStall;
	}

private:
	const std::vector<uint8_t> &m_rContention;
	uint32_t m_nL;
	std::vector<std::pair<uint64_t, uint8_t>> m_Stalls;	// start, length
};

static void RunStoppedClockMulticolour (fuseula_machine Machine, uint32_t nClockHz,
					double nStartJitterNs = 0, bool bStopsInBorder = true)
{
	// Each frame: the interrupt push 0 to 3 T-states after the start
	// (HALT), then attribute writes for every line of 6 character rows and
	// 4 columns, each aimed near the fetch of its chunk, with a contended
	// read of the screen before every other write that nobody sees. The
	// writes end where Fuse's contention lets them; the clock loses every
	// T-state the Z80 waits. Reads stop for 7 us every 13.1 ms.
	auto pPicture = NewPicture ();
	const uint32_t L = fuseula_frame_tstates (Machine);
	ClockNs = 1e9 / nClockHz;
	std::vector<uint8_t> Contention (L);
	fuseula_contention (Machine, Contention.data (), L);
	fuseula_select (Machine);
	const TMachine Expect = (TMachine) Machine;
	ClearScreen (0x38, 0);
	for (unsigned y = 0; y < 192; y++)
	{
		for (unsigned c = 0; c < 32; c++)
		{
			fuseula_memory_write (0, 0x4000 + PixelOffset (y, c), 0xFF);
		}
	}
	fuseula_end_frame ();

	CStoppingZ80 Z80 (Contention, L);
	const uint64_t S = 2 * L;
	const unsigned nFrames = 24, nCol0 = 1, nCols = 4, nRows = 6;
	auto Ink = [] (unsigned y, unsigned r, unsigned c) { return (uint8_t) (1 + (y + r + c) % 7); };

	struct TTrueWrite { uint64_t u; TSimWrite Write; };
	std::vector<TTrueWrite> True;
	std::vector<std::vector<uint64_t>> Ends (nFrames);	// true end per write, per frame
	for (unsigned n = 0; n < nFrames; n++)
	{
		uint64_t s = S + (uint64_t) n * L;
		uint64_t nPush = s + FrameEventTStates + (n * 3 + 1) % 4 * (n == 2 ? 0 : 1);
		True.push_back ({ nPush, { 0, KindMemory, 0xBFFE, 0x12, true, 0 } });
		True.push_back ({ nPush + 3, { 0, KindMemory, 0xBFFD, 0x34, false, 0 } });

		uint64_t nNow = s + 1000;
		unsigned nIndex = 0;
		for (unsigned r = 0; r < nRows; r++)
		{
			for (unsigned y = 0; y < 8; y++)
			{
				for (unsigned c = 0; c < nCols; c++, nIndex++)
				{
					unsigned nLine = 8 * (r + 1) + y;
					int j = (int) ((5 * y + 3 * r + c) % 7) - 3;
					uint64_t nAim = s + (uint64_t) ((int) ChunkTState (6 + nCol0 + c, 39 + nLine) + j);
					uint64_t nStart = nAim > nNow ? nAim : nNow;
					if (nIndex % 2 == 0)
					{
						nStart = Z80.Contended (nStart) + 1;	// the unseen read
					}
					uint64_t nEnd = Z80.Contended (nStart);
					True.push_back ({ nEnd, { 0, KindMemory,
						(uint16_t) (0x4000 + AttrOffset (nLine, nCol0 + c)),
						(uint8_t) (0x38 | Ink (y, r, c)), false, 0 } });
					Ends[n].push_back (nEnd - s);
					nNow = nEnd + 4;	// uncontended code
				}
			}
		}
	}

	std::vector<TSimWrite> Writes;
	for (TTrueWrite &rTrue : True)
	{
		rTrue.Write.Count = Z80.CountAt ((double) rTrue.u);
		rTrue.Write.Ns = ((double) rTrue.u - 0.5) * ClockNs;
		Writes.push_back (rTrue.Write);
	}

	double nEndNs = ((double) (S + (uint64_t) nFrames * L) + 2000) * ClockNs;
	// Restarts as the Pi plans them in stopped mode: each transfer ends in
	// a border (PlanTransferReads), or every 65,534 reads.
	std::vector<TSimStop> Stops;
	for (double t = 0; ; )
	{
		uint32_t nReads = 65534;
		if (bStopsInBorder)
		{
			double u = t / ClockNs - (double) S;
			double nFrameT = u - (double) L * (double) (int64_t) (u / L);
			nReads = PlanTransferReads (nFrameT, ReadNs / ClockNs, L, 65534);
		}
		t += nReads * ReadNs;
		if (t >= nEndNs)
		{
			break;
		}
		Stops.push_back ({ t, 2000 });
		t += 2000;
	}
	CCpldSim Sim;
	Sim.StartJitterNs = nStartJitterNs;
	Sim.CountAtNs = [&Z80] (double ns) { return Z80.CountAt (ns / ClockNs); };
	Sim.Run (Writes, Stops, nEndNs);
	CHECK_EQ (Sim.Stored.size (), Writes.size ());

	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
	Frames.SetContention (Expect, Contention.data ());
	CTeeSink Tee (&Frames);
	CUlaClock Ula (&Tee, &Frames, TickHz);
	// Core 1's part, then core 2's, through the queue between them.
	std::unique_ptr<CUlaQueue> pQueue (new CUlaQueue);
	CTStateClock Clock (pQueue.get (), TickHz, 3546900);
	TIntakeCounters Counters;
	double nCore2Ns = 0;
	auto t0 = std::chrono::steady_clock::now ();
	Feed (Sim, Clock, Counters, 97, [&] (void)
	{
		pQueue->Publish ();
		auto a = std::chrono::steady_clock::now ();
		pQueue->Drain (Ula);
		nCore2Ns += std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now () - a).count ();
	});
	auto t1 = std::chrono::steady_clock::now ();
	CHECK_EQ (pQueue->Dropped (), 0u);
	pLog->Publish ();
	if (Machine == FUSEULA_128K && ReadNs == 200.0)
	{
		double nNs = std::chrono::duration<double, std::nano> (t1 - t0).count () - nCore2Ns;
		double nPairs = (double) (Sim.Words.size () / 2);
		printf ("cost (host, stopped clock): core 1 %.1f ns per pair, core 2 %.1f ns per pair\n",
			nNs / nPairs, nCore2Ns / nPairs);
	}

	CHECK (Ula.Stopped ());
	CHECK (Ula.LastFrameClocks () + 500 < L);
	CHECK_EQ (Frames.Machine (), Expect);
	// Only while the phase settles, in the first frames after the clock is
	// found stopped.
	CHECK (Ula.Ambiguous () <= 16);
	CHECK (Frames.Acquisitions () <= 2);
	CHECK (Ula.PhaseSlips () <= 2);
	CHECK (Ula.RestartsInDisplay () <= 8);	// before the frame start settles

	// Every write from frame 10 on at its true ULA time, up to one constant.
	CHECK_EQ (Tee.Counts.size (), True.size ());
	if (Tee.Counts.size () == True.size ())
	{
		size_t nFrom = 0;
		while (nFrom < True.size () && True[nFrom].u < S + 10 * L)
		{
			nFrom++;
		}
		uint64_t nOffset = Tee.Counts[nFrom] - True[nFrom].u;
		unsigned nWrong = 0;
		for (size_t i = nFrom; i < True.size (); i++)
		{
			nWrong += Tee.Counts[i] - True[i].u != nOffset;
		}
		CHECK_EQ (nWrong, 0u);
	}

	CFramePresenter Presenter (pLog.get ());
	while (Presenter.Present ())
	{
	}
	unsigned nBad = 0;
	const std::vector<uint64_t> &rEnds = Ends[nFrames - 1];
	unsigned nIndex = 0;
	for (unsigned r = 0; r < nRows; r++)
	{
		for (unsigned y = 0; y < 8; y++)
		{
			for (unsigned c = 0; c < nCols; c++, nIndex++)
			{
				unsigned nLine = 8 * (r + 1) + y;
				bool bThisLine = rEnds[nIndex] <= ChunkTState (6 + nCol0 + c, 39 + nLine) + 3;
				uint8_t nExpect = bThisLine ? Ink (y, r, c) : Ink ((y + 7) % 8, r, c);
				nBad += pPicture->Screen (8 * (nCol0 + c), nLine) != Colour (nExpect);
			}
		}
	}
	CHECK_EQ (nBad, 0u);
}

static void TestStoppedClockMulticolour (void)
{
	RunStoppedClockMulticolour (FUSEULA_128K, 3546900);
	RunStoppedClockMulticolour (FUSEULA_48K, 3500000);

	// SMI reads 200 ppm slower than the 200 ns the Pi assumes: the ring
	// clock measures the period over the first transfer.
	ReadNs = 200.04;
	RunStoppedClockMulticolour (FUSEULA_128K, 3546900);
	ReadNs = 200.0;

	// The first read of a transfer up to 500 ns after the Pi times its
	// start, with the restarts in the border.
	RunStoppedClockMulticolour (FUSEULA_128K, 3546900, 500, true);
	ClockNs = 1e9 / 3546900.0;
}

static void TestResetClearsPlus3 (void)
{
	// A +3 seen by its 0x1FFD write, then a reset and frames with no 0x1FFD
	// write: back to the 128K; the +3 ROM's next 0x1FFD write makes it a +3
	// again.
	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
	uint64_t s = 1000000;
	Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0, true), s + 10);
	Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), s + 500);
	CHECK_EQ (Frames.Machine (), MachinePlus3);
	Frames.TimedEvent (MakeEvent (KindReset, 0, 0), s + 1000);
	CHECK_EQ (Frames.Machine (), Machine128K);
	Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), s + 1000 + ResetBurstTStates + 100);
	CHECK_EQ (Frames.Machine (), MachinePlus3);
	CHECK_EQ (Frames.Port1FFDWrite (), 0x1FFD04u);
}

// A reset button that bounces: a dozen reset events at the press, the Z80
// held for 300 ms, a few dozen at the release, with stray writes between
// the bounces (one of them to exactly 0x1FFD), then the 128K ROM's own
// writes, which include OUT (n),A to 0x10FD. One reset, the 128K stays a
// 128K, and the frame start is not disturbed by F events in the burst.
static void TestBouncingReset (void)
{
	for (int nPlus3 = 0; nPlus3 < 2; nPlus3++)
	{
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
		const uint32_t L = 70908;
		uint64_t s = 1000000;
		for (unsigned n = 0; n < 10; n++, s += L)
		{
			Frames.TimedEvent (MakeEvent (KindMemory, 0xFFFE, 0, true), s + 10);
			Frames.Clock (s + L + 10, 0.0);
		}
		uint32_t nAcq = Frames.Acquisitions ();
		uint64_t c = s + 5000;
		for (unsigned i = 0; i < 12; i++, c += 600)
		{
			Frames.TimedEvent (MakeEvent (KindReset, 0, 0), c);
			Frames.TimedEvent (MakeEvent (KindIo, (uint16_t) (0x1000 | (i * 0x111)), 0xFF), c + 20);
			Frames.TimedEvent (MakeEvent (KindMemory, 0x5800, 0x12, true), c + 40);
		}
		Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x07), c + 50);
		c += 300 * 3547;			// held
		for (unsigned i = 0; i < 30; i++, c += 400)
		{
			Frames.TimedEvent (MakeEvent (KindReset, 0, 0), c);
			Frames.TimedEvent (MakeEvent (KindIo, (uint16_t) (0x1FF0 + i), 0xFF), c + 20);
		}
		c += ResetBurstTStates + 1000;
		Frames.TimedEvent (MakeEvent (KindIo, 0x7FFD, 0x10), c);
		Frames.TimedEvent (MakeEvent (KindIo, 0x10FD, 0x00), c + 100);
		if (nPlus3)
		{
			Frames.TimedEvent (MakeEvent (KindIo, 0x1FFD, 0x04), c + 200);
		}

		CHECK_EQ (Frames.Resets (), 2u);	// press and release, far apart
		CHECK_EQ (Frames.Machine (), nPlus3 ? MachinePlus3 : Machine128K);
		CHECK_EQ (Frames.Port1FFDWrite (), nPlus3 ? 0x1FFD04u : 0u);
		CHECK_EQ (Frames.Acquisitions (), nAcq);
	}
}

// --- cost -----------------------------------------------------------------

static void ReportCost (void)
{
	// Core 0: one frame with the worst write rate, every write to the
	// shown screen, so each one goes through Fuse's display code.
	auto pPicture = NewPicture ();
	fuseula_select (FUSEULA_128K);
	const unsigned nWrites = 13000;
	const unsigned nFramesRun = 50;
	auto t0 = std::chrono::steady_clock::now ();
	for (unsigned f = 0; f < nFramesRun; f++)
	{
		for (unsigned i = 0; i < nWrites; i++)
		{
			fuseula_memory_write (i * 5, (uint16_t) (0x4000 + (i * 7) % 0x1B00), (uint8_t) (i + f));
		}
		fuseula_end_frame ();
	}
	auto t1 = std::chrono::steady_clock::now ();
	double nFrameUs = std::chrono::duration<double, std::micro> (t1 - t0).count () / nFramesRun;

	// Core 1: the intake path for one frame of words at the worst write
	// rate.
	std::vector<TSimWrite> Writes;
	for (uint64_t c = 100; c < 70908; c += 5)
	{
		Writes.push_back ({ c, KindMemory, (uint16_t) (0x4000 + c % 0x1B00), 0, false });
	}
	CCpldSim Sim;
	Sim.Run (Writes, {}, CountNs (71000));
	std::unique_ptr<CEventLog> pLog (new CEventLog);
	CFrameClock Frames (pLog.get (), FrameLengths (), ChooseAuto);
	CTStateClock Clock (&Frames, TickHz, 3546900);
	TIntakeCounters Counters;
	auto t2 = std::chrono::steady_clock::now ();
	Feed (Sim, Clock, Counters, 4096);
	auto t3 = std::chrono::steady_clock::now ();
	double nIntakeUs = std::chrono::duration<double, std::micro> (t3 - t2).count ();

	printf ("cost (host): core 0 %.0f us per frame with %u writes; "
		"core 1 %.0f us per frame of %zu words with %llu events\n",
		nFrameUs, nWrites, nIntakeUs, Sim.Words.size (),
		(unsigned long long) Counters.Events);
}

int main (void)
{
	Run ("gray code", TestGrayCode);
	Run ("decoder: event fields and time bits", TestDecoderEvent);
	Run ("decoder: empty pairs, mismatch, malformed", TestDecoderEmptyAndMalformed);
	Run ("intake: pairs across runs, first pair after SYNC", TestIntakePairsAcrossRuns);
	Run ("intake: stop at an H mismatch", TestIntakeMismatchStops);
	Run ("clock count: steady stream", TestCountSteadyStream);
	Run ("clock count: full queue and restart gaps", TestCountFullQueueAndRestartGap);
	Run ("clock count: long wait is reported", TestCountLongWaitIsReported);
	Run ("frame clock: frame start from the earliest acknowledge", TestFrameStartFromEarliestAck);
	Run ("frame clock: machine detection", TestMachineDetection);
	Run ("fuse: still picture and border", TestFuseStillPicture);
	Run ("fuse: multicolour per line", TestMulticolourPerLine);
	Run ("fuse: border change inside a line", TestMidLineBorder);
	Run ("fuse: 0x7FFD screen switch mid-frame, reset", TestMidFrameScreenSwitch);
	Run ("fuse: +2A/+3 paging decode", TestPlus3Paging);
	Run ("fuse: ULAplus palette and mode", TestUlaPlus);
	Run ("fuse: multicolour across the FLASH frames", TestMulticolourAcrossFlashFrames);
	Run ("frame clock: frame start from +3 contention", TestContentionAlignedFrameStart);
	Run ("presenter: whole frames, skips and repeats", TestPresenterWholeFrames);
	Run ("end to end: multicolour through CPLD, clocks and Fuse", TestEndToEndMulticolour);
	Run ("48K and 128K: stopped clock, multicolour at ULA time", TestStoppedClockMulticolour);
	Run ("frame clock: a reset clears the +2A/+3 detection", TestResetClearsPlus3);
	Run ("frame clock: a bouncing reset", TestBouncingReset);

	ReportCost ();

	printf ("%u tests, %u failed; %u checks, %u failed\n",
		s_nTests, s_nTestsFailed, s_nChecks, s_nFailures);
	return s_nFailures == 0 ? 0 : 1;
}
