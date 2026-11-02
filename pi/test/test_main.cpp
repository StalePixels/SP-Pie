//
// test_main.cpp
//
// Host tests for the event decoder, the intake loop, the state model, the
// frame lock, the timeline, the event log, the presenter and the renderer.
// Each test feeds a synthetic event stream through the same code the Pi
// kernel runs and checks the result, down to individual pixels.
//
// The timed tests use a simulated Spectrum: writes at known T-states of
// known frames, F events with a random Z80 delay, a random transport
// latency, the Pi's 19.2 MHz tick clock and a 50.00 Hz HDMI vertical sync.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "../src/event.h"
#include "../src/eventlog.h"
#include "../src/framelock.h"
#include "../src/intake.h"
#include "../src/presenter.h"
#include "../src/renderer.h"
#include "../src/spectrumstate.h"
#include "../src/timeline.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace sppie;

static unsigned s_nChecks;
static unsigned s_nFailures;
static bool s_bTestFailed;

#define CHECK(cond)							\
	do {								\
		s_nChecks++;						\
		if (!(cond)) {						\
			s_nFailures++;					\
			s_bTestFailed = true;				\
			printf ("    check failed: %s (%s:%d)\n",	\
				#cond, __FILE__, __LINE__);		\
		}							\
	} while (0)

#define CHECK_EQ_HEX(a, b)						\
	do {								\
		unsigned long long _a = (a), _b = (b);			\
		s_nChecks++;						\
		if (_a != _b) {						\
			s_nFailures++;					\
			s_bTestFailed = true;				\
			printf ("    check failed: %s == %s: 0x%llx != 0x%llx (%s:%d)\n", \
				#a, #b, _a, _b, __FILE__, __LINE__);	\
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
	printf ("%s %s\n", s_bTestFailed ? "FAIL" : "PASS", pName);
}

// --- helpers -------------------------------------------------------------

static uint32_t Mem (uint16_t nAddress, uint8_t nData, bool bFrame = false)
{
	return EncodeEvent (KindMemory, nAddress, nData, false, bFrame);
}

static uint32_t Io (uint16_t nPort, uint8_t nData)
{
	return EncodeEvent (KindIo, nPort, nData);
}

static uint32_t ResetWord (bool bOverflow = false)
{
	return EncodeEvent (KindReset, 0, 0, bOverflow);
}

// Screen addresses at 0x4000 (bank 5 on every model in normal paging).
static uint16_t PixelAddr (unsigned y, unsigned nCol)
{
	return (uint16_t) (0x4000 + PixelRowOffset (y) + nCol);
}

static uint16_t AttrAddr (unsigned y, unsigned nCol)
{
	return (uint16_t) (0x4000 + AttributeRowOffset (y) + nCol);
}

// Applies events straight to a state.
class CStateSink : public IEventSink
{
public:
	CStateSink (CRenderState &rState, TMachine Machine) : m_rState (rState), m_Machine (Machine) {}

	void Event (const TEvent &rEvent) override
	{
		m_rState.State.Apply (rEvent, m_Machine);
	}

private:
	CRenderState	&m_rState;
	TMachine	m_Machine;
};

static void Feed (CRenderState &rState, TIntakeCounters &rCounters,
		  const std::vector<uint32_t> &rWords, TMachine Machine = Machine48K)
{
	CStateSink Sink (rState, Machine);
	bool bMismatch = false;
	size_t n = ProcessWords (rWords.data (), rWords.size (), Sink, rCounters, bMismatch);
	CHECK (!bMismatch);
	CHECK (n == rWords.size ());
	rState.SyncColours ();
}

static std::unique_ptr<CRenderState> NewState (void)
{
	std::unique_ptr<CRenderState> p (new CRenderState);
	p->Reset ();
	return p;
}

// The 352 x 296 picture, with a wider pitch to check that the renderer
// honours it.
static const unsigned TestPitch = PictureWidth + 16;

struct CTestPicture
{
	std::vector<uint32_t> Pixels;

	CTestPicture (void) : Pixels (TestPitch * PictureHeight, 0x12345678u) {}

	uint32_t At (unsigned x, unsigned y) const { return Pixels[y * TestPitch + x]; }
	uint32_t *Data (void) { return Pixels.data (); }
};

static void Render (CRenderState &rState, uint32_t nFrame, CTestPicture &rPicture,
		    TMachine Machine = Machine48K)
{
	RenderStill (rState, Machine, nFrame, rPicture.Data (), TestPitch);
}

// Pixel (px, py) of the 256 x 192 screen, in picture coordinates.
static uint32_t ScreenPixel (const CTestPicture &rPicture, unsigned px, unsigned py)
{
	return rPicture.At (BorderLeft + px, BorderTop + py);
}

static void CheckCell (const CTestPicture &rPicture, unsigned nCol, unsigned y, uint8_t nBits,
		       uint32_t nInk, uint32_t nPaper)
{
	for (unsigned i = 0; i < 8; i++)
	{
		uint32_t nExpect = (nBits & (0x80 >> i)) ? nInk : nPaper;
		CHECK_EQ_HEX (ScreenPixel (rPicture, nCol * 8 + i, y), nExpect);
	}
}

static void CheckBorder (const CTestPicture &rPicture, uint32_t nColour)
{
	// corners, the middle of each band, and both side strips on a screen row
	CHECK_EQ_HEX (rPicture.At (0, 0), nColour);
	CHECK_EQ_HEX (rPicture.At (PictureWidth - 1, 0), nColour);
	CHECK_EQ_HEX (rPicture.At (0, PictureHeight - 1), nColour);
	CHECK_EQ_HEX (rPicture.At (PictureWidth - 1, PictureHeight - 1), nColour);
	CHECK_EQ_HEX (rPicture.At (176, BorderTop - 1), nColour);
	CHECK_EQ_HEX (rPicture.At (176, BorderTop + ScreenPixelHeight), nColour);
	CHECK_EQ_HEX (rPicture.At (BorderLeft - 1, 100), nColour);
	CHECK_EQ_HEX (rPicture.At (BorderLeft + ScreenPixelWidth, 100), nColour);
}

static const uint32_t Black = MakeRGB (0, 0, 0);
static const uint32_t Blue = MakeRGB (0, 0, 0xA0);
static const uint32_t Red = MakeRGB (0xA0, 0, 0);
static const uint32_t Green = MakeRGB (0, 0xA0, 0);
static const uint32_t Yellow = MakeRGB (0xA0, 0xA0, 0);
static const uint32_t White = MakeRGB (0xA0, 0xA0, 0xA0);

// --- decoder -------------------------------------------------------------

static void TestDecoderFields (void)
{
	TEvent Event;

	CHECK (DecodeWord (EmptyPairWord, Event) == WordEmpty);

	// memory write to 0xDAFF: A15 in half-word 0 bit 8
	uint32_t nWord = Mem (0xDAFF, 0xA5);
	CHECK_EQ_HEX (nWord & 0xFFFF, 0x4000 | 0x0100 | 0xA5);	// V, KIND 00, A15, DATA
	CHECK_EQ_HEX (nWord >> 16, 0x8000 | 0x5AFF);		// H, A14..A0
	CHECK (DecodeWord (nWord, Event) == WordEvent);
	CHECK (Event.Kind == KindMemory);
	CHECK (Event.Data == 0xA5);
	CHECK (Event.Address == 0xDAFF);
	CHECK (!Event.Overflow);
	CHECK (!Event.Frame);

	// I/O write with OVF and F
	nWord = EncodeEvent (KindIo, 0x7FFD, 0x17, true, true);
	CHECK_EQ_HEX (nWord & 0xFFFF, 0x4000 | 0x2000 | 0x1000 | 0x0400 | 0x17);
	CHECK (DecodeWord (nWord, Event) == WordEvent);
	CHECK (Event.Kind == KindIo);
	CHECK (Event.Address == 0x7FFD);
	CHECK (Event.Data == 0x17);
	CHECK (Event.Overflow);
	CHECK (Event.Frame);

	// Reset: KIND 10, address 0, DATA 0, with or without OVF.
	CHECK_EQ_HEX (ResetWord (), 0x80004800u);
	CHECK (DecodeWord (ResetWord (), Event) == WordEvent);
	CHECK (Event.Kind == KindReset);
	CHECK (!Event.Overflow);
	CHECK (DecodeWord (ResetWord (true), Event) == WordEvent);
	CHECK (Event.Kind == KindReset);
	CHECK (Event.Overflow);
}

static void TestDecoderMismatch (void)
{
	TEvent Event;

	// half-word 0 with H = 1
	CHECK (DecodeWord (Io (0x00FE, 1) | 0x8000u, Event) == WordMismatch);
	// half-word 1 with H = 0
	CHECK (DecodeWord (Io (0x00FE, 1) & ~0x80000000u, Event) == WordMismatch);
	// halves swapped: what a one-read slip looks like
	uint32_t nGood = Mem (0x4123, 0x55);
	uint32_t nSwapped = (nGood >> 16) | (nGood << 16);
	CHECK (DecodeWord (nSwapped, Event) == WordMismatch);
	// an empty pair also carries H bits
	CHECK (DecodeWord (0x00000000u, Event) == WordMismatch);
	CHECK (DecodeWord (0xFFFFFFFFu, Event) == WordMismatch);
}

static void TestDecoderMalformed (void)
{
	TEvent Event;

	CHECK (DecodeWord (EncodeEvent (KindReserved, 0x1234, 0), Event) == WordMalformed);
	CHECK (DecodeWord (EncodeEvent (KindReset, 0, 0x01), Event) == WordMalformed);	// DATA not 0
	CHECK (DecodeWord (EncodeEvent (KindReset, 0x0001, 0), Event) == WordMalformed);	// address not 0
	CHECK (DecodeWord (EncodeEvent (KindReset, 0x8000, 0), Event) == WordMalformed);	// A15 not 0
	CHECK (DecodeWord (Io (0x00FE, 0) | 0x0200u, Event) == WordMalformed);		// bit 9
	// V = 0 is only valid as the exact empty pair 0x0000, 0x8000
	CHECK (DecodeWord (EmptyPairWord | 0x0001u, Event) == WordMalformed);
	CHECK (DecodeWord (EmptyPairWord | 0x2000u, Event) == WordMalformed);	// OVF on an empty pair
	CHECK (DecodeWord (EmptyPairWord | 0x1000u, Event) == WordMalformed);	// F on an empty pair
	CHECK (DecodeWord (EmptyPairWord | 0x00010000u, Event) == WordMalformed);
}

// --- intake --------------------------------------------------------------

static void TestIntakeOverflow (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	Feed (*pState, Counters, {
		EmptyPairWord,
		Mem (0x4000, 0x11),
		EncodeEvent (KindMemory, 0x4001, 0x22, true),
		EmptyPairWord,
		EmptyPairWord,
		EncodeEvent (KindIo, 0x00FE, 0x05, true, true),
		EncodeEvent (KindReset, 0, 0x80),			// malformed: DATA not 0
	});

	CHECK (Counters.Events == 3);
	CHECK (Counters.EmptyPairs == 3);
	CHECK (Counters.Overflows == 2);
	CHECK (Counters.FrameEvents == 1);
	CHECK (Counters.Malformed == 1);
	CHECK (Counters.Mismatches == 0);
	// an event with OVF or F set is still applied
	CHECK (pState->State.Ram[5][0] == 0x11);
	CHECK (pState->State.Ram[5][1] == 0x22);
	CHECK (pState->State.Border == 5);
}

static void TestIntakeMismatch (void)
{
	auto pState = NewState ();
	CStateSink Sink (*pState, Machine48K);
	TIntakeCounters Counters = {};

	std::vector<uint32_t> Words = {
		Mem (0x4010, 0xAA),
		EmptyPairWord,
		Mem (0x4011, 0xBB) | 0x8000u,		// bad H in half-word 0
		Mem (0x4012, 0xCC),			// must not be applied
		Io (0x00FE, 0x03),			// must not be applied
	};

	bool bMismatch = false;
	size_t n = ProcessWords (Words.data (), Words.size (), Sink, Counters, bMismatch);

	CHECK (bMismatch);
	CHECK (n == 3);
	CHECK (Counters.Mismatches == 1);
	CHECK (Counters.Events == 1);
	CHECK (Counters.EmptyPairs == 1);
	CHECK (pState->State.Ram[5][0x10] == 0xAA);
	CHECK (pState->State.Ram[5][0x11] == 0x00);
	CHECK (pState->State.Ram[5][0x12] == 0x00);
	CHECK (pState->State.Border == 0);

	// After a resync the caller resumes with fresh words.
	std::vector<uint32_t> After = { Mem (0x4013, 0xDD) };
	n = ProcessWords (After.data (), After.size (), Sink, Counters, bMismatch);
	CHECK (!bMismatch);
	CHECK (n == 1);
	CHECK (pState->State.Ram[5][0x13] == 0xDD);
}

// --- state model: decodes and paging -------------------------------------

static void TestMemoryMap48K (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	Feed (*pState, Counters, {
		Mem (0x0000, 0x99),		// ROM: no effect
		Mem (0x4000, 0x01),
		Mem (0x8000, 0x02),
		Mem (0xC000, 0x03),
		Io (0x7FFD, 0x0F),		// not a 48K port
		Mem (0xC001, 0x04),
	});

	const CSpectrumState &S = pState->State;
	CHECK (S.Ram[5][0] == 0x01);
	CHECK (S.Ram[2][0] == 0x02);
	CHECK (S.Ram[0][0] == 0x03);
	CHECK (S.Ram[0][1] == 0x04);
	CHECK (S.Port7FFD == 0);
	for (unsigned b = 0; b < BankCount; b++)
	{
		CHECK (b == 5 || b == 2 || b == 0 || S.Ram[b][0] == 0);
	}
}

static void TestPagingDecode128K (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	const CSpectrumState &S = pState->State;

	// 128K/+2: A15 = 0 and A1 = 0 is enough, so 0x3FFD and 0x1FFD page.
	Feed (*pState, Counters, { Io (0x3FFD, 0x03), Mem (0xC000, 0x33) }, Machine128K);
	CHECK (S.Port7FFD == 0x03);
	CHECK (S.Ram[3][0] == 0x33);
	Feed (*pState, Counters, { Io (0x1FFD, 0x04), Mem (0xC000, 0x44) }, Machine128K);
	CHECK (S.Port7FFD == 0x04);
	CHECK (S.Port1FFD == 0x00);
	CHECK (S.Ram[4][0] == 0x44);
	// A15 = 1 or A1 = 1: not 0x7FFD
	Feed (*pState, Counters, { Io (0xFFFD, 0x01), Io (0x7FFF, 0x01) }, Machine128K);
	CHECK (S.Port7FFD == 0x04);

	// 0x7FFC also matches the ULA (A0 = 0): both act.
	Feed (*pState, Counters, { Io (0x7FFC, 0x06) }, Machine128K);
	CHECK (S.Port7FFD == 0x06);
	CHECK (S.Border == 0x06);

	// Lock: bit 5 stops further paging until reset.
	Feed (*pState, Counters, { Io (0x7FFD, 0x21), Io (0x7FFD, 0x07) }, Machine128K);
	CHECK (S.Port7FFD == 0x21);
	CHECK (S.PagingLocked ());
	Feed (*pState, Counters, { ResetWord () }, Machine128K);
	CHECK (!S.PagingLocked ());
	CHECK (S.Port7FFD == 0);
	Feed (*pState, Counters, { Io (0x7FFD, 0x07) }, Machine128K);
	CHECK (S.Port7FFD == 0x07);
}

static void TestPagingDecodePlus3 (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	const CSpectrumState &S = pState->State;

	// +2A/+3: 0x7FFD needs A15..A14 = 01, A1 = 0.
	Feed (*pState, Counters, { Io (0x3FFD, 0x03) }, MachinePlus3);
	CHECK (S.Port7FFD == 0x00);
	Feed (*pState, Counters, { Io (0x7FFD, 0x03), Mem (0xC000, 0x33) }, MachinePlus3);
	CHECK (S.Port7FFD == 0x03);
	CHECK (S.Ram[3][0] == 0x33);

	// 0x1FFD needs A15..A12 = 0001, A1 = 0.
	Feed (*pState, Counters, { Io (0x1FFD, 0x04) }, MachinePlus3);
	CHECK (S.Port1FFD == 0x04);
	CHECK (S.Port7FFD == 0x03);
	Feed (*pState, Counters, { Io (0x3FFD, 0x00), Io (0x1FFF, 0x00) }, MachinePlus3);
	CHECK (S.Port1FFD == 0x04);

	// Special paging, bits 2, 1 = 01: banks 4, 5, 6, 7.
	Feed (*pState, Counters, {
		Io (0x1FFD, 0x03),
		Mem (0x0000, 0x40), Mem (0x4000, 0x50), Mem (0x8000, 0x60), Mem (0xC000, 0x70),
	}, MachinePlus3);
	CHECK (S.Ram[4][0] == 0x40);
	CHECK (S.Ram[5][0] == 0x50);
	CHECK (S.Ram[6][0] == 0x60);
	CHECK (S.Ram[7][0] == 0x70);

	// Bits 2, 1 = 11: banks 4, 7, 6, 3, so 0x4000 is bank 7.
	Feed (*pState, Counters, { Io (0x1FFD, 0x07), Mem (0x4001, 0x77), Mem (0xC001, 0x31) },
	      MachinePlus3);
	CHECK (S.Ram[7][1] == 0x77);
	CHECK (S.Ram[3][1] == 0x31);

	// Bits 2, 1 = 00: banks 0, 1, 2, 3.
	Feed (*pState, Counters, { Io (0x1FFD, 0x01), Mem (0x0002, 0x02), Mem (0x4002, 0x12) },
	      MachinePlus3);
	CHECK (S.Ram[0][2] == 0x02);
	CHECK (S.Ram[1][2] == 0x12);

	// The lock stops both ports.
	Feed (*pState, Counters, { Io (0x1FFD, 0x00), Io (0x7FFD, 0x20),
				   Io (0x1FFD, 0x01), Io (0x7FFD, 0x07) }, MachinePlus3);
	CHECK (S.Port7FFD == 0x20);
	CHECK (S.Port1FFD == 0x00);
	Feed (*pState, Counters, { ResetWord (), Io (0x1FFD, 0x01) }, MachinePlus3);
	CHECK (S.Port1FFD == 0x01);
}

// --- renderer, still pictures -------------------------------------------

static void TestAttributePixelPattern (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	// Row 0, column 0: pixels 0xB1, attribute BRIGHT, paper black, ink white.
	// Row 9 (character row 1, pixel line 1), column 5: pixels 0x38,
	// attribute paper blue (1), ink yellow (6), not bright.
	// Row 191 (last), column 31: pixels 0x83, attribute paper green, ink red.
	Feed (*pState, Counters, {
		Mem (PixelAddr (0, 0), 0xB1),
		Mem (AttrAddr (0, 0), 0x47),
		Mem (PixelAddr (9, 5), 0x38),
		Mem (AttrAddr (9, 5), (1 << 3) | 6),
		Mem (PixelAddr (191, 31), 0x83),
		Mem (AttrAddr (191, 31), (4 << 3) | 2),
	});

	// Screen layout spot checks
	CHECK (PixelRowOffset (1) == 0x0100);
	CHECK (PixelRowOffset (8) == 0x0020);
	CHECK (PixelRowOffset (64) == 0x0800);
	CHECK (PixelRowOffset (191) == 0x17E0);
	CHECK (AttributeRowOffset (191) == 0x1AE0);

	// Geometry: 640 x 360, picture 352 x 296 centred.
	CHECK (PictureWidth == 352 && PictureHeight == 296);
	CHECK (PictureLeft == 144 && PictureTop == 32);

	CTestPicture Picture;
	Render (*pState, 0, Picture);

	CheckCell (Picture, 0, 0, 0xB1, MakeRGB (0xFF, 0xFF, 0xFF), Black);
	CheckCell (Picture, 5, 9, 0x38, Yellow, Blue);
	CheckCell (Picture, 31, 191, 0x83, Red, Green);
	// the attribute covers the whole 8 x 8 cell
	CheckCell (Picture, 0, 7, 0x00, MakeRGB (0xFF, 0xFF, 0xFF), Black);
	// an untouched cell is black paper on black
	CheckCell (Picture, 10, 100, 0x00, Black, Black);
	// the pitch padding is not written
	CHECK_EQ_HEX (Picture.Pixels[PictureWidth], 0x12345678u);
	CheckBorder (Picture, Black);
}

static void TestShadowScreen (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	unsigned nPix = PixelRowOffset (50) + 3;
	unsigned nAttr = AttributeRowOffset (50) + 3;

	// Bank 5 at 0x4000; bank 7 paged in at 0xC000 with 0x7FFD = 7.
	Feed (*pState, Counters, {
		Mem ((uint16_t) (0x4000 + nPix), 0xF0),
		Mem ((uint16_t) (0x4000 + nAttr), 0x07),		// white on black
		Io (0x7FFD, 0x07),
		Mem ((uint16_t) (0xC000 + nPix), 0x0F),
		Mem ((uint16_t) (0xC000 + nAttr), 0x02 | (1 << 3)),	// red on blue
		Io (0x7FFD, 0x00),
	}, MachinePlus3);

	CTestPicture Picture;
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 3, 50, 0xF0, White, Black);

	// Port 0x7FFD bit 3 selects bank 7; other bits do not matter.
	Feed (*pState, Counters, { Io (0x7FFD, 0x08 | 0x01 | 0x10) }, MachinePlus3);
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 3, 50, 0x0F, Red, Blue);

	// A 48K always shows bank 5.
	Render (*pState, 0, Picture, Machine48K);
	CheckCell (Picture, 3, 50, 0xF0, White, Black);

	// Writes to the hidden bank still land while the other is shown.
	Feed (*pState, Counters, { Mem ((uint16_t) (0x4000 + nPix), 0xFF) }, MachinePlus3);
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 3, 50, 0x0F, Red, Blue);

	Feed (*pState, Counters, { Io (0x7FFD, 0x00) }, MachinePlus3);
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 3, 50, 0xFF, White, Black);
}

