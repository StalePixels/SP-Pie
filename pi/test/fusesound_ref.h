/*
 * fusesound_ref.h
 *
 * The reference Fuse sound generator of the host tests (fusesound_ref.c).
 * Start it after fusesound_init (), which sets Fuse's sound settings.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSESOUND_REF_H
#define SPPIE_FUSESOUND_REF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int fusesound_ref_start( void );
void fusesound_ref_ay_write( int reg, int val, uint32_t tstate );
int fusesound_ref_frame( const int16_t **ppSamples );

#ifdef __cplusplus
}
#endif

#endif
