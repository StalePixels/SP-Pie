/*
 * fusesound.c
 *
 * Decodes the sound ports, drives the three Fuse sound generators and
 * mixes their output. See fusesound.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <config.h>

#include <string.h>

#include "libspectrum.h"

#include "compat.h"
#include "machine.h"
#include "settings.h"
#include "sound/blipbuffer.h"

#include "fusesound.h"

/* --- the generators ---------------------------------------------------- */

/* The names each compile of sound.c gets (fusesound_rename.h), and the
   functions this layer defines for each: its machine description and its
   low-level output, which keeps the frame's samples. */
#define GENERATOR( p, machine ) \
  extern int p##_sound_enabled; \
  extern Blip_Buffer *p##_left_buf; \
  void p##_sound_init( const char *device ); \
  void p##_sound_ay_write( int reg, int val, libspectrum_dword now ); \
  void p##_sound_frame( void ); \
  void p##_sound_ay_reset( void ); \
  void p##_sound_ula( libspectrum_dword at_tstates, int mic_on, int beeper_on ); \
  int p##_sound_lowlevel_init( const char *device, int *freqptr, int *stereoptr ); \
  void p##_sound_lowlevel_end( void ); \
  void p##_sound_lowlevel_frame( libspectrum_signed_word *data, int len ); \
  \
  fuse_machine_info *p##_machine_current = &machine; \
  static libspectrum_signed_word p##_output[ FUSESOUND_FRAME_MAX ]; \
  static int p##_output_count; \
  \
  int \
  p##_sound_lowlevel_init( const char *device GCC_UNUSED, int *freqptr GCC_UNUSED, \
                           int *stereoptr GCC_UNUSED ) \
  { \
    return 0; \
  } \
  \
  void \
  p##_sound_lowlevel_end( void ) \
  { \
  } \
  \
  void \
  p##_sound_lowlevel_frame( libspectrum_signed_word *data, int len ) \
  { \
    if( len > FUSESOUND_FRAME_MAX ) len = FUSESOUND_FRAME_MAX; \
    memcpy( p##_output, data, len * sizeof *data ); \
    p##_output_count = len; \
  }

/* The AY generators see a machine with an AY. The two ULA generators see
   a machine without one, so they make no AY sound: ula48 a 48K, for which
   Fuse picks the internal beeper's speaker model, and ula128 a 128K, for
   which it picks the TV speaker (sound.c, sound_get_speaker_type). The
   frame's model selects the ULA generator that gets the beeper.

   Each ULA generator keeps one speaker type for good, set before the
   first frame. Fuse clears the ULA buffer when the speaker type changes
   (sound.c, sound_ula_update), which moves its sample phase away from the
   main buffer's; from then on some frames give one ULA sample fewer, and
   that sample of the mix lacks the beeper (a one-sample drop to 0). With
   fixed types every buffer of every generator keeps the same phase and
   gives the same number of samples each frame. */
static fuse_machine_info ay_machine, ula48_machine, ula128_machine;

GENERATOR( fusesound_ay0, ay_machine )
GENERATOR( fusesound_ay1, ay_machine )
GENERATOR( fusesound_ula48, ula48_machine )
GENERATOR( fusesound_ula128, ula128_machine )

/* --- state ------------------------------------------------------------- */

#define COVOX_PORTS 3

static const libspectrum_byte covox_ports[ COVOX_PORTS ] = { 0xfb, 0xdd, 0xdf };
static const unsigned covox_bits[ COVOX_PORTS ] = {
  FUSESOUND_COVOX_FB, FUSESOUND_COVOX_DD, FUSESOUND_COVOX_DF
};

/* Fuse's Covox level: the DAC value times 128 (sound.c,
   sound_covox_write), here around the centre value 0x80. */
#define COVOX_CENTRE 0x80
#define COVOX_SCALE  128

/* 16-bit generator output to the 24-bit mix. The sum of the two AY chips
   and the beeper stays below full scale. The Covox DACs share the 16 bits
   of the ula128 generator, where Fuse limits them (sound.c,
   sound_mix_ula_speaker). */
#define MIX_SCALE 128

/* AY register and data ports, decoded as Fuse decodes them on the 128K
   and the +2A/+3 (peripherals/sound/ay.c, ay_ports and ay_ports_plus3):
   A15 = 1, A1 = 0, and A14 = 1 for the register port, 0 for the data
   port. */
#define AY_PORT_MASK	 0xc002
#define AY_REGISTER_PORT 0xc000
#define AY_DATA_PORT	 0x8000

/* TurboSound chip select: these values written to the register port.
   The NedoPC Turbo Sound FM manual (NedoPC group, 2007, section
   "Principle of operation", in Russian) gives the select value as 11111<fm><stat><sel>, with sel = 0
   for chip D1, the chip a reset selects, and sel = 1 for chip D2. MAME
   selects the same way (bus/spectrum/ay/slot.cpp, address_w), for 0xFE
   and 0xFF only; so does this layer. */
#define TURBOSOUND_CHIP0 0xfe
#define TURBOSOUND_CHIP1 0xff

#define AY_REGISTERS_COUNT 16

/* Changes of the AY gates within a frame. */
#define MAX_GATE_CHANGES 256

typedef struct gate_change {
  libspectrum_dword at;		/* generator count */
  unsigned devices;
} gate_change;

static int initialised;

static libspectrum_dword clock_128, clock_48;	/* T-states per second */
static libspectrum_dword frame_48, frame_128;	/* T-states per frame */

static enum fuseula_machine frame_machine = FUSEULA_128K;
static libspectrum_dword frame_counts;		/* generator counts this frame */
static libspectrum_qword frame_48_remainder;	/* of the 48K frame scaling */

static unsigned devices = FUSESOUND_DEFAULT;
static unsigned frame_start_devices = FUSESOUND_DEFAULT;
static gate_change gate_changes[ MAX_GATE_CHANGES ];
static unsigned gate_change_count;

static unsigned selected_chip;
static libspectrum_byte selected_register[ 2 ];

static int ula_mic_on, ula_beeper_on;		/* the last ULA output */

static Blip_Synth *covox_synth[ COVOX_PORTS ];
static libspectrum_byte covox_value[ COVOX_PORTS ];

static int32_t mixed[ FUSESOUND_FRAME_MAX ];

/* --- helpers ----------------------------------------------------------- */

/* A T-state of the current frame as a generator count. */
static libspectrum_dword
to_counts( uint32_t nTState )
{
  libspectrum_qword at = nTState;

  if( frame_machine == FUSEULA_48K )
    at = at * clock_128 / clock_48;
  if( at >= frame_counts ) at = frame_counts - 1;

  return (libspectrum_dword)at;
}

static void
ay_write( unsigned chip, int reg, int val, libspectrum_dword at )
{
  if( chip == 0 )
    fusesound_ay0_sound_ay_write( reg, val, at );
  else
    fusesound_ay1_sound_ay_write( reg, val, at );
}

/* The ULA output into the frame model's ULA generator. */
static void
ula_output( libspectrum_dword at, int mic_on, int beeper_on )
{
  if( frame_machine == FUSEULA_48K )
    fusesound_ula48_sound_ula( at, mic_on, beeper_on );
  else
    fusesound_ula128_sound_ula( at, mic_on, beeper_on );
}

static void
covox_level( unsigned index, libspectrum_dword at )
{
  blip_synth_update( covox_synth[ index ], at,
                     ( (int)covox_value[ index ] - COVOX_CENTRE ) * COVOX_SCALE );
}

static int32_t
saturate( int32_t value )
{
  if( value > FUSESOUND_FULL_SCALE ) return FUSESOUND_FULL_SCALE;
  if( value < -FUSESOUND_FULL_SCALE - 1 ) return -FUSESOUND_FULL_SCALE - 1;
  return value;
}

static void
set_frame_machine( enum fuseula_machine Machine )
{
  frame_machine = Machine;

  if( Machine == FUSEULA_48K ) {
    /* 69,888 T-states at 3.5 MHz in counts at 3.5469 MHz, carrying the
       remainder into the next 48K frame. */
    libspectrum_qword scaled = (libspectrum_qword)frame_48 * clock_128 + frame_48_remainder;
    frame_counts = (libspectrum_dword)( scaled / clock_48 );
    frame_48_remainder = scaled % clock_48;
  } else {
    frame_counts = frame_128;
  }

  ay_machine.timings.tstates_per_frame = frame_counts;
  ula48_machine.timings.tstates_per_frame = frame_counts;
  ula128_machine.timings.tstates_per_frame = frame_counts;
}

/* --- the interface ----------------------------------------------------- */

int
fusesound_init( void )
{
  unsigned i;

  if( initialised ) return 0;

  clock_128 = libspectrum_timings_processor_speed( LIBSPECTRUM_MACHINE_128 );
  clock_48 = libspectrum_timings_processor_speed( LIBSPECTRUM_MACHINE_48 );
  frame_128 = libspectrum_timings_tstates_per_frame( LIBSPECTRUM_MACHINE_128 );
  frame_48 = libspectrum_timings_tstates_per_frame( LIBSPECTRUM_MACHINE_48 );
  if( clock_128 != FUSESOUND_CLOCK || clock_48 == 0 ) return 1;

  /* Fuse's sound settings at their defaults (settings.dat), at 48 kHz.
     The AY stereo separation and the speaker type are Fuse's defaults
     too, None (mono) and Automatic (fusestubs.c). */
  settings_current.sound = 1;
  settings_current.sound_freq = FUSESOUND_RATE;
  settings_current.sound_load = 1;
  settings_current.emulation_speed = 100;
  settings_current.volume_ay = 100;
  settings_current.volume_beeper = 100;
  settings_current.volume_covox = 100;
  settings_current.volume_specdrum = 100;
  settings_current.volume_uspeech = 100;

  memset( &ay_machine, 0, sizeof ay_machine );
  memset( &ula48_machine, 0, sizeof ula48_machine );
  memset( &ula128_machine, 0, sizeof ula128_machine );
  ay_machine.machine = LIBSPECTRUM_MACHINE_128;
  ay_machine.capabilities = LIBSPECTRUM_MACHINE_CAPABILITY_AY;
  ay_machine.timings.processor_speed = clock_128;
  ula48_machine.machine = LIBSPECTRUM_MACHINE_48;
  ula48_machine.capabilities = libspectrum_machine_capabilities( LIBSPECTRUM_MACHINE_48 )
                               & ~LIBSPECTRUM_MACHINE_CAPABILITY_AY;
  ula48_machine.timings.processor_speed = clock_128;
  ula128_machine.machine = LIBSPECTRUM_MACHINE_128;
  ula128_machine.capabilities = libspectrum_machine_capabilities( LIBSPECTRUM_MACHINE_128 )
                                & ~LIBSPECTRUM_MACHINE_CAPABILITY_AY;
  ula128_machine.timings.processor_speed = clock_128;
  set_frame_machine( FUSEULA_128K );

  fusesound_ay0_sound_init( NULL );
  fusesound_ay1_sound_init( NULL );
  fusesound_ula48_sound_init( NULL );
  fusesound_ula128_sound_init( NULL );
  if( !fusesound_ay0_sound_enabled || !fusesound_ay1_sound_enabled ||
      !fusesound_ula48_sound_enabled || !fusesound_ula128_sound_enabled )
    return 1;

  /* Fuse calls sound_ay_reset () on every machine reset (machine.c,
     machine_reset); it sets the AY output levels and zeroes the
     registers. Without it the AY is silent. */
  fusesound_ay0_sound_ay_reset();
  fusesound_ay1_sound_ay_reset();
  fusesound_ula48_sound_ay_reset();
  fusesound_ula128_sound_ay_reset();

  /* Each ULA generator's speaker type, set now and kept. */
  fusesound_ula48_sound_ula( 0, 0, 0 );
  fusesound_ula128_sound_ula( 0, 0, 0 );

  for( i = 0; i < COVOX_PORTS; i++ ) {
    covox_synth[i] = new_Blip_Synth();
    if( !covox_synth[i] ) return 1;
    blip_synth_set_volume( covox_synth[i], settings_current.volume_covox / 100.0 );
    blip_synth_set_output( covox_synth[i], fusesound_ula128_left_buf );
    blip_synth_set_treble_eq( covox_synth[i], 0.0 );
    covox_value[i] = COVOX_CENTRE;
  }

  devices = frame_start_devices = FUSESOUND_DEFAULT;
  gate_change_count = 0;
  selected_chip = 0;
  selected_register[0] = selected_register[1] = 0;
  ula_mic_on = ula_beeper_on = 0;

  initialised = 1;
  return 0;
}

void
fusesound_begin_frame( enum fuseula_machine Machine )
{
  int was_48 = frame_machine == FUSEULA_48K;

  if( !initialised ) return;

  set_frame_machine( Machine );

  /* A change between the 48K and the 128K class moves the beeper to the
     other ULA generator: the old one falls to 0, the new one starts at
     the ULA's level. */
  if( was_48 != ( Machine == FUSEULA_48K ) ) {
    if( was_48 )
      fusesound_ula48_sound_ula( 0, 0, 0 );
    else
      fusesound_ula128_sound_ula( 0, 0, 0 );
    if( devices & FUSESOUND_BEEPER ) ula_output( 0, ula_mic_on, ula_beeper_on );
  }
  frame_start_devices = devices;
  gate_change_count = 0;
}

void
fusesound_devices( uint32_t nTState, unsigned nDevices )
{
  libspectrum_dword at;
  unsigned changed, i;

  if( !initialised ) return;

  nDevices &= FUSESOUND_DEVICES;
  changed = devices ^ nDevices;
  if( !changed ) return;

  at = to_counts( nTState );

  if( changed & FUSESOUND_BEEPER ) {
    if( nDevices & FUSESOUND_BEEPER )
      ula_output( at, ula_mic_on, ula_beeper_on );
    else
      ula_output( at, 0, 0 );
  }

  for( i = 0; i < COVOX_PORTS; i++ ) {
    if( changed & covox_bits[i] ) {
      /* A Covox DAC starts at the centre when it is attached, and leaves
         the centre level when it is removed. */
      covox_value[i] = COVOX_CENTRE;
      covox_level( i, at );
    }
  }

  if( !( nDevices & FUSESOUND_TURBOSOUND ) ) selected_chip = 0;

  if( ( changed & ( FUSESOUND_AY | FUSESOUND_TURBOSOUND ) ) &&
      gate_change_count < MAX_GATE_CHANGES ) {
    gate_changes[ gate_change_count ].at = at;
    gate_changes[ gate_change_count ].devices = nDevices;
    gate_change_count++;
  }

  devices = nDevices;
}

unsigned
fusesound_current_devices( void )
{
  return devices;
}

void
fusesound_io_write( uint32_t nTState, uint16_t nPort, uint8_t nData )
{
  unsigned i;

  if( !initialised ) return;

  if( devices & FUSESOUND_AY ) {
    unsigned chip = ( devices & FUSESOUND_TURBOSOUND ) ? selected_chip : 0;

    if( ( nPort & AY_PORT_MASK ) == AY_REGISTER_PORT ) {
      if( ( devices & FUSESOUND_TURBOSOUND ) &&
          ( nData == TURBOSOUND_CHIP0 || nData == TURBOSOUND_CHIP1 ) ) {
        selected_chip = nData == TURBOSOUND_CHIP0 ? 0 : 1;
      } else {
        /* Fuse keeps the low four bits (ay.c, set_current_register). */
        selected_register[ chip ] = nData & 0x0f;
      }
    } else if( ( nPort & AY_PORT_MASK ) == AY_DATA_PORT ) {
      ay_write( chip, selected_register[ chip ], nData, to_counts( nTState ) );
    }
  }

  /* Covox: the low eight address bits, as Fuse decodes 0xFB and 0xDD
     (peripherals/sound/covox.c). */
  for( i = 0; i < COVOX_PORTS; i++ ) {
    if( ( devices & covox_bits[i] ) && ( nPort & 0xff ) == covox_ports[i] ) {
      covox_value[i] = nData;
      covox_level( i, to_counts( nTState ) );
    }
  }
}

void
fusesound_ula( uint32_t at_tstates, int mic_on, int beeper_on )
{
  if( !initialised ) return;

  ula_mic_on = !!mic_on;
  ula_beeper_on = !!beeper_on;
  if( devices & FUSESOUND_BEEPER )
    ula_output( to_counts( at_tstates ), ula_mic_on, ula_beeper_on );
}

void
fusesound_reset( uint32_t nTState )
{
  libspectrum_dword at;
  unsigned chip;
  int reg;

  if( !initialised ) return;

  at = to_counts( nTState );
  for( chip = 0; chip < 2; chip++ ) {
    for( reg = 0; reg < AY_REGISTERS_COUNT; reg++ ) ay_write( chip, reg, 0, at );
    selected_register[ chip ] = 0;
  }
  selected_chip = 0;
}

unsigned
fusesound_end_frame( const int32_t **ppSamples )
{
  int count, i;
  unsigned next_change = 0, gates = frame_start_devices;

  *ppSamples = mixed;
  if( !initialised ) return 0;

  fusesound_ay0_sound_frame();
  fusesound_ay1_sound_frame();
  fusesound_ula48_sound_frame();
  fusesound_ula128_sound_frame();

  /* The generators have the same clock, sample rate, frame lengths and
     sample phase, so they give the same number of samples. */
  count = fusesound_ula128_output_count;
  if( fusesound_ula48_output_count < count ) count = fusesound_ula48_output_count;
  if( fusesound_ay0_output_count < count ) count = fusesound_ay0_output_count;
  if( fusesound_ay1_output_count < count ) count = fusesound_ay1_output_count;

  for( i = 0; i < count; i++ ) {
    int32_t sum = fusesound_ula48_output[i] + fusesound_ula128_output[i];

    /* A gate change applies from the sample its time falls in. */
    while( next_change < gate_change_count &&
           (libspectrum_qword)gate_changes[ next_change ].at * count <
           (libspectrum_qword)( i + 1 ) * frame_counts ) {
      gates = gate_changes[ next_change ].devices;
      next_change++;
    }

    if( gates & FUSESOUND_AY ) {
      sum += fusesound_ay0_output[i];
      if( gates & FUSESOUND_TURBOSOUND ) sum += fusesound_ay1_output[i];
    }

    mixed[i] = saturate( sum * MIX_SCALE );
  }

  fusesound_ay0_output_count = fusesound_ay1_output_count = 0;
  fusesound_ula48_output_count = fusesound_ula128_output_count = 0;
  frame_start_devices = devices;
  gate_change_count = 0;

  return (unsigned)count;
}

uint8_t
fusesound_ay_selected_register( unsigned nChip )
{
  return selected_register[ nChip & 1 ];
}

unsigned
fusesound_ay_selected_chip( void )
{
  return selected_chip;
}

uint8_t
fusesound_covox_value( unsigned nIndex )
{
  return nIndex < COVOX_PORTS ? covox_value[ nIndex ] : 0;
}