static void TestBorderChange (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	CTestPicture Picture;

	// Bits 3 and 4 (MIC, EAR) are not border bits; every even port is the ULA.
	Feed (*pState, Counters, { Io (0x00FE, 0x18 | 0x02) });
	Render (*pState, 0, Picture);
	CheckBorder (Picture, Red);

	Feed (*pState, Counters, { Io (0x12F0, 0x05) });
	Render (*pState, 0, Picture);
	CheckBorder (Picture, MakeRGB (0, 0xA0, 0xA0));

	// An odd port is not the ULA.
	Feed (*pState, Counters, { Io (0x00FF, 0x01) });
	Render (*pState, 0, Picture);
	CheckBorder (Picture, MakeRGB (0, 0xA0, 0xA0));
	// the screen area is not border
	CHECK_EQ_HEX (Picture.At (BorderLeft, BorderTop), Black);
}

static void TestFlashPhases (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	// FLASH set, ink blue, paper yellow, pixels 0xF0. A second cell without
	// FLASH must never swap.
	Feed (*pState, Counters, {
		Mem (PixelAddr (16, 0), 0xF0),
		Mem (AttrAddr (16, 0), 0x80 | (6 << 3) | 1),
		Mem (PixelAddr (16, 1), 0xF0),
		Mem (AttrAddr (16, 1), (6 << 3) | 1),
	});

	CTestPicture Picture;

	const uint32_t Frames[] = { 0, 15, 16, 31, 32, 47, 48 };
	for (uint32_t nFrame : Frames)
	{
		bool bSwapped = ((nFrame / 16) & 1) != 0;
		CHECK (FlashSwapped (nFrame) == bSwapped);
		Render (*pState, nFrame, Picture);
		if (bSwapped)
		{
			CheckCell (Picture, 0, 16, 0xF0, Yellow, Blue);
		}
		else
		{
			CheckCell (Picture, 0, 16, 0xF0, Blue, Yellow);
		}
		CheckCell (Picture, 1, 16, 0xF0, Blue, Yellow);
	}
}

