//
// renderer.h
//
// Draws Spectrum frames like the ULA, line by line, from the state model and
// the frame's timed events. Platform-free, so the host tests can check the
// pixels.
//
// Display: 640 x 360 pixels, 16:9, which the firmware scales by exactly 3 to
// 1920 x 1080. The Spectrum picture is 352 x 296 pixels, centred: the 256 x
// 192 screen with 48 pixels of border left and right, 48 lines above and 56
// lines below. Sources (World of Spectrum FAQ, 48K reference): "The border
// is 48 pixels wide at each side"; of the 64 lines before the screen "at
// least the last 48 of these are actual border-lines"; after the screen
// come "56 border lines". The 128K class has the same 48 + 192 + 56 lines
// (63 lines before the screen, 311 lines a frame). The areas left and right
// of the picture are black; the optional overlay uses them.
//
// Timing: the picture is drawn in 8-pixel blocks, 44 a line. Block b of
// picture line y is shown at T-state
//
//     (FirstPixelLine - 48 + y) * TStatesPerLine + FirstPixelLineT + 4 * (b - 6)
//
// (1 T-state = 2 pixels; block 6 is the first screen byte). Each block uses
// the state after every event before that T-state: a screen block takes its
// pixel byte, attribute, screen bank and colours from it, and a border block
// its border colour. The ULA changes the border only at these 8-pixel
// blocks (BorderStepTStates).
//
// Pixel format: 0xAARRGGBB in one uint32_t (alpha 0xFF). This is the
// ARGB8888 layout that Circle uses for a 32-bit frame buffer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_renderer_h
#define _sppie_renderer_h

#include "eventlog.h"
#include "machinetiming.h"
#include "spectrumstate.h"
#include <stdint.h>

namespace sppie {

static const unsigned DisplayWidth	= 640;
static const unsigned DisplayHeight	= 360;

static const unsigned BorderLeft	= 48;		// pixels
static const unsigned BorderTop		= 48;		// lines
static const unsigned BorderBottom	= 56;		// lines
static const unsigned ScreenPixelWidth	= 256;
static const unsigned ScreenPixelHeight	= 192;
static const unsigned PictureWidth	= BorderLeft + ScreenPixelWidth + BorderLeft;	// 352
static const unsigned PictureHeight	= BorderTop + ScreenPixelHeight + BorderBottom;	// 296
static const unsigned PictureLeft	= (DisplayWidth - PictureWidth) / 2;		// 144
static const unsigned PictureTop	= (DisplayHeight - PictureHeight) / 2;		// 32

static const unsigned BlockPixels	= 2 * BorderStepTStates;			// 8
static const unsigned BlocksPerLine	= PictureWidth / BlockPixels;			// 44
static const unsigned ScreenFirstBlock	= BorderLeft / BlockPixels;			// 6

static const unsigned FlashPeriodFrames	= 16;	// FLASH phase changes every 16 frames

static inline uint32_t MakeRGB (uint8_t r, uint8_t g, uint8_t b)
{
	return 0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
}

// The 16 Spectrum colours, index = BRIGHT * 8 + colour (bit 2 green,
// bit 1 red, bit 0 blue). Normal intensity 0xA0, bright 0xFF.
uint32_t SpectrumColour (unsigned nIndex);

// Converts a ULAplus GGGRRRBB palette entry to a pixel. The missing low blue
// bit is the OR of the two stored blue bits; 3-bit levels scale to 0..255.
uint32_t UlaPlusColour (uint8_t nGRB);

// FLASH phase for a Spectrum frame number: true when FLASH cells show ink
// and paper swapped.
static inline bool FlashSwapped (uint32_t nFrame)
{
	return ((nFrame / FlashPeriodFrames) & 1) != 0;
}

// T-state at which block b of picture line y is shown.
static inline uint32_t BlockTState (const TMachineTiming &rTiming, unsigned y, unsigned b)
{
	return   (rTiming.FirstPixelLine - BorderTop + y) * rTiming.TStatesPerLine
	       + rTiming.FirstPixelLineT + BorderStepTStates * b
	       - BorderStepTStates * ScreenFirstBlock;
}

// The state model plus its colours as pixels. The ULAplus colours follow
// each palette write.
struct CRenderState
{
	CSpectrumState	State;
	uint32_t	NormalColours[16];
	uint32_t	UlaPlusColours[UlaPlusPaletteSize];

	void Reset (void);		// state and colours
	void SyncColours (void);	// after the state was changed directly
	void Apply (const TTimedEvent &rEvent, TMachine Machine);
};

// Draws one Spectrum frame and applies all of its events to rState.
//
// rEvents holds the frame's events in time order. pPixels points at the
// top-left pixel of the 352 x 296 picture; nPitch is the distance between
// rows in pixels. Normal mode uses the 16 Spectrum colours and FLASH (from
// nFrame). ULAplus mode (on at the block's T-state) uses the 64-entry
// palette: attribute bits 7..6 select one of four 16-entry groups, ink is
// entry group*16 + INK and paper is group*16 + 8 + PAPER; FLASH does not
// swap; the border is entry 8 + border colour.
void RenderTimedFrame (CRenderState &rState, const TFrameSpan &rEvents, TMachine Machine,
		       uint32_t nFrame, uint32_t *pPixels, unsigned nPitch);

// Applies a frame's events without drawing.
void ApplyFrame (CRenderState &rState, const TFrameSpan &rEvents, TMachine Machine);

// Draws the state as it is, with no events.
void RenderStill (CRenderState &rState, TMachine Machine, uint32_t nFrame,
		  uint32_t *pPixels, unsigned nPitch);

// Offset of the pixel byte for screen row y (0..191), column byte 0, in a
// screen bank.
static inline unsigned PixelRowOffset (unsigned y)
{
	return ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
}

// Offset of the attribute byte for screen row y, column byte 0.
static inline unsigned AttributeRowOffset (unsigned y)
{
	return 0x1800 + (y >> 3) * 32;
}

}

#endif
