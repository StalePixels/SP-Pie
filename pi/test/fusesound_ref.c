/*
 * fusesound_ref.c
 *
 * A reference sound generator for the host tests: Fuse's sound.c,
 * unmodified, once more with its own names (fusesound_rename.h), on a
 * 128K, driven directly with sound_ay_write () the way Fuse's AY port code
 * drives it. The tests compare SP-Pie's sound output with it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define FUSESOUND_INSTANCE fusesound_ref
#include "fusesound_rename.h"
#include "sound.c"

#include <string.h>

#include "fusesound_ref.h"

static fuse_machine_info ref_machine;
fuse_machine_info *machine_current = &ref_machine;

static libspectrum_signed_word ref_output[ 2048 ];
static int ref_count;

int
sound_lowlevel_init( const char *device GCC_UNUSED, int *freqptr GCC_UNUSED,
                     int *stereoptr GCC_UNUSED )
{
  return 0;
}

void
sound_lowlevel_end( void )
{
}

void
sound_lowlevel_frame( libspectrum_signed_word *data, int len )
{
  if( len > 2048 ) len = 2048;
  memcpy( ref_output, data, len * sizeof *data );
  ref_count = len;
}

int
fusesound_ref_start( void )
{
  ref_machine.machine = LIBSPECTRUM_MACHINE_128;
  ref_machine.capabilities = libspectrum_machine_capabilities( LIBSPECTRUM_MACHINE_128 );
  ref_machine.timings.processor_speed =
    libspectrum_timings_processor_speed( LIBSPECTRUM_MACHINE_128 );
  ref_machine.timings.tstates_per_frame =
    libspectrum_timings_tstates_per_frame( LIBSPECTRUM_MACHINE_128 );
  sound_init( NULL );
  sound_ay_reset();	/* as Fuse's machine_reset () does */
  return sound_enabled ? 0 : 1;
}

void
fusesound_ref_ay_write( int reg, int val, uint32_t tstate )
{
  sound_ay_write( reg, val, tstate );
}

int
fusesound_ref_frame( const int16_t **ppSamples )
{
  sound_frame();
  *ppSamples = ref_output;
  return ref_count;
}
