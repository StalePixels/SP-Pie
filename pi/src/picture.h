//
// picture.h
//
// The display geometry.
//
// The display is 480 x 270 pixels, 16:9. The frame buffer has two lines
// for each display line, 480 x 540, which the firmware scales by exactly
// 4 across and 2 down to 1920 x 1080: each display line is 4 HDMI lines,
// and the two frame buffer lines of it can differ (the line weave of
// interlace mixing, interlace.h). The Spectrum picture is 352 x 270 pixels in the
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

namespace sppie {

static const unsigned DisplayWidth	= 480;
static const unsigned DisplayHeight	= 270;
static const unsigned FrameBufferLines	= 2;	// frame buffer lines per display line

static const unsigned PictureWidth	= FUSEULA_PICTURE_WIDTH;			// 352
static const unsigned PictureHeight	= FUSEULA_PICTURE_HEIGHT;			// 270
static const unsigned PictureLeft	= (DisplayWidth - PictureWidth) / 2;		// 64
static const unsigned PictureTop	= (DisplayHeight - PictureHeight) / 2;		// 0

}

#endif