static void TestUlaPlusColour (void)
{
	CHECK_EQ_HEX (UlaPlusColour (0x00), MakeRGB (0, 0, 0));
	CHECK_EQ_HEX (UlaPlusColour (0xFF), MakeRGB (255, 255, 255));
	CHECK_EQ_HEX (UlaPlusColour (0x01), MakeRGB (0, 0, 109));	// blue 01 -> 011
	CHECK_EQ_HEX (UlaPlusColour (0x02), MakeRGB (0, 0, 182));	// blue 10 -> 101
	CHECK_EQ_HEX (UlaPlusColour (0x03), MakeRGB (0, 0, 255));	// blue 11 -> 111
	CHECK_EQ_HEX (UlaPlusColour (0x1C), MakeRGB (255, 0, 0));	// red 111
	CHECK_EQ_HEX (UlaPlusColour (0x20), MakeRGB (0, 36, 0));	// green 001
	CHECK_EQ_HEX (UlaPlusColour (0x80), MakeRGB (0, 146, 0));	// green 100
}

static void TestUlaPlusPaletteAndMode (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	const CSpectrumState &S = pState->State;

	// Cell (2, row 40): FLASH and BRIGHT set (group 3), ink 5, paper 2.
	uint8_t nAttrByte = 0x80 | 0x40 | (2 << 3) | 5;

	std::vector<uint32_t> Words = {
		Mem (PixelAddr (40, 2), 0xCC),
		Mem (AttrAddr (40, 2), nAttrByte),
		Io (0x00FE, 0x04),				// border 4
	};
	// Palette upload: entry i = i * 4 + 3, through register then data port.
	for (unsigned i = 0; i < 64; i++)
	{
		Words.push_back (Io (0xBF3B, (uint8_t) i));
		Words.push_back (Io (0xFF3B, (uint8_t) (i * 4 + 3)));
	}
	Feed (*pState, Counters, Words);
	CHECK (S.UlaPlusPalette[63] == (uint8_t) (63 * 4 + 3));
	CHECK (!S.UlaPlusOn ());

	// Palette loaded but mode off: normal colours, FLASH swaps in phase 1.
	CTestPicture Picture;
	Render (*pState, 16, Picture);
	CheckCell (Picture, 2, 40, 0xCC, MakeRGB (0xFF, 0, 0), MakeRGB (0, 0xFF, 0xFF));
	CheckBorder (Picture, Green);

	// Mode group (register 0x40), data bit 0 = 1: ULAplus on.
	Feed (*pState, Counters, { Io (0xBF3B, 0x40), Io (0xFF3B, 0x01) });
	CHECK (S.UlaPlusOn ());

	uint32_t nInk = UlaPlusColour (S.UlaPlusPalette[48 + 5]);
	uint32_t nPaper = UlaPlusColour (S.UlaPlusPalette[48 + 8 + 2]);
	uint32_t nBorder = UlaPlusColour (S.UlaPlusPalette[8 + 4]);
	for (uint32_t nFrame : { 0u, 16u })	// FLASH does not swap in ULAplus mode
	{
		Render (*pState, nFrame, Picture);
		CheckCell (Picture, 2, 40, 0xCC, nInk, nPaper);
		CheckBorder (Picture, nBorder);
	}
	// an untouched cell (attribute 0) uses group 0 entries 0 and 8
	CheckCell (Picture, 0, 0, 0x00, UlaPlusColour (S.UlaPlusPalette[0]),
		   UlaPlusColour (S.UlaPlusPalette[8]));

	// A palette write while on changes the picture.
	Feed (*pState, Counters, { Io (0xBF3B, 48 + 5), Io (0xFF3B, 0x1C) });	// pure red
	Render (*pState, 0, Picture);
	CheckCell (Picture, 2, 40, 0xCC, MakeRGB (255, 0, 0), nPaper);

	// Groups 2 and 3 are ignored; ports that miss the decode do nothing.
	Feed (*pState, Counters, {
		Io (0xBF3B, 0x80), Io (0xFF3B, 0x00),
		Io (0xBF3B, 0xC0), Io (0xFF3B, 0x00),
		Io (0xBF3B, 0x00), Io (0xFF7B, 0x55),	// A6 = 1
		Io (0xFF3F, 0x55),			// A2 = 1
		Io (0x7F3B, 0x55),			// A15 = 0
	});
	CHECK (S.UlaPlusOn ());
	CHECK (S.UlaPlusPalette[0] == 3);

	// Mode data 0: ULAplus off, back to normal colours.
	Feed (*pState, Counters, { Io (0xBF3B, 0x40), Io (0xFF3B, 0x00) });
	CHECK (!S.UlaPlusOn ());
	Render (*pState, 0, Picture);
	CheckCell (Picture, 2, 40, 0xCC, MakeRGB (0, 0xFF, 0xFF), MakeRGB (0xFF, 0, 0));
	CheckBorder (Picture, Green);
}

