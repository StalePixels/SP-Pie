/*
 * fusesound.h
 *
 * The C interface to SP-Pie's sound: the AY chips, TurboSound, the
 * beeper and the Covox DACs, made from the Spectrum's port writes with
 * Fuse's sound generator (public/fuse/sound.c, pinned and unmodified).
 *
 * Four copies of Fuse's generator run side by side (fusesound_rename.h):
 * one for AY chip 0, one for AY chip 1 (TurboSound), one for the ULA
 * output (beeper and MIC) on the 48K, with Fuse's model of the 48K's
 * internal speaker, and one for the ULA output on the 128K class, with
 * Fuse's model of the TV speaker, which also carries the Covox DACs. Each
 * makes 48 kHz mono samples for each Spectrum frame. This layer decodes
 * the ports, sends every write to its generator at its T-state, and adds
 * the outputs.
 *
 * Time. The generators count a clock of 3,546,900 Hz, the 128K's Z80
 * clock, twice the AY clock of 1,773,450 Hz. On the 128K class that is
 * the T-state count. A 48K frame (69,888 T-states at 3.5 MHz) is scaled
 * to the same clock, so its AY runs at 1,773,450 Hz too, and its frame is
 * 70,825 or 70,826 counts, alternating so that no time is lost. The
 * samples of a frame cover exactly its real length, so the generators
 * make 48,000 samples per second of Spectrum time on every model.
 *
 * Every function runs on the core that renders (core 0 on the Pi), with
 * the display calls of fuseula.h for the same frame.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSESOUND_H
#define SPPIE_FUSESOUND_H

#include <stdint.h>

#include "fuseula.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The attached devices: the parameter of control-port select 1
   (docs/control-ports.md). */
#define FUSESOUND_AY		0x01	/* AY chip 0 */
#define FUSESOUND_TURBOSOUND	0x02	/* AY chip 1; needs FUSESOUND_AY too */
#define FUSESOUND_BEEPER	0x04	/* the ULA's EAR and MIC output */
#define FUSESOUND_COVOX_FB	0x08	/* Covox DAC on port 0xFB */
#define FUSESOUND_COVOX_DD	0x10	/* Covox DAC on port 0xDD */
#define FUSESOUND_COVOX_DF	0x20	/* Covox DAC on port 0xDF */
#define FUSESOUND_DEVICES	0x3F
#define FUSESOUND_DEFAULT	( FUSESOUND_AY | FUSESOUND_TURBOSOUND | FUSESOUND_BEEPER )

#define FUSESOUND_RATE		48000	/* samples per second */
#define FUSESOUND_CLOCK		3546900	/* generator counts per second */
#define FUSESOUND_FRAME_MAX	1024	/* samples of one frame, at most */

/* The largest sample value: samples are signed 24-bit. */
#define FUSESOUND_FULL_SCALE	0x7FFFFF

/* Starts the generators, with the default devices and every AY register
   0. Returns 0 on success. A second call does nothing. */
int fusesound_init( void );

/* Starts a frame of model Machine. Call before the frame's writes. */
void fusesound_begin_frame( enum fuseula_machine Machine );

/* The attached devices from nTState on (FUSESOUND_* bits). */
void fusesound_devices( uint32_t nTState, unsigned nDevices );
unsigned fusesound_current_devices( void );

/* An I/O write at nTState, the T-state at which Fuse makes it take
   effect (fuseula_io_write). The AY ports, the TurboSound chip select and
   the Covox ports are decoded here. The beeper comes through Fuse's own
   ULA port code (fusesound_ula). */
void fusesound_io_write( uint32_t nTState, uint16_t nPort, uint8_t nData );

/* The ULA's sound output from at_tstates on, as Fuse's ULA port code
   computes it (peripherals/ula.c, ula_write): mic_on is 1 when port 0xFE
   bit 3 is 0, beeper_on is port 0xFE bit 4. Called by Fuse's sound_ula
   stand-in (fusestubs.c). */
void fusesound_ula( uint32_t at_tstates, int mic_on, int beeper_on );

/* The end of a Spectrum reset at nTState: every AY register of both chips
   becomes 0, register 0 is selected in each, and chip 0 is selected. The
   devices are set by the caller (fusesound_devices). */
void fusesound_reset( uint32_t nTState );

/* Ends the frame. *ppSamples points to the frame's mixed samples, signed
   24-bit, valid until the next call. Returns their number. */
unsigned fusesound_end_frame( const int32_t **ppSamples );

/* State, for the tests. */
uint8_t fusesound_ay_selected_register( unsigned nChip );
unsigned fusesound_ay_selected_chip( void );
uint8_t fusesound_covox_value( unsigned nIndex );	/* 0 = 0xFB, 1 = 0xDD, 2 = 0xDF */

#ifdef __cplusplus
}
#endif

#endif
