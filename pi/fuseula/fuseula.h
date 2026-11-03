/*
 * fuseula.h
 *
 * The C interface between the SP-Pie C++ code and the Fuse ULA model.
 *
 * Fuse (public/fuse, pinned) emulates the ZX Spectrum. This layer builds
 * only its display model (display.c), its memory and paging code
 * (memory_pages.c and the machine files) and its peripheral port decode,
 * unmodified, and feeds them the Spectrum's own writes instead of a Z80:
 * each memory write and I/O write the CPLD sends is applied at its T-state
 * in the frame, and at the end of the frame Fuse draws what the ULA showed.
 *
 * Fuse's display covers 320 x 240 pixels: the 256 x 192 screen with 32
 * pixels of border left and right and 24 lines above and below. The
 * picture here is 352 x 270. This layer draws the border outside Fuse's
 * area with Fuse's own rule: an 8-pixel chunk shows the border colour that
 * was current 3 T-states after the chunk's first T-state.
 *
 * Fuse has no ULAplus. This layer adds it: the register port 0xBF3B and the
 * data port 0xFF3B, the 64-entry palette and the mode register. A palette
 * or mode change applies to every chunk drawn after it in the frame, by the
 * same rule as the border.
 *
 * Every function runs on the core that renders (core 0 on the Pi). Fuse
 * keeps its state in globals, so there is one instance.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSEULA_H
#define SPPIE_FUSEULA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The models SP-Pie emulates, and the Fuse machine used for each. */
enum fuseula_machine
{
	FUSEULA_48K	= 0,	/* LIBSPECTRUM_MACHINE_48: 48K and 16K */
	FUSEULA_128K	= 1,	/* LIBSPECTRUM_MACHINE_128: 128K and +2 */
	FUSEULA_PLUS3	= 2,	/* LIBSPECTRUM_MACHINE_PLUS3: +2A and +3 */
	FUSEULA_MACHINES
};

#define FUSEULA_PICTURE_WIDTH	352
#define FUSEULA_PICTURE_HEIGHT	270

/* The 256 x 192 screen inside the picture: its first pixel and line. */
#define FUSEULA_SCREEN_LEFT	48
#define FUSEULA_SCREEN_TOP	39

/* Starts Fuse with the 48K selected. Fuse draws into the 352 x 270 picture
   at pPicture, nPitch pixels between rows, in 0xAARRGGBB. Returns 0 on
   success. A second call keeps Fuse's modules and RAM, selects the 48K
   again and redraws the whole picture from the next frame. */
int fuseula_init (uint32_t *pPicture, unsigned nPitch);

/* Selects the Fuse machine for a model. RAM, the border colour and ULAplus
   keep their values; paging starts as after a reset. Does nothing when the
   model is already selected. Call between frames. */
void fuseula_select (enum fuseula_machine Machine);

enum fuseula_machine fuseula_current_machine (void);

/* T-states per frame of a model, from libspectrum's timing tables. */
uint32_t fuseula_frame_tstates (enum fuseula_machine Machine);

/* Fuse's memory contention delay for each T-state of a model's frame
   (Fuse's ula_contention table, which machine_reset fills from the model's
   contend_delay function), into pTable[0] to pTable[nSize - 1]. Selects
   each model in turn, so call it only before the first frame; the 48K is
   selected again afterwards. */
void fuseula_contention (enum fuseula_machine Machine, uint8_t *pTable, uint32_t nSize);

/* One write, at nTState (0 to frame T-states - 1) in the frame. Writes of
   a frame come in time order. nTState is the T-state at which Fuse makes
   the write take effect: the end of a memory write cycle, and the first
   T-state after the start of an I/O write cycle. */
void fuseula_memory_write (uint32_t nTState, uint16_t nAddress, uint8_t nData);
void fuseula_io_write (uint32_t nTState, uint16_t nPort, uint8_t nData);

/* The end of a Spectrum reset (/RESET went high) at nTState: Fuse's
   machine reset, which sets the paging to its start state. ULAplus is
   turned off and its register cleared. RAM, the border colour and the
   ULAplus palette keep their values. */
void fuseula_reset (uint32_t nTState);

/* Ends the frame: Fuse draws the rest of it, then this layer draws the
   border outside Fuse's area. The picture then holds the whole frame. */
void fuseula_end_frame (void);

/* The screen bank, 5 or 7 (port 0x7FFD bit 3), that Fuse showed on each of
   the FUSEULA_PICTURE_HEIGHT picture lines of the frame last ended: the
   bank of the line's first screen chunk, at that chunk's T-state. Lines
   above and below the screen take the bank at the same place in their
   line. A 48K always shows bank 5. */
const uint8_t *fuseula_line_screens (void);

/* Draws screen line nLine (0 to 191) with the pixel bytes of RAM bank nBank
   (5 or 7), 256 pixels into pOut, in 0xAARRGGBB. Each 8-pixel chunk has
   the ink and paper the picture shows for it: the colours Fuse gave the
   chunk at its beam time, from the attribute of the bank shown then, with
   FLASH and ULAplus applied. The pixel byte is the one Fuse drew when
   nBank is the bank it drew the chunk from, else the byte in the Pi's copy
   of the bank now (after the frame last ended). */
void fuseula_screen_line (unsigned nBank, unsigned nLine, uint32_t *pOut);

/* State, for the tests. */
uint8_t fuseula_ram (unsigned nBank, unsigned nOffset);
uint8_t fuseula_border (void);
unsigned fuseula_screen_bank (void);
int fuseula_ulaplus_on (void);
uint32_t fuseula_line_tstates (void);			/* T-states per line */
uint32_t fuseula_top_left_border_tstate (void);	/* Fuse's line_times[0] */

#ifdef __cplusplus
}
#endif

#endif