// --- reset ---------------------------------------------------------------

static void TestResetShadowScreen (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	const CSpectrumState &S = pState->State;

	unsigned nPix = PixelRowOffset (60) + 4;
	unsigned nAttr = AttributeRowOffset (60) + 4;

	Feed (*pState, Counters, {
		Mem ((uint16_t) (0x4000 + nPix), 0xF0),
		Mem ((uint16_t) (0x4000 + nAttr), 0x07),		// white on black
		Io (0x7FFD, 0x07),
		Mem ((uint16_t) (0xC000 + nPix), 0x0F),
		Mem ((uint16_t) (0xC000 + nAttr), 0x02 | (1 << 3)),	// red on blue
		Io (0x1FFD, 0x04),
		Io (0x7FFD, 0x08 | 0x07),				// show bank 7
	}, MachinePlus3);

	CTestPicture Picture;
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 4, 60, 0x0F, Red, Blue);

	Feed (*pState, Counters, { ResetWord () }, MachinePlus3);
	CHECK (Counters.Resets == 1);
	CHECK (S.Port7FFD == 0);
	CHECK (S.Port1FFD == 0);
	Render (*pState, 0, Picture, MachinePlus3);
	CheckCell (Picture, 4, 60, 0xF0, White, Black);
	// RAM keeps its contents
	CHECK (S.Ram[7][nPix] == 0x0F);
}

