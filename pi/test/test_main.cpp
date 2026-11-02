//
// test_main.cpp
//
// Host tests for the event decoder, the intake loop, the state model and the
// renderer. Each test feeds a synthetic event stream through the same code
// the Pi kernel runs and checks the result, down to individual pixels.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "../src/event.h"
#include "../src/intake.h"
#include "../src/renderer.h"
#include "../src/spectrumstate.h"
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

// A frame buffer the size of the Pi's, with a wider pitch to check that the
// renderer honours it.
static const unsigned TestPitch = ScreenWidth + 16;

struct CTestFrame
{
	std::vector<uint32_t> Pixels;

	CTestFrame (void) : Pixels (TestPitch * ScreenHeight, 0x12345678u) {}

	uint32_t At (unsigned x, unsigned y) const { return Pixels[y * TestPitch + x]; }
	uint32_t *Data (void) { return Pixels.data (); }
};

// Feeds a stream into a fresh state through the intake loop.
static void Feed (CSpectrumState &rState, TIntakeCounters &rCounters,
		  const std::vector<uint32_t> &rWords)
{
	bool bMismatch = false;
	size_t n = ProcessWords (rWords.data (), rWords.size (), rState, rCounters, bMismatch);
	CHECK (!bMismatch);
	CHECK (n == rWords.size ());
}

static void Render (const CSpectrumState &rState, uint32_t nFrame, CTestFrame &rFrame)
{
	RenderFrame (rState, FlashSwapped (nFrame), rFrame.Data (), TestPitch);
}

// Pixel (px, py) of the 256 x 192 picture, in frame coordinates.
static uint32_t PicturePixel (const CTestFrame &rFrame, unsigned px, unsigned py)
{
	return rFrame.At (BorderLeft + px, BorderTop + py);
}

static void CheckCell (const CTestFrame &rFrame, unsigned nCol, unsigned y, uint8_t nBits,
		       uint32_t nInk, uint32_t nPaper)
{
	for (unsigned i = 0; i < 8; i++)
	{
		uint32_t nExpect = (nBits & (0x80 >> i)) ? nInk : nPaper;
		CHECK_EQ_HEX (PicturePixel (rFrame, nCol * 8 + i, y), nExpect);
	}
}

static void CheckBorder (const CTestFrame &rFrame, uint32_t nColour)
{
	// corners, the middle of each band, and both side strips on a picture row
	CHECK_EQ_HEX (rFrame.At (0, 0), nColour);
	CHECK_EQ_HEX (rFrame.At (ScreenWidth - 1, 0), nColour);
	CHECK_EQ_HEX (rFrame.At (0, ScreenHeight - 1), nColour);
	CHECK_EQ_HEX (rFrame.At (ScreenWidth - 1, ScreenHeight - 1), nColour);
	CHECK_EQ_HEX (rFrame.At (160, BorderTop - 1), nColour);
	CHECK_EQ_HEX (rFrame.At (160, BorderTop + PictureHeight), nColour);
	CHECK_EQ_HEX (rFrame.At (BorderLeft - 1, 100), nColour);
	CHECK_EQ_HEX (rFrame.At (BorderLeft + PictureWidth, 100), nColour);
}

// --- decoder -------------------------------------------------------------

