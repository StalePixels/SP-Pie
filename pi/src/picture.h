//
// picture.h
//
// The display geometry.
//
// The display is 480 x 270 pixels, 16:9. The frame buffer is 960 x 540,
// also 16:9, which the firmware scales by exactly 2 to 1920 x 1080. The
// firmware keeps the frame buffer's shape, so the buffer must be 16:9.
// Each display pixel is 2 x 2 frame buffer pixels (WriteFrameBuffer ()),
// except in the double high mode of interlace mixing (interlace.h), where
// the second frame buffer line of a display line can show another
// picture. The Spectrum picture is 352 x 270 pixels in the
// middle and fills the height: the 256 x 192 screen with 48 pixels of
// border left and right and 39 lines above and below. Sources (World of
// Spectrum FAQ, 48K reference): "The border is 48 pixels wide at each
// side"; of the 64 lines before the screen "at least the last 48 of these
// are actual border-lines"; after the screen come "56 border lines". So the
// display shows all of the side border and the 39 border lines nearest the
// screen above and below. The areas left and right of the picture are
// black, or hold the border extension (borderextension.h) and the HUD.
//
// Pixel format: 0xAARRGGBB in one uint32_t (alpha 0xFF), the ARGB8888
// layout that Circle uses for a 32-bit frame buffer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_picture_h
#define _sppie_picture_h

#include "../fuseula/fuseula.h"
#include <stdint.h>

namespace sppie {

static const unsigned DisplayWidth	= 480;
static const unsigned DisplayHeight	= 270;
static const unsigned FrameBufferScale	= 2;	// frame buffer pixels per display pixel, each way
static const unsigned FrameBufferWidth	= DisplayWidth * FrameBufferScale;	// 960
static const unsigned FrameBufferHeight	= DisplayHeight * FrameBufferScale;	// 540
static_assert (FrameBufferScale == 2, "two frame buffer pixels per display pixel, each way");

static const unsigned PictureWidth	= FUSEULA_PICTURE_WIDTH;			// 352
static const unsigned PictureHeight	= FUSEULA_PICTURE_HEIGHT;			// 270
static const unsigned PictureLeft	= (DisplayWidth - PictureWidth) / 2;		// 64
static const unsigned PictureTop	= (DisplayHeight - PictureHeight) / 2;		// 0

// The 256 x 192 screen inside the picture.
static const unsigned ScreenLeft	= FUSEULA_SCREEN_LEFT;				// 48
static const unsigned ScreenTop		= FUSEULA_SCREEN_TOP;				// 39
static const unsigned ScreenWidth	= 256;
static const unsigned ScreenHeight	= 192;

// Two frame buffer pixels, the same 0xAARRGGBB pixel twice.
inline uint64_t PixelPair (uint32_t nPixel)
{
	return (uint64_t) nPixel << 32 | nPixel;
}

// The frame buffer lines of one display line: pDisplayRow, 480 pixels,
// each pixel twice into pFirst and pSecond (FrameBufferWidth pixels, as
// 64-bit words of two pixels). When pSecondRow is not 0, pSecond takes
// its picture area from pSecondRow (the 352-pixel picture line) and its
// side areas from pDisplayRow. TWord is uint64_t, or volatile uint64_t
// for the frame buffer, which is device memory and takes aligned 64-bit
// stores.
template <typename TWord>
inline void WriteFrameBufferLines (const uint32_t *pDisplayRow, const uint32_t *pSecondRow,
				   TWord *pFirst, TWord *pSecond)
{
	for (unsigned x = 0; x < DisplayWidth; x++)
	{
		pFirst[x] = PixelPair (pDisplayRow[x]);
	}
	if (pSecondRow == 0)
	{
		for (unsigned x = 0; x < DisplayWidth; x++)
		{
			pSecond[x] = PixelPair (pDisplayRow[x]);
		}
		return;
	}
	for (unsigned x = 0; x < DisplayWidth; x++)
	{
		bool bPicture = x >= PictureLeft && x < PictureLeft + PictureWidth;
		pSecond[x] = PixelPair (bPicture ? pSecondRow[x - PictureLeft] : pDisplayRow[x]);
	}
}

// Display rows nFirst to nEnd - 1 of the frame buffer from the 480 x 270
// display at pDisplay (DisplayWidth pixels between rows): two lines each
// at pBuffer, nPitchWords 64-bit words between lines. pSecondPicture, when
// not 0, is the 352 x 270 picture for the second frame buffer line of each
// display line (CFramePresenter::SecondPicture ()).
template <typename TWord>
inline void WriteFrameBufferRows (const uint32_t *pDisplay, const uint32_t *pSecondPicture,
				  TWord *pBuffer, unsigned nPitchWords,
				  unsigned nFirst, unsigned nEnd)
{
	for (unsigned y = nFirst; y < nEnd && y < DisplayHeight; y++)
	{
		const uint32_t *pSecondRow = 0;
		if (pSecondPicture != 0 && y >= PictureTop && y < PictureTop + PictureHeight)
		{
			pSecondRow = pSecondPicture + (y - PictureTop) * PictureWidth;
		}
		TWord *pFirst = pBuffer + (y * FrameBufferScale) * nPitchWords;
		WriteFrameBufferLines (pDisplay + y * DisplayWidth, pSecondRow,
				       pFirst, pFirst + nPitchWords);
	}
}

// The whole frame buffer: FrameBufferHeight lines at pBuffer.
template <typename TWord>
inline void WriteFrameBuffer (const uint32_t *pDisplay, const uint32_t *pSecondPicture,
			      TWord *pBuffer, unsigned nPitchWords)
{
	WriteFrameBufferRows (pDisplay, pSecondPicture, pBuffer, nPitchWords, 0, DisplayHeight);
}

}

#endif