static void TestResetUlaPlus (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};
	const CSpectrumState &S = pState->State;

	std::vector<uint32_t> Words = {
		Mem (PixelAddr (20, 7), 0xAA),
		Mem (AttrAddr (20, 7), (1 << 3) | 6),			// yellow on blue
		Io (0x00FE, 0x02),					// border red
	};
	for (unsigned i = 0; i < 64; i++)
	{
		Words.push_back (Io (0xBF3B, (uint8_t) i));
		Words.push_back (Io (0xFF3B, (uint8_t) (255 - i)));
	}
	Words.push_back (Io (0xBF3B, 0x40));
	Words.push_back (Io (0xFF3B, 0x01));		// ULAplus on
	Words.push_back (Io (0xBF3B, 0x17));		// register latch not 0
	Feed (*pState, Counters, Words);
	CHECK (S.UlaPlusOn ());
	CHECK (S.UlaPlusRegister == 0x17);

	Feed (*pState, Counters, { ResetWord () });
	CHECK (Counters.Resets == 1);
	CHECK (!S.UlaPlusOn ());
	CHECK (S.UlaPlusMode == 0);
	CHECK (S.UlaPlusRegister == 0);
	CHECK (S.Border == 2);
	for (unsigned i = 0; i < 64; i++)
	{
		CHECK (S.UlaPlusPalette[i] == (uint8_t) (255 - i));
	}

	// normal colours, border unchanged
	CTestPicture Picture;
	Render (*pState, 0, Picture);
	CheckCell (Picture, 7, 20, 0xAA, Yellow, Blue);
	CheckBorder (Picture, Red);

	// The register latch is 0 after the reset, so a data-port write with no
	// register write before it sets palette entry 0.
	Feed (*pState, Counters, { Io (0xFF3B, 0x1C) });
	CHECK (S.UlaPlusPalette[0] == 0x1C);
	CHECK (!S.UlaPlusOn ());

	// The kept palette is used again when software turns ULAplus back on.
	Feed (*pState, Counters, { Io (0xBF3B, 0x40), Io (0xFF3B, 0x01) });
	Render (*pState, 0, Picture);
	CheckBorder (Picture, UlaPlusColour (S.UlaPlusPalette[8 + 2]));
	CHECK (S.UlaPlusPalette[8 + 2] == (uint8_t) (255 - 10));
}

static void TestResetBorderAndOverflow (void)
{
	auto pState = NewState ();
	TIntakeCounters Counters = {};

	Feed (*pState, Counters, {
		Io (0x00FE, 0x05),
		ResetWord (true),		// a reset event can carry OVF
		EmptyPairWord,
		ResetWord (),
	});
	CHECK (Counters.Resets == 2);
	CHECK (Counters.Overflows == 1);
	CHECK (Counters.Events == 3);
	CHECK (Counters.Malformed == 0);
	CHECK (pState->State.Border == 5);

	CTestPicture Picture;
	Render (*pState, 0, Picture);
	CheckBorder (Picture, MakeRGB (0, 0xA0, 0xA0));
}

// --- event log -----------------------------------------------------------

static void TestEventLogFullDrops (void)
{
	std::unique_ptr<CEventLog> pLog (new CEventLog);

	TTimedEvent Event = { 0, 0, 0x4000, KindMemory, 0 };
	for (uint32_t i = 0; i < CEventLog::Entries; i++)
	{
		CHECK (pLog->Push (Event) || !"push failed early");
		if (s_bTestFailed)
		{
			return;
		}
	}
	// Full: the producer does not wait; the entry is dropped and counted.
	CHECK (!pLog->Push (Event));
	CHECK (pLog->Dropped () == 1);

	pLog->Complete (1, Machine48K, false);
	CHECK (pLog->Completed () == 1);
	TFrameSpan Span = pLog->TakeFrame (0);
	CHECK (Span.nCount == CEventLog::Entries);
	pLog->Release (Span);

	// Space again after the consumer releases.
	CHECK (pLog->Push (Event));
	CHECK (pLog->Dropped () == 1);
}

// --- simulated Spectrum for the timed tests -------------------------------

static const uint64_t TickHz = 19200000;	// the Pi's generic timer

class CSim
{
public:
	typedef std::function<void (const CFramePresenter &, const CTestPicture &)> TPresentFunc;

	// Hw: the timing of the simulated machine.
	CSim (TMachine Hw, TMachineChoice Choice = ChooseAuto)
	:	m_Timing (MachineTiming (Hw)),
		m_pLog (new CEventLog),
		m_pTimeline (new CTimeline (m_pLog.get (), TickHz, Choice)),
		m_pPresenter (new CFramePresenter (m_pLog.get ())),
		m_nRandom (12345)
	{
		m_pTimeline->Start (StartTicks);
	}

	// Ticks at Spectrum frame f, T-state t (the interrupt of frame 0 is
	// at StartTicks + 3 ms).
	double SpectrumTicks (uint32_t f, double t) const
	{
		double nT = (double) f * m_Timing.FrameTStates + t;
		return StartTicks + 0.003 * TickHz + nT * TickHz / m_Timing.ClockHz;
	}

	void Write (uint32_t f, double t, uint32_t nWord)
	{
		m_Writes.push_back ({ SpectrumTicks (f, t), nWord });
	}