static void TestDecoderFields (void)
{
	TEvent Event;

	CHECK (DecodeWord (EmptyPairWord, Event) == WordEmpty);

	uint32_t nWord = EncodeEvent (EventBank7, 0xA5, 0x1AFF);
	CHECK_EQ_HEX (nWord & 0xFFFF, 0x4800 | 0xA5);	// V, TYPE 2, DATA
	CHECK_EQ_HEX (nWord >> 16, 0x8000 | 0x1AFF);	// H, OFFSET
	CHECK (DecodeWord (nWord, Event) == WordEvent);
	CHECK (Event.Type == EventBank7);
	CHECK (Event.Data == 0xA5);
	CHECK (Event.Offset == 0x1AFF);
	CHECK (!Event.Overflow);

	CHECK (DecodeWord (EncodeEvent (EventPortFE, 0x17, 0, true), Event) == WordEvent);
	CHECK (Event.Type == EventPortFE);
	CHECK (Event.Data == 0x17);
	CHECK (Event.Overflow);

	// Reset: TYPE 0, DATA 0, OFFSET 0, with or without OVF.
	CHECK_EQ_HEX (EncodeEvent (EventReset, 0, 0), 0x80004000u);
	CHECK (DecodeWord (EncodeEvent (EventReset, 0, 0), Event) == WordEvent);
	CHECK (Event.Type == EventReset);
	CHECK (!Event.Overflow);
	CHECK (DecodeWord (EncodeEvent (EventReset, 0, 0, true), Event) == WordEvent);
	CHECK (Event.Type == EventReset);
	CHECK (Event.Overflow);
}

static void TestDecoderMismatch (void)
{
	TEvent Event;

	// half-word 0 with H = 1
	CHECK (DecodeWord (EncodeEvent (EventPortFE, 1, 0) | 0x8000u, Event) == WordMismatch);
	// half-word 1 with H = 0
	CHECK (DecodeWord (EncodeEvent (EventPortFE, 1, 0) & ~0x80000000u, Event) == WordMismatch);
	// halves swapped: what a one-read slip looks like
	uint32_t nGood = EncodeEvent (EventBank5, 0x55, 0x0123);
	uint32_t nSwapped = (nGood >> 16) | (nGood << 16);
	CHECK (DecodeWord (nSwapped, Event) == WordMismatch);
	// an empty pair also carries H bits
	CHECK (DecodeWord (0x00000000u, Event) == WordMismatch);
	CHECK (DecodeWord (0xFFFFFFFFu, Event) == WordMismatch);
}

static void TestDecoderMalformed (void)
{
	TEvent Event;

	CHECK (DecodeWord (EncodeEvent (EventReset, 0x01, 0), Event) == WordMalformed);	// DATA not 0
	CHECK (DecodeWord (EncodeEvent (EventReset, 0, 0x0001), Event) == WordMalformed);	// OFFSET not 0
	CHECK (DecodeWord (EncodeEvent (EventBank5, 0, 0x1B00), Event) == WordMalformed);
	CHECK (DecodeWord (EncodeEvent (EventPortFE, 0, 0x0001), Event) == WordMalformed);
	CHECK (DecodeWord (EncodeEvent (EventPortFE, 0, 0) | 0x0100u, Event) == WordMalformed);
	CHECK (DecodeWord (EncodeEvent (EventPortFE, 0, 0) | 0x20000000u, Event) == WordMalformed);
	// V = 0 is only valid as the exact empty pair 0x0000, 0x8000
	CHECK (DecodeWord (EmptyPairWord | 0x0001u, Event) == WordMalformed);
	CHECK (DecodeWord (EmptyPairWord | 0x2000u, Event) == WordMalformed);	// OVF on an empty pair
	CHECK (DecodeWord (EmptyPairWord | 0x00010000u, Event) == WordMalformed);
}

// --- intake --------------------------------------------------------------

static void TestIntakeOverflow (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	Feed (State, Counters, {
		EmptyPairWord,
		EncodeEvent (EventBank5, 0x11, 0x0000),
		EncodeEvent (EventBank5, 0x22, 0x0001, true),
		EmptyPairWord,
		EmptyPairWord,
		EncodeEvent (EventPortFE, 0x05, 0, true),
		EncodeEvent (EventReset, 0x80, 0),			// malformed: DATA not 0
	});

	CHECK (Counters.Events == 3);
	CHECK (Counters.EmptyPairs == 3);
	CHECK (Counters.Overflows == 2);
	CHECK (Counters.Malformed == 1);
	CHECK (Counters.Mismatches == 0);
	// an event with OVF set is still applied
	CHECK (State.Bank5[0] == 0x11);
	CHECK (State.Bank5[1] == 0x22);
	CHECK (State.Border == 5);
}

