//
// renderer.h
//
// Draws the Spectrum picture from the state model into a 32-bit pixel
// buffer. Platform-free, so the host tests can check the pixels.
//
// Layout: 320 x 240 pixels. The 256 x 192 Spectrum picture starts at
// (32, 24); the border fills 32 pixels left and right and 24 pixels top and
// bottom.
//
// Pixel format: 0xAARRGGBB in one uint32_t (alpha 0xFF). This is the
// ARGB8888 layout that Circle uses for a 32-bit frame buffer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_renderer_h
#define _sppie_renderer_h

#include "spectrumstate.h"
#include <stdint.h>

namespace sppie {

static const unsigned ScreenWidth	= 320;
static const unsigned ScreenHeight	= 240;
static const unsigned PictureWidth	= 256;
static const unsigned PictureHeight	= 192;
static const unsigned BorderLeft	= 32;
static const unsigned BorderTop		= 24;

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

// FLASH phase for a frame number: true when FLASH cells show ink and paper
// swapped. The phase changes every FlashPeriodFrames frames.
static inline bool FlashSwapped (uint32_t nFrame)
{
	return ((nFrame / FlashPeriodFrames) & 1) != 0;
}

// Draws one whole frame (border and picture) from rState.
//
// pPixels points at pixel (0, 0); nPitch is the distance between rows in
// pixels. Normal mode uses the 16 Spectrum colours and honours FLASH using
// bFlashSwapped. ULAplus mode (rState.UlaPlusOn ()) uses the 64-entry
// palette: attribute bits 7..6 select one of four 16-entry groups, ink is
// entry group*16 + INK and paper is group*16 + 8 + PAPER; FLASH does not
// swap; the border is entry 8 + border colour.
void RenderFrame (const CSpectrumState &rState, bool bFlashSwapped,
		  uint32_t *pPixels, unsigned nPitch);

// Offset of the pixel byte for picture row y (0..191), column byte 0, in a
// screen bank.
static inline unsigned PixelRowOffset (unsigned y)
{
	return ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
}

// Offset of the attribute byte for picture row y, column byte 0.
static inline unsigned AttributeRowOffset (unsigned y)
{
	return 0x1800 + (y >> 3) * 32;
}

}

#endif