	// F events for frames [f0, f1): the push of PC after the acknowledge,
	// FrameEventTStates plus 0 to 22 T-states after the interrupt. The
	// pushed bytes are the simulated frame number, so a test can tell which
	// frame was presented (SimFrame ()).
	void FrameEvents (uint32_t f0, uint32_t f1)
	{
		for (uint32_t f = f0; f < f1; f++)
		{
			double nDelay = (double) (Random () % 23);
			Write (f, FrameEventTStates + nDelay, Mem (0x9FFE, (uint8_t) (f >> 8), true));
			Write (f, FrameEventTStates + nDelay + 3, Mem (0x9FFD, (uint8_t) f));
		}
	}

	// The same frame number bytes with no F event, for frames [f0, f1).
	void Markers (uint32_t f0, uint32_t f1)
	{
		for (uint32_t f = f0; f < f1; f++)
		{
			Write (f, 200, Mem (0x9FFE, (uint8_t) (f >> 8)));
			Write (f, 203, Mem (0x9FFD, (uint8_t) f));
		}
	}

	// The simulated frame number of the frame just presented.
	static uint32_t SimFrame (const CFramePresenter &rPresenter)
	{
		const CSpectrumState &S = rPresenter.RenderState ().State;
		return (uint32_t) (S.Ram[2][0x1FFE] << 8) | S.Ram[2][0x1FFD];
	}

	// Runs every write in time order, with a vertical sync every 20.000 ms,
	// up to Spectrum frame nEndFrame.
	void Run (uint32_t nEndFrame, TPresentFunc Present)
	{
		std::stable_sort (m_Writes.begin (), m_Writes.end (),
				  [] (const TWrite &a, const TWrite &b) { return a.nTicks < b.nTicks; });

		// Arrival: write + 1.0 us + 0 to 0.8 us, in order, whole ticks.
		std::vector<uint64_t> Arrivals;
		uint64_t nLast = 0;
		for (const TWrite &rWrite : m_Writes)
		{
			double nLatencyNs = 1000.0 + (Random () % 801);
			uint64_t nArrival = (uint64_t) (rWrite.nTicks + nLatencyNs * TickHz / 1e9);
			nLast = std::max (nLast, nArrival);
			Arrivals.push_back (nLast);
		}

		const double nVsyncTicks = 0.020 * TickHz;
		double nVsync = StartTicks + 0.0071 * TickHz;
		uint64_t nEnd = (uint64_t) SpectrumTicks (nEndFrame, 0);
		size_t i = 0;

		while (nVsync < nEnd)
		{
			while (i < m_Writes.size () && Arrivals[i] <= (uint64_t) nVsync)
			{
				m_pTimeline->BeginBatch (Arrivals[i]);
				bool bMismatch = false;
				ProcessWords (&m_Writes[i].nWord, 1, *m_pTimeline, m_Counters, bMismatch);
				m_pTimeline->EndBatch ();
				m_pTimeline->Advance (Arrivals[i]);
				i++;
			}

			m_pTimeline->Advance ((uint64_t) nVsync);
			if (m_pPresenter->Present (m_Picture.Data (), TestPitch) && Present)
			{
				Present (*m_pPresenter, m_Picture);
			}

			nVsync += nVsyncTicks;
		}
	}

	CTimeline &Timeline (void)		{ return *m_pTimeline; }
	CFramePresenter &Presenter (void)	{ return *m_pPresenter; }
	CEventLog &Log (void)			{ return *m_pLog; }
	const TMachineTiming &Timing (void) const { return m_Timing; }

	uint32_t Random (void)
	{
		m_nRandom = m_nRandom * 1103515245u + 12345u;
		return (m_nRandom >> 16) & 0x7FFF;
	}

private:
	static constexpr double StartTicks = 1.0 * TickHz;

	struct TWrite
	{
		double		nTicks;
		uint32_t	nWord;
	};

	const TMachineTiming		&m_Timing;
	std::unique_ptr<CEventLog>	m_pLog;
	std::unique_ptr<CTimeline>	m_pTimeline;
	std::unique_ptr<CFramePresenter> m_pPresenter;
	std::vector<TWrite>		m_Writes;
	TIntakeCounters			m_Counters = {};
	CTestPicture			m_Picture;
	uint32_t			m_nRandom;
};

// T-state at which the ULA shows 8-pixel block b of picture line y.
static uint32_t BlockT (const TMachineTiming &rTiming, unsigned y, unsigned b)
{
	return BlockTState (rTiming, y, b);
}

// --- frame lock ----------------------------------------------------------

static void CheckLock (TMachine Hw, TMachineChoice Choice, TMachine Expect, uint64_t nPeriodNs)
{
	CSim Sim (Hw, Choice);
	Sim.FrameEvents (0, 400);
	Sim.Run (400, nullptr);

	const CFrameLock &rLock = Sim.Timeline ().Lock ();
	CHECK (rLock.Locked ());
	CHECK (rLock.Machine () == Expect);
	CHECK (rLock.Acquisitions () == 1);
	int64_t nError = (int64_t) rLock.PeriodNs () - (int64_t) nPeriodNs;
	CHECK (nError > -100 && nError < 100);
	if (nError <= -100 || nError >= 100)
	{
		printf ("    period %llu ns, expected %llu\n",
			(unsigned long long) rLock.PeriodNs (), (unsigned long long) nPeriodNs);
	}
}

static void TestFrameLockMachines (void)
{
	CheckLock (Machine48K, ChooseAuto, Machine48K, FramePeriodNs48K);
	CheckLock (Machine128K, ChooseAuto, MachinePlus3, FramePeriodNs128K);
	CheckLock (Machine128K, ChooseAuto128, Machine128K, FramePeriodNs128K);
	CheckLock (Machine48K, Choose128K, Machine128K, FramePeriodNs48K);
}

// The lock's position for an ordinary write, against the T-state it was
// written at, after 600 frames of F events with random Z80 delay and
// transport latency.
static void TestPositionAccuracy (void)
{
	for (TMachine Hw : { Machine48K, Machine128K })
	{
		std::unique_ptr<CEventLog> pLog (new CEventLog);
		CTimeline Timeline (pLog.get (), TickHz);
		const TMachineTiming &rTiming = MachineTiming (Hw);
		const double nTicksPerT = (double) TickHz / rTiming.ClockHz;
		const double nInt0 = 1.003 * TickHz;	// interrupt of frame 0
		Timeline.Start (TickHz);

		uint32_t nRandom = 99;
		auto Random = [&nRandom] () { nRandom = nRandom * 1103515245u + 12345u;
					      return (nRandom >> 16) & 0x7FFF; };
		auto Latency = [&] () { return (1000.0 + (Random () % 801)) * TickHz / 1e9; };

		for (uint32_t f = 0; f < 600; f++)
		{
			double nInt = nInt0 + (double) f * rTiming.FrameTStates * nTicksPerT;
			uint64_t nArrival = (uint64_t) (nInt + (FrameEventTStates + Random () % 23)
							* nTicksPerT + Latency ());

			TIntakeCounters Counters = {};
			bool bMismatch;
			uint32_t nWord = Mem (0x9FFE, 0, true);
			Timeline.BeginBatch (nArrival);
			ProcessWords (&nWord, 1, Timeline, Counters, bMismatch);
			Timeline.EndBatch ();
			Timeline.Advance (nArrival);
		}

		const CFrameLock &rLock = Timeline.Lock ();
		CHECK (rLock.Locked ());

		// Writes in the next frames, at the average latency.
		int nWorst = 0;
		for (uint32_t k = 0; k < 200; k++)
		{
			uint32_t f = 600 + k / 50;
			uint32_t t = 500 + (k * 337) % (rTiming.FrameTStates - 1000);
			double nTicks =   nInt0 + ((double) f * rTiming.FrameTStates + t) * nTicksPerT
					+ 1400.0 * TickHz / 1e9;
			uint32_t nFrame, nTState;
			rLock.Position ((uint64_t) nTicks, nFrame, nTState);

			int nError = (int) nTState - (int) t;
			int nHalf = (int) rTiming.FrameTStates / 2;
			if (nError > nHalf) nError -= (int) rTiming.FrameTStates;
			if (nError < -nHalf) nError += (int) rTiming.FrameTStates;
			nWorst = std::max (nWorst, nError < 0 ? -nError : nError);
		}
		printf ("    %s: worst position error %d T-states\n",
			Hw == Machine48K ? "48K" : "128K class", nWorst);
		CHECK (nWorst <= 3);
	}
}