static void TestIntakeMismatch (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	std::vector<uint32_t> Words = {
		EncodeEvent (EventBank5, 0xAA, 0x0010),
		EmptyPairWord,
		EncodeEvent (EventBank5, 0xBB, 0x0011) | 0x8000u,	// bad H in half-word 0
		EncodeEvent (EventBank5, 0xCC, 0x0012),			// must not be applied
		EncodeEvent (EventPortFE, 0x03, 0),			// must not be applied
	};

	bool bMismatch = false;
	size_t n = ProcessWords (Words.data (), Words.size (), State, Counters, bMismatch);

	CHECK (bMismatch);
	CHECK (n == 3);
	CHECK (Counters.Mismatches == 1);
	CHECK (Counters.Events == 1);
	CHECK (Counters.EmptyPairs == 1);
	CHECK (State.Bank5[0x10] == 0xAA);
	CHECK (State.Bank5[0x11] == 0x00);
	CHECK (State.Bank5[0x12] == 0x00);
	CHECK (State.Border == 0);

	// After a resync the caller resumes with fresh words.
	std::vector<uint32_t> After = { EncodeEvent (EventBank5, 0xDD, 0x0013) };
	n = ProcessWords (After.data (), After.size (), State, Counters, bMismatch);
	CHECK (!bMismatch);
	CHECK (n == 1);
	CHECK (State.Bank5[0x13] == 0xDD);
}

// --- renderer ------------------------------------------------------------

static void TestAttributePixelPattern (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	// Row 0, column 0: pixels 0xB1, attribute BRIGHT, paper black, ink white.
	// Row 9 (character row 1, pixel line 1), column 5: pixels 0x38,
	// attribute paper blue (1), ink yellow (6), not bright.
	// Row 191 (last), column 31: pixels 0x83, attribute paper green, ink red.
	Feed (State, Counters, {
		EncodeEvent (EventBank5, 0xB1, (uint16_t) PixelRowOffset (0)),
		EncodeEvent (EventBank5, 0x47, (uint16_t) AttributeRowOffset (0)),
		EncodeEvent (EventBank5, 0x38, (uint16_t) (PixelRowOffset (9) + 5)),
		EncodeEvent (EventBank5, (1 << 3) | 6, (uint16_t) (AttributeRowOffset (9) + 5)),
		EncodeEvent (EventBank5, 0x83, (uint16_t) (PixelRowOffset (191) + 31)),
		EncodeEvent (EventBank5, (4 << 3) | 2, (uint16_t) (AttributeRowOffset (191) + 31)),
	});

	// Screen layout spot checks
	CHECK (PixelRowOffset (1) == 0x0100);
	CHECK (PixelRowOffset (8) == 0x0020);
	CHECK (PixelRowOffset (64) == 0x0800);
	CHECK (PixelRowOffset (191) == 0x17E0);
	CHECK (AttributeRowOffset (191) == 0x1AE0);

	CTestFrame Frame;
	Render (State, 0, Frame);

	CheckCell (Frame, 0, 0, 0xB1, MakeRGB (0xFF, 0xFF, 0xFF), MakeRGB (0, 0, 0));
	CheckCell (Frame, 5, 9, 0x38, MakeRGB (0xA0, 0xA0, 0x00), MakeRGB (0x00, 0x00, 0xA0));
	CheckCell (Frame, 31, 191, 0x83, MakeRGB (0xA0, 0x00, 0x00), MakeRGB (0x00, 0xA0, 0x00));
	// the attribute covers the whole 8 x 8 cell: row 7 of cell (0, 0) has no
	// pixels set, so it shows bright black paper
	CheckCell (Frame, 0, 7, 0x00, MakeRGB (0xFF, 0xFF, 0xFF), MakeRGB (0, 0, 0));
	// an untouched cell is black paper on black
	CheckCell (Frame, 10, 100, 0x00, MakeRGB (0, 0, 0), MakeRGB (0, 0, 0));
	// the pitch padding is not written
	CHECK_EQ_HEX (Frame.Pixels[ScreenWidth], 0x12345678u);
	CheckBorder (Frame, MakeRGB (0, 0, 0));
}

static void TestPortFEAnd1FFD (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	// One write that matches both decodes arrives as TYPE 3 then TYPE 7.
	Feed (State, Counters, {
		EncodeEvent (EventPortFE, 0x04, 0),
		EncodeEvent (EventPort1FFD, 0x04, 0),
	});
	CHECK (Counters.Events == 2);
	CHECK (State.Border == 4);
	CHECK (State.Port1FFD == 0x04);
	CHECK (State.Port7FFD == 0x00);

	CTestFrame Frame;
	Render (State, 0, Frame);
	CheckBorder (Frame, MakeRGB (0, 0xA0, 0));
}

static void TestShadowScreen (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	unsigned nPix = PixelRowOffset (50) + 3;
	unsigned nAttr = AttributeRowOffset (50) + 3;

	Feed (State, Counters, {
		EncodeEvent (EventBank5, 0xF0, (uint16_t) nPix),
		EncodeEvent (EventBank5, 0x07, (uint16_t) nAttr),	// white ink, black paper
		EncodeEvent (EventBank7, 0x0F, (uint16_t) nPix),
		EncodeEvent (EventBank7, 0x02 | (1 << 3), (uint16_t) nAttr),	// red on blue
	});

	CTestFrame Frame;
	Render (State, 0, Frame);
	CheckCell (Frame, 3, 50, 0xF0, MakeRGB (0xA0, 0xA0, 0xA0), MakeRGB (0, 0, 0));

	// Port 0x7FFD bit 3 selects bank 7; other bits do not matter.
	Feed (State, Counters, { EncodeEvent (EventPort7FFD, 0x08 | 0x07 | 0x10, 0) });
	Render (State, 0, Frame);
	CheckCell (Frame, 3, 50, 0x0F, MakeRGB (0xA0, 0, 0), MakeRGB (0, 0, 0xA0));

	// Writes to the hidden bank still land while the other is shown.
	Feed (State, Counters, { EncodeEvent (EventBank5, 0xFF, (uint16_t) nPix) });
	Render (State, 0, Frame);
	CheckCell (Frame, 3, 50, 0x0F, MakeRGB (0xA0, 0, 0), MakeRGB (0, 0, 0xA0));

	Feed (State, Counters, { EncodeEvent (EventPort7FFD, 0x00, 0) });
	Render (State, 0, Frame);
	CheckCell (Frame, 3, 50, 0xFF, MakeRGB (0xA0, 0xA0, 0xA0), MakeRGB (0, 0, 0));

	// Port 0x1FFD is stored but does not change the picture.
	Feed (State, Counters, { EncodeEvent (EventPort1FFD, 0x07, 0) });
	CHECK (State.Port1FFD == 0x07);
	Render (State, 0, Frame);
	CheckCell (Frame, 3, 50, 0xFF, MakeRGB (0xA0, 0xA0, 0xA0), MakeRGB (0, 0, 0));
}

static void TestBorderChange (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};
	CTestFrame Frame;

	// Bits 3 and 4 (MIC, EAR) are not border bits.
	Feed (State, Counters, { EncodeEvent (EventPortFE, 0x18 | 0x02, 0) });
	Render (State, 0, Frame);
	CheckBorder (Frame, MakeRGB (0xA0, 0, 0));

	Feed (State, Counters, { EncodeEvent (EventPortFE, 0x05, 0) });
	Render (State, 0, Frame);
	CheckBorder (Frame, MakeRGB (0, 0xA0, 0xA0));
	// the picture area is not border
	CHECK_EQ_HEX (Frame.At (BorderLeft, BorderTop), MakeRGB (0, 0, 0));
}