// --- timed rendering ------------------------------------------------------

// Multicolour: one attribute rewritten for every line of a character row,
// 40 T-states before the ULA reads it.
static void TestMulticolour (void)
{
	CSim Sim (Machine48K);
	const TMachineTiming &rTiming = Sim.Timing ();
	const uint32_t nFirst = 300, nLast = 310;
	const unsigned nRow = 5, nCol = 10;

	Sim.FrameEvents (0, nLast + 4);
	for (uint32_t f = nFirst; f < nLast; f++)
	{
		for (unsigned k = 0; k < 8; k++)
		{
			unsigned y = nRow * 8 + k;
			uint32_t t = BlockT (rTiming, BorderTop + y, ScreenFirstBlock + nCol) - 40;
			Sim.Write (f, t, Mem (AttrAddr (y, nCol), (uint8_t) ((k << 3) | 7)));
		}
	}

	unsigned nShown = 0;
	Sim.Run (nLast + 4, [&] (const CFramePresenter &rPresenter, const CTestPicture &rPicture) {
		uint32_t f = CSim::SimFrame (rPresenter);
		if (f < nFirst || f >= nLast)
		{
			return;
		}
		nShown++;
		for (unsigned k = 0; k < 8; k++)
		{
			CheckCell (rPicture, nCol, nRow * 8 + k, 0x00, White, SpectrumColour (k));
		}
		// The cells beside it keep attribute 0.
		CheckCell (rPicture, nCol - 1, nRow * 8 + 3, 0x00, Black, Black);
		CheckCell (rPicture, nCol + 1, nRow * 8 + 3, 0x00, Black, Black);
	});
	CHECK (nShown >= 8);
}

// The renderer puts a border change at the 8-pixel block whose T-state is
// after the event: an event at T shows from the first block shown after T.
static void TestBorderBlockExact (void)
{
	auto pState = NewState ();
	const TMachineTiming &rTiming = MachineTiming (Machine48K);
	const unsigned y = 150, b = 40;		// picture line, right border block

	for (uint32_t nDelta : { 1u, 4u, 0u })
	{
		pState->Reset ();
		TTimedEvent Events[] =
		{
			{ 0, 100, 0x00FE, KindIo, 1 },					// blue
			{ 0, BlockT (rTiming, y, b) - nDelta, 0x00FE, KindIo, 2 },	// red
		};
		TFrameSpan Span = { Events, 0xFFFFFFFFu, 0, 2 };
		CTestPicture Picture;
		RenderTimedFrame (*pState, Span, Machine48K, 0, Picture.Data (), TestPitch);

		// nDelta 1 or 4: from block b; nDelta 0: from block b + 1
		unsigned nFirstRed = nDelta == 0 ? b + 1 : b;
		CHECK_EQ_HEX (Picture.At (nFirstRed * 8 - 1, y), Blue);
		CHECK_EQ_HEX (Picture.At (nFirstRed * 8, y), Red);
		CHECK_EQ_HEX (Picture.At (PictureWidth - 1, y - 1), Blue);
		CHECK_EQ_HEX (Picture.At (0, y + 1), Red);
		CHECK (pState->State.Border == 2);
	}
}

// End to end, a mid-line border change lands at its block, within the
// position accuracy of the frame lock (a few T-states, so one block either
// way at most).
static void TestMidLineBorder (void)
{
	CSim Sim (Machine48K);
	const TMachineTiming &rTiming = Sim.Timing ();
	const uint32_t nFirst = 300, nLast = 340;
	const unsigned y = 150, b = 40;		// picture line, right border block

	Sim.FrameEvents (0, nLast + 4);
	for (uint32_t f = nFirst; f < nLast; f++)
	{
		Sim.Write (f, 100, Io (0x00FE, 1));				// blue
		Sim.Write (f, BlockT (rTiming, y, b) - 2, Io (0x00FE, 2));	// red, mid-block
	}

	unsigned nShown = 0, nExact = 0;
	Sim.Run (nLast + 4, [&] (const CFramePresenter &rPresenter, const CTestPicture &rPicture) {
		uint32_t f = CSim::SimFrame (rPresenter);
		if (f < nFirst || f >= nLast)
		{
			return;
		}
		nShown++;
		CHECK_EQ_HEX (rPicture.At (0, 0), Blue);
		CHECK_EQ_HEX (rPicture.At ((b - 1) * 8 - 1, y), Blue);
		CHECK_EQ_HEX (rPicture.At ((b + 1) * 8, y), Red);
		CHECK_EQ_HEX (rPicture.At (PictureWidth - 1, y - 1), Blue);
		CHECK_EQ_HEX (rPicture.At (0, y + 1), Red);
		if (rPicture.At (b * 8 - 1, y) == Blue && rPicture.At (b * 8, y) == Red)
		{
			nExact++;
		}
	});
	CHECK (nShown >= 30);
	printf ("    exact block in %u of %u frames\n", nExact, nShown);
}

// A 0x7FFD bit 3 switch at a screen line splits the picture there.
static void TestMidFrameScreenSwitch (void)
{
	CSim Sim (Machine128K);
	const TMachineTiming &rTiming = Sim.Timing ();
	const uint32_t nFirst = 300, nLast = 306;
	const unsigned nSplit = 96;		// screen line

	Sim.FrameEvents (0, nLast + 4);

	// Frame 290: bank 5 attributes white paper, bank 7 attributes red paper.
	for (unsigned i = 0; i < 768; i++)
	{
		Sim.Write (290, 1000 + i * 40, Mem ((uint16_t) (0x5800 + i), 7 << 3));
	}
	Sim.Write (290, 40000, Io (0x7FFD, 0x07));
	for (unsigned i = 0; i < 768; i++)
	{
		Sim.Write (290, 40100 + i * 30, Mem ((uint16_t) (0xD800 + i), 2 << 3));
	}
	Sim.Write (290, 70000, Io (0x7FFD, 0x00));

	for (uint32_t f = nFirst; f < nLast; f++)
	{
		Sim.Write (f, 100, Io (0x7FFD, 0x00));
		Sim.Write (f, BlockT (rTiming, BorderTop + nSplit, 0), Io (0x7FFD, 0x08));
	}

	unsigned nShown = 0;
	Sim.Run (nLast + 4, [&] (const CFramePresenter &rPresenter, const CTestPicture &rPicture) {
		uint32_t f = CSim::SimFrame (rPresenter);
		if (f < nFirst || f >= nLast)
		{
			return;
		}
		nShown++;
		CHECK (rPresenter.LastRecord ().Machine == MachinePlus3);
		for (unsigned y : { 0u, 50u, nSplit - 1 })
		{
			CHECK_EQ_HEX (ScreenPixel (rPicture, 0, y), White);
			CHECK_EQ_HEX (ScreenPixel (rPicture, 255, y), White);
		}
		for (unsigned y : { nSplit, 150u, 191u })
		{
			CHECK_EQ_HEX (ScreenPixel (rPicture, 0, y), Red);
			CHECK_EQ_HEX (ScreenPixel (rPicture, 255, y), Red);
		}
	});
	CHECK (nShown >= 4);
}