static void TestFlashPhases (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	// FLASH set, ink blue, paper yellow, pixels 0xF0. A second cell without
	// FLASH must never swap.
	Feed (State, Counters, {
		EncodeEvent (EventBank5, 0xF0, (uint16_t) PixelRowOffset (16)),
		EncodeEvent (EventBank5, 0x80 | (6 << 3) | 1, (uint16_t) AttributeRowOffset (16)),
		EncodeEvent (EventBank5, 0xF0, (uint16_t) (PixelRowOffset (16) + 1)),
		EncodeEvent (EventBank5, (6 << 3) | 1, (uint16_t) (AttributeRowOffset (16) + 1)),
	});

	uint32_t nBlue = MakeRGB (0, 0, 0xA0);
	uint32_t nYellow = MakeRGB (0xA0, 0xA0, 0);
	CTestFrame Frame;

	const uint32_t Frames[] = { 0, 15, 16, 31, 32, 47, 48 };
	for (uint32_t nFrame : Frames)
	{
		bool bSwapped = ((nFrame / 16) & 1) != 0;
		CHECK (FlashSwapped (nFrame) == bSwapped);
		Render (State, nFrame, Frame);
		if (bSwapped)
		{
			CheckCell (Frame, 0, 16, 0xF0, nYellow, nBlue);
		}
		else
		{
			CheckCell (Frame, 0, 16, 0xF0, nBlue, nYellow);
		}
		CheckCell (Frame, 1, 16, 0xF0, nBlue, nYellow);
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
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	// Cell (2, row 40): FLASH and BRIGHT set (group 3), ink 5, paper 2.
	unsigned nPix = PixelRowOffset (40) + 2;
	unsigned nAttr = AttributeRowOffset (40) + 2;
	uint8_t nAttrByte = 0x80 | 0x40 | (2 << 3) | 5;

	std::vector<uint32_t> Words = {
		EncodeEvent (EventBank5, 0xCC, (uint16_t) nPix),
		EncodeEvent (EventBank5, nAttrByte, (uint16_t) nAttr),
		EncodeEvent (EventPortFE, 0x04, 0),			// border 4
	};
	// Palette upload: entry i = i * 4 + 3, through register then data port.
	for (unsigned i = 0; i < 64; i++)
	{
		Words.push_back (EncodeEvent (EventUlaReg, (uint8_t) i, 0));
		Words.push_back (EncodeEvent (EventUlaData, (uint8_t) (i * 4 + 3), 0));
	}
	Feed (State, Counters, Words);
	CHECK (State.UlaPlusPalette[63] == (uint8_t) (63 * 4 + 3));
	CHECK (!State.UlaPlusOn ());

	// Palette loaded but mode off: normal colours, FLASH swaps in phase 1.
	CTestFrame Frame;
	Render (State, 16, Frame);
	CheckCell (Frame, 2, 40, 0xCC, MakeRGB (0xFF, 0, 0), MakeRGB (0, 0xFF, 0xFF));
	CheckBorder (Frame, MakeRGB (0, 0xA0, 0));

	// Mode group (register 0x40), data bit 0 = 1: ULAplus on.
	Feed (State, Counters, {
		EncodeEvent (EventUlaReg, 0x40, 0),
		EncodeEvent (EventUlaData, 0x01, 0),
	});
	CHECK (State.UlaPlusOn ());

	uint32_t nInk = UlaPlusColour (State.UlaPlusPalette[48 + 5]);
	uint32_t nPaper = UlaPlusColour (State.UlaPlusPalette[48 + 8 + 2]);
	uint32_t nBorder = UlaPlusColour (State.UlaPlusPalette[8 + 4]);
	for (uint32_t nFrame : { 0u, 16u })	// FLASH does not swap in ULAplus mode
	{
		Render (State, nFrame, Frame);
		CheckCell (Frame, 2, 40, 0xCC, nInk, nPaper);
		CheckBorder (Frame, nBorder);
	}
	// an untouched cell (attribute 0) uses group 0 entries 0 and 8
	CheckCell (Frame, 0, 0, 0x00, UlaPlusColour (State.UlaPlusPalette[0]),
		   UlaPlusColour (State.UlaPlusPalette[8]));

	// A palette write while on changes the picture at once.
	Feed (State, Counters, {
		EncodeEvent (EventUlaReg, 48 + 5, 0),
		EncodeEvent (EventUlaData, 0x1C, 0),			// pure red
	});
	Render (State, 0, Frame);
	CheckCell (Frame, 2, 40, 0xCC, MakeRGB (255, 0, 0), nPaper);

	// Groups 2 and 3 are ignored.
	Feed (State, Counters, {
		EncodeEvent (EventUlaReg, 0x80, 0),
		EncodeEvent (EventUlaData, 0x00, 0),
		EncodeEvent (EventUlaReg, 0xC0, 0),
		EncodeEvent (EventUlaData, 0x00, 0),
	});
	CHECK (State.UlaPlusOn ());
	CHECK (State.UlaPlusPalette[0] == 3);

	// Mode data 0: ULAplus off, back to normal colours.
	Feed (State, Counters, {
		EncodeEvent (EventUlaReg, 0x40, 0),
		EncodeEvent (EventUlaData, 0x00, 0),
	});
	CHECK (!State.UlaPlusOn ());
	Render (State, 0, Frame);
	CheckCell (Frame, 2, 40, 0xCC, MakeRGB (0, 0xFF, 0xFF), MakeRGB (0xFF, 0, 0));
	CheckBorder (Frame, MakeRGB (0, 0xA0, 0));
}

// --- reset ---------------------------------------------------------------

static void TestResetShadowScreen (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	unsigned nPix = PixelRowOffset (60) + 4;
	unsigned nAttr = AttributeRowOffset (60) + 4;

	Feed (State, Counters, {
		EncodeEvent (EventBank5, 0xF0, (uint16_t) nPix),
		EncodeEvent (EventBank5, 0x07, (uint16_t) nAttr),	// white on black
		EncodeEvent (EventBank7, 0x0F, (uint16_t) nPix),
		EncodeEvent (EventBank7, 0x02 | (1 << 3), (uint16_t) nAttr),	// red on blue
		EncodeEvent (EventPort1FFD, 0x05, 0),
		EncodeEvent (EventPort7FFD, 0x08 | 0x07, 0),		// show bank 7
	});

	CTestFrame Frame;
	Render (State, 0, Frame);
	CheckCell (Frame, 4, 60, 0x0F, MakeRGB (0xA0, 0, 0), MakeRGB (0, 0, 0xA0));

	Feed (State, Counters, { EncodeEvent (EventReset, 0, 0) });
	CHECK (Counters.Resets == 1);
	CHECK (Counters.Events == 7);
	CHECK (State.Port7FFD == 0);
	CHECK (State.Port1FFD == 0);
	Render (State, 0, Frame);
	CheckCell (Frame, 4, 60, 0xF0, MakeRGB (0xA0, 0xA0, 0xA0), MakeRGB (0, 0, 0));
	// the screen banks keep their contents
	CHECK (State.Bank7[nPix] == 0x0F);
}

static void TestResetUlaPlus (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	unsigned nPix = PixelRowOffset (20) + 7;
	unsigned nAttr = AttributeRowOffset (20) + 7;

	std::vector<uint32_t> Words = {
		EncodeEvent (EventBank5, 0xAA, (uint16_t) nPix),
		EncodeEvent (EventBank5, (1 << 3) | 6, (uint16_t) nAttr),	// yellow on blue
		EncodeEvent (EventPortFE, 0x02, 0),				// border red
	};
	for (unsigned i = 0; i < 64; i++)
	{
		Words.push_back (EncodeEvent (EventUlaReg, (uint8_t) i, 0));
		Words.push_back (EncodeEvent (EventUlaData, (uint8_t) (255 - i), 0));
	}
	Words.push_back (EncodeEvent (EventUlaReg, 0x40, 0));
	Words.push_back (EncodeEvent (EventUlaData, 0x01, 0));		// ULAplus on
	Words.push_back (EncodeEvent (EventUlaReg, 0x17, 0));		// register latch not 0
	Feed (State, Counters, Words);
	CHECK (State.UlaPlusOn ());
	CHECK (State.UlaPlusRegister == 0x17);

	Feed (State, Counters, { EncodeEvent (EventReset, 0, 0) });
	CHECK (Counters.Resets == 1);
	CHECK (!State.UlaPlusOn ());
	CHECK (State.UlaPlusMode == 0);
	CHECK (State.UlaPlusRegister == 0);
	CHECK (State.Border == 2);
	for (unsigned i = 0; i < 64; i++)
	{
		CHECK (State.UlaPlusPalette[i] == (uint8_t) (255 - i));
	}

	// normal colours, border unchanged
	CTestFrame Frame;
	Render (State, 0, Frame);
	CheckCell (Frame, 7, 20, 0xAA, MakeRGB (0xA0, 0xA0, 0), MakeRGB (0, 0, 0xA0));
	CheckBorder (Frame, MakeRGB (0xA0, 0, 0));

	// The register latch is 0 after the reset, so a data-port write with no
	// register write before it sets palette entry 0.
	Feed (State, Counters, { EncodeEvent (EventUlaData, 0x1C, 0) });
	CHECK (State.UlaPlusPalette[0] == 0x1C);
	CHECK (!State.UlaPlusOn ());

	// The kept palette is used again when software turns ULAplus back on.
	Feed (State, Counters, {
		EncodeEvent (EventUlaReg, 0x40, 0),
		EncodeEvent (EventUlaData, 0x01, 0),
	});
	Render (State, 0, Frame);
	CheckBorder (Frame, UlaPlusColour (State.UlaPlusPalette[8 + 2]));
	CHECK (State.UlaPlusPalette[8 + 2] == (uint8_t) (255 - 10));
}

static void TestResetBorderAndOverflow (void)
{
	CSpectrumState State;
	State.Reset ();
	TIntakeCounters Counters = {};

	Feed (State, Counters, {
		EncodeEvent (EventPortFE, 0x05, 0),
		EncodeEvent (EventReset, 0, 0, true),	// a reset event can carry OVF
		EmptyPairWord,
		EncodeEvent (EventReset, 0, 0),
	});
	CHECK (Counters.Resets == 2);
	CHECK (Counters.Overflows == 1);
	CHECK (Counters.Events == 3);
	CHECK (Counters.Malformed == 0);
	CHECK (State.Border == 5);

	CTestFrame Frame;
	Render (State, 0, Frame);
	CheckBorder (Frame, MakeRGB (0, 0xA0, 0xA0));
}

int main (void)
{
	Run ("decoder: fields of event and empty pair", TestDecoderFields);
	Run ("decoder: H-bit mismatch", TestDecoderMismatch);
	Run ("decoder: malformed events", TestDecoderMalformed);
	Run ("intake: OVF counted, event still applied", TestIntakeOverflow);
	Run ("intake: H mismatch stops the run", TestIntakeMismatch);
	Run ("renderer: attribute and pixel pattern", TestAttributePixelPattern);
	Run ("intake: port 0xFE and 0x1FFD from one write", TestPortFEAnd1FFD);
	Run ("renderer: shadow screen switch", TestShadowScreen);
	Run ("renderer: border change", TestBorderChange);
	Run ("renderer: FLASH phases", TestFlashPhases);
	Run ("renderer: ULAplus colour conversion", TestUlaPlusColour);
	Run ("renderer: ULAplus palette and mode on/off", TestUlaPlusPaletteAndMode);
	Run ("reset: shadow screen back to bank 5, paging cleared", TestResetShadowScreen);
	Run ("reset: ULAplus off, palette kept, register latch cleared", TestResetUlaPlus);
	Run ("reset: border kept, OVF and counter", TestResetBorderAndOverflow);

	printf ("%u tests, %u failed, %u checks, %u failed checks\n",
		s_nTests, s_nTestsFailed, s_nChecks, s_nFailures);

	return s_nTestsFailed == 0 ? 0 : 1;
}