// Alternate pictures on alternate frames: every presented frame is one or
// the other, whole, even across the skips that the faster Spectrum frame
// rate forces.
static void TestAlternateFramesWhole (void)
{
	CSim Sim (Machine48K);
	const TMachineTiming &rTiming = Sim.Timing ();
	const uint32_t nFirst = 100, nLast = 1500;

	Sim.FrameEvents (0, nLast + 4);
	for (uint32_t f = nFirst; f < nLast; f++)
	{
		// In the top lines that are not shown: border and one attribute.
		bool bOdd = (f & 1) != 0;
		Sim.Write (f, 200, Io (0x00FE, bOdd ? 2 : 1));
		Sim.Write (f, 300, Mem (AttrAddr (100, 16), (uint8_t) ((bOdd ? 2 : 1) << 3)));
		// And one in the middle of the screen, after the cell is shown,
		// that must never show.
		Sim.Write (f, BlockT (rTiming, BorderTop + 100, ScreenFirstBlock + 16) + 8,
			   Mem (AttrAddr (100, 16), 4 << 3));
	}

	unsigned nShown = 0;
	uint32_t nPrevFrame = 0;
	unsigned nGaps = 0;
	Sim.Run (nLast, [&] (const CFramePresenter &rPresenter, const CTestPicture &rPicture) {
		uint32_t f = CSim::SimFrame (rPresenter);
		if (f < nFirst + 2 || f >= nLast)
		{
			nPrevFrame = f;
			return;
		}
		nShown++;
		if (f != nPrevFrame + 1)
		{
			nGaps++;
		}
		nPrevFrame = f;

		uint32_t nExpect = (f & 1) ? Red : Blue;
		CHECK_EQ_HEX (rPicture.At (0, 0), nExpect);
		CHECK_EQ_HEX (rPicture.At (PictureWidth - 1, PictureHeight - 1), nExpect);
		CHECK_EQ_HEX (rPicture.At (0, 150), nExpect);
		CHECK_EQ_HEX (ScreenPixel (rPicture, 16 * 8, 100), nExpect);
	});

	// 1400 frames at 50.08 Hz shown at 50.00 Hz: about two skips.
	CHECK (nShown > 1300);
	CHECK (nGaps >= 1);
	CHECK (Sim.Presenter ().Skipped () >= 1);
	printf ("    shown %u, skipped %u, repeated %u\n", nShown,
		Sim.Presenter ().Skipped (), Sim.Presenter ().Repeated ());
}

// No F events for a long time: the lock keeps the last period and phase.
static void TestFlywheel (void)
{
	CSim Sim (Machine48K);
	const TMachineTiming &rTiming = Sim.Timing ();
	const uint32_t nFirst = 1300, nLast = 1306;
	const unsigned nRow = 12, nCol = 20;

	Sim.FrameEvents (0, 300);			// then interrupts off
	Sim.Markers (300, nLast + 4);
	for (uint32_t f = nFirst; f < nLast; f++)
	{
		for (unsigned k = 0; k < 8; k++)
		{
			unsigned y = nRow * 8 + k;
			uint32_t t = BlockT (rTiming, BorderTop + y, ScreenFirstBlock + nCol) - 40;
			Sim.Write (f, t, Mem (AttrAddr (y, nCol), (uint8_t) ((k << 3) | 7)));
		}
	}

	unsigned nShown = 0;
	Sim.Run (nLast + 4, [&] (const CFramePresenter &rPresenter, const CTestPicture &rPicture) {
		uint32_t f = CSim::SimFrame (rPresenter);
		if (f < nFirst || f >= nLast)
		{
			return;
		}
		nShown++;
		for (unsigned k = 0; k < 8; k++)
		{
			CheckCell (rPicture, nCol, nRow * 8 + k, 0x00, White, SpectrumColour (k));
		}
	});
	CHECK (nShown >= 4);
	CHECK (Sim.Timeline ().Lock ().Locked ());
	CHECK (Sim.Timeline ().Lock ().Acquisitions () == 1);
}

int main (void)
{
	Run ("decoder: fields of event and empty pair", TestDecoderFields);
	Run ("decoder: H-bit mismatch", TestDecoderMismatch);
	Run ("decoder: malformed events", TestDecoderMalformed);
	Run ("intake: OVF and F counted, event still applied", TestIntakeOverflow);
	Run ("intake: H mismatch stops the run", TestIntakeMismatch);
	Run ("state: 48K memory map, no paging", TestMemoryMap48K);
	Run ("state: 128K/+2 paging decode and lock", TestPagingDecode128K);
	Run ("state: +2A/+3 paging decode, special paging and lock", TestPagingDecodePlus3);
	Run ("renderer: attribute and pixel pattern", TestAttributePixelPattern);
	Run ("renderer: shadow screen switch", TestShadowScreen);
	Run ("renderer: border change", TestBorderChange);
	Run ("renderer: FLASH phases", TestFlashPhases);
	Run ("renderer: ULAplus colour conversion", TestUlaPlusColour);
	Run ("renderer: ULAplus palette, mode and decode", TestUlaPlusPaletteAndMode);
	Run ("reset: shadow screen back to bank 5, paging cleared", TestResetShadowScreen);
	Run ("reset: ULAplus off, palette kept, register latch cleared", TestResetUlaPlus);
	Run ("reset: border kept, OVF and counter", TestResetBorderAndOverflow);
	Run ("event log: full ring drops, producer never waits", TestEventLogFullDrops);
	Run ("frame lock: period and machine for 48K and 128K class", TestFrameLockMachines);
	Run ("frame lock: position accuracy", TestPositionAccuracy);
	Run ("timed: multicolour attribute per line", TestMulticolour);
	Run ("renderer: border change at its 8-pixel block", TestBorderBlockExact);
	Run ("timed: mid-line border change", TestMidLineBorder);
	Run ("timed: mid-frame 0x7FFD screen switch", TestMidFrameScreenSwitch);
	Run ("timed: alternate frames presented whole", TestAlternateFramesWhole);
	Run ("timed: flywheel with no F events", TestFlywheel);

	printf ("%u tests, %u failed, %u checks, %u failed checks\n",
		s_nTests, s_nTestsFailed, s_nChecks, s_nFailures);

	return s_nTestsFailed == 0 ? 0 : 1;
}
