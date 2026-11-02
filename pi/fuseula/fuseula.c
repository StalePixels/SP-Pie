/*
 * fuseula.c
 *
 * Drives Fuse's display, memory and port code with the Spectrum's writes,
 * and takes Fuse's drawing calls (uidisplay_plot8 and friends) into the
 * picture. See fuseula.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <config.h>

#include <string.h>

#include "libspectrum.h"

#include "compat.h"
#include "display.h"
#include "infrastructure/startup_manager.h"
#include "machine.h"
#include "machines/machines_periph.h"
#include "memory_pages.h"
#include "periph.h"
#include "peripherals/ula.h"
#include "spectrum.h"
#include "ui/uidisplay.h"

#include "fuseula.h"
#include "fusestubs.h"

/* Picture geometry. Fuse's area (DISPLAY_ASPECT_WIDTH x
   DISPLAY_SCREEN_HEIGHT, 320 x 240) sits in the middle of the picture. */
#define CHUNK_PIXELS	8
#define PICTURE_CHUNKS	( FUSEULA_PICTURE_WIDTH / CHUNK_PIXELS )		/* 44 */
#define FUSE_LEFT	( ( PICTURE_CHUNKS - DISPLAY_SCREEN_WIDTH_COLS ) / 2 )	/* 2 chunks */
#define FUSE_TOP	( ( FUSEULA_PICTURE_HEIGHT - DISPLAY_SCREEN_HEIGHT ) / 2 )	/* 15 lines */

/* Fuse's beam rule (display.c, get_beam_position): a change at T-state t
   on line y shows from chunk ( t - line_times[y] ) / 4 on. So the chunk
   that starts at T-state T shows the state of T + 3. */
#define CHUNK_TSTATES	4

/* ULAplus. Register port 0xBF3B and data port 0xFF3B: A15..A14 = 10 or 11,
   A7..A6 = 00, A2 = 0, A0 = 1, the decode of the TK-Pie board. The
   register byte is a group (bits 7..6) and a sub-group (bits 5..0). Group 0
   writes palette entries, group 1 the mode; mode bit 0 turns the palette
   on. */
#define ULAPLUS_MASK		0xC0C5
#define ULAPLUS_REGISTER	0x8001
#define ULAPLUS_DATA		0xC001
#define ULAPLUS_ENTRIES		64

/* What the output of a chunk depends on besides Fuse's own state: the
   ULAplus palette and mode, and the border colour for the area outside
   Fuse's. */
typedef struct output_state {
  libspectrum_byte palette[ ULAPLUS_ENTRIES ];
  libspectrum_byte mode;
  libspectrum_byte border;
} output_state;

#define CHANGE_MODE	ULAPLUS_ENTRIES
#define CHANGE_BORDER	( ULAPLUS_ENTRIES + 1 )

typedef struct output_change {
  libspectrum_dword tstate;
  libspectrum_byte what;	/* palette entry, CHANGE_MODE or CHANGE_BORDER */
  libspectrum_byte value;
} output_change;

/* More changes than this in one frame are applied at the frame's end. */
#define MAX_CHANGES 4096

static libspectrum_dword *picture;
static unsigned picture_pitch;

static enum fuseula_machine current_machine = FUSEULA_48K;

static const libspectrum_machine fuse_machines[ FUSEULA_MACHINES ] = {
  LIBSPECTRUM_MACHINE_48,
  LIBSPECTRUM_MACHINE_128,
  LIBSPECTRUM_MACHINE_PLUS3,
};

static libspectrum_byte ulaplus_register;

/* The state at the start of the frame, the changes since, and the state
   after them. */
static output_state frame_start, current;
static output_change changes[ MAX_CHANGES ];
static unsigned change_count;
static int ulaplus_changed;		/* palette or mode changed this frame */
static int ulaplus_changed_before;	/* ... or in the frame before */

/* Lookup cursor: the state at cursor_tstate, after the first cursor_next
   changes. */
static output_state cursor;
static libspectrum_dword cursor_tstate;
static unsigned cursor_next;

/* Pixel colours: the 16 Spectrum colours (BRIGHT * 8 + G R B) and the
   converted ULAplus palette. */
static libspectrum_dword normal_colours[ 16 ];

static libspectrum_dword
make_rgb( unsigned r, unsigned g, unsigned b )
{
  return 0xff000000u | ( r << 16 ) | ( g << 8 ) | b;
}

/* ULAplus GGGRRRBB to 24-bit colour. The missing low blue bit is the OR of
   the two stored blue bits; 3-bit levels scale to 0..255. */
static libspectrum_dword
ulaplus_colour( libspectrum_byte grb )
{
  unsigned g = ( grb >> 5 ) & 7, r = ( grb >> 2 ) & 7;
  unsigned b = ( ( grb & 3 ) << 1 ) | ( ( grb & 3 ) ? 1 : 0 );

  return make_rgb( r * 255 / 7, g * 255 / 7, b * 255 / 7 );
}

static void
init_colours( void )
{
  unsigned i;

  for( i = 0; i < 16; i++ ) {
    unsigned level = ( i & 8 ) ? 0xff : 0xa0;
    normal_colours[i] = make_rgb( ( i & 2 ) ? level : 0, ( i & 4 ) ? level : 0,
                                  ( i & 1 ) ? level : 0 );
  }
}

/* --- the output state timeline ---------------------------------------- */

static void
push_change( libspectrum_byte what, libspectrum_byte value )
{
  if( change_count == MAX_CHANGES ) return;

  changes[ change_count ].tstate = tstates;
  changes[ change_count ].what = what;
  changes[ change_count ].value = value;
  change_count++;
}

static void
apply_change( output_state *state, const output_change *change )
{
  if( change->what < ULAPLUS_ENTRIES ) {
    state->palette[ change->what ] = change->value;
  } else if( change->what == CHANGE_MODE ) {
    state->mode = change->value;
  } else {
    state->border = change->value;
  }
}

/* The output state that applies to a chunk starting at T-state chunk_start.
   Fuse draws in beam order within a pass, so the cursor mostly moves
   forward. */
static const output_state *
state_at( libspectrum_signed_dword chunk_start )
{
  libspectrum_dword t;

  if( change_count == 0 ) return &current;

  t = chunk_start + CHUNK_TSTATES - 1 < 0 ? 0 :
      (libspectrum_dword)( chunk_start + CHUNK_TSTATES - 1 );

  if( t < cursor_tstate ) {
    cursor = frame_start;
    cursor_next = 0;
  }
  while( cursor_next < change_count && changes[ cursor_next ].tstate <= t ) {
    apply_change( &cursor, &changes[ cursor_next ] );
    cursor_next++;
  }
  cursor_tstate = t;

  return &cursor;
}

/* First T-state of chunk x (0 = the leftmost chunk of the picture) on
   picture line y. */
static libspectrum_signed_dword
chunk_tstate( int x, int y )
{
  return (libspectrum_signed_dword) machine_current->line_times[ 0 ]
         + ( y - FUSE_TOP ) * (libspectrum_signed_dword) machine_current->timings.tstates_per_line
         + ( x - FUSE_LEFT ) * CHUNK_TSTATES;
}

static void
fill_chunk( int x, int y, libspectrum_byte data, libspectrum_dword ink,
            libspectrum_dword paper )
{
  libspectrum_dword *out = picture + y * picture_pitch + x * CHUNK_PIXELS;
  int i;

  for( i = 0; i < CHUNK_PIXELS; i++ ) {
    out[i] = ( data & ( 0x80 >> i ) ) ? ink : paper;
  }
}

/* --- Fuse's drawing calls ---------------------------------------------- */

int
uidisplay_init( int width GCC_UNUSED, int height GCC_UNUSED )
{
  return 0;
}

int
uidisplay_end( void )
{
  return 0;
}

void
uidisplay_area( int x GCC_UNUSED, int y GCC_UNUSED, int w GCC_UNUSED,
                int h GCC_UNUSED )
{
}

void
uidisplay_frame_end( void )
{
}

/* One 8-pixel chunk of Fuse's area: chunk column x (0 to 39) on line y
   (0 to 239). Screen chunks come with the pixel byte, border chunks with
   data 0 and the border colour as paper. ink and paper are Fuse's colour
   numbers, BRIGHT * 8 + colour, with FLASH already applied. */
void
uidisplay_plot8( int x, int y, libspectrum_byte data, libspectrum_byte ink,
                 libspectrum_byte paper )
{
  int px = x + FUSE_LEFT, py = y + FUSE_TOP;
  const output_state *state;

  if( !picture ) return;

  state = state_at( chunk_tstate( px, py ) );

  if( !( state->mode & 0x01 ) ) {
    fill_chunk( px, py, data, normal_colours[ ink & 0x0f ],
                normal_colours[ paper & 0x0f ] );
    return;
  }

  if( x >= DISPLAY_BORDER_WIDTH_COLS &&
      x < DISPLAY_BORDER_WIDTH_COLS + DISPLAY_WIDTH_COLS &&
      y >= DISPLAY_BORDER_HEIGHT && y < DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT ) {

    /* Fuse read this attribute from the shown screen just before the
       call; read it again for the bits Fuse does not pass on. In ULAplus
       mode bits 7..6 select one of four 16-entry groups and FLASH does not
       swap ink and paper. */
    int column = x - DISPLAY_BORDER_WIDTH_COLS, row = y - DISPLAY_BORDER_HEIGHT;
    libspectrum_byte attr =
      RAM[ memory_current_screen ][ display_attr_start[ row ] + column ];
    unsigned group = ( attr >> 6 ) * 16;

    fill_chunk( px, py, data,
                ulaplus_colour( state->palette[ group + ( attr & 7 ) ] ),
                ulaplus_colour( state->palette[ group + 8 + ( ( attr >> 3 ) & 7 ) ] ) );
  } else {
    libspectrum_dword colour =
      ulaplus_colour( state->palette[ 8 + ( paper & 7 ) ] );
    fill_chunk( px, py, 0, colour, colour );
  }
}

/* Timex and Pentagon modes; not used by the models built here. */
void
uidisplay_plot16( int x GCC_UNUSED, int y GCC_UNUSED,
                  libspectrum_word data GCC_UNUSED, libspectrum_byte ink GCC_UNUSED,
                  libspectrum_byte paper GCC_UNUSED )
{
}

void
uidisplay_putpixel( int x GCC_UNUSED, int y GCC_UNUSED, int colour GCC_UNUSED )
{
}

/* --- the border outside Fuse's area ------------------------------------ */

static void
draw_outer_chunk( int x, int y )
{
  const output_state *state = state_at( chunk_tstate( x, y ) );
  libspectrum_dword colour = ( state->mode & 0x01 ) ?
    ulaplus_colour( state->palette[ 8 + state->border ] ) :
    normal_colours[ state->border ];

  fill_chunk( x, y, 0, colour, colour );
}

static void
draw_outer_border( void )
{
  int x, y;

  for( y = 0; y < FUSEULA_PICTURE_HEIGHT; y++ ) {
    if( y < FUSE_TOP || y >= FUSE_TOP + DISPLAY_SCREEN_HEIGHT ) {
      for( x = 0; x < PICTURE_CHUNKS; x++ ) draw_outer_chunk( x, y );
    } else {
      for( x = 0; x < FUSE_LEFT; x++ ) draw_outer_chunk( x, y );
      for( x = FUSE_LEFT + DISPLAY_SCREEN_WIDTH_COLS; x < PICTURE_CHUNKS; x++ )
        draw_outer_chunk( x, y );
    }
  }
}

/* --- the interface ----------------------------------------------------- */

static void
ulaplus_mark_changed( void )
{
  /* Fuse redraws only chunks whose pixel byte or attribute changed. A
     palette or mode change changes the colours of the rest of this frame
     and of the next frame, so both are redrawn in full. */
  display_refresh_all();
  ulaplus_changed = 1;
}

static void
ulaplus_write( libspectrum_word port, libspectrum_byte b )
{
  if( ( port & ULAPLUS_MASK ) == ULAPLUS_REGISTER ) {
    ulaplus_register = b;
  } else if( ( port & ULAPLUS_MASK ) == ULAPLUS_DATA ) {
    switch( ulaplus_register >> 6 ) {
    case 0:
      if( current.palette[ ulaplus_register & 0x3f ] != b ) {
        current.palette[ ulaplus_register & 0x3f ] = b;
        push_change( ulaplus_register & 0x3f, b );
        ulaplus_mark_changed();
      }
      break;
    case 1:
      if( current.mode != b ) {
        current.mode = b;
        push_change( CHANGE_MODE, b );
        ulaplus_mark_changed();
      }
      break;
    default:
      break;
    }
  }
}

int
fuseula_init( uint32_t *pPicture, unsigned nPitch )
{
  static display_startup_context display_context;
  static int started = 0;
  static int argc = 0;
  static char **argv = NULL;

  picture = pPicture;
  picture_pitch = nPitch;
  init_colours();

  memset( &current, 0, sizeof current );
  frame_start = current;
  change_count = 0;
  ulaplus_register = 0;
  ulaplus_changed = ulaplus_changed_before = 0;

  display_context.argc = &argc;
  display_context.argv = &argv;

  /* Fuse's modules, in an order that meets their dependencies. They start
     once; a later call only selects the 48K again. */
  if( !started ) {
    memory_register_startup();
    ula_register_startup();
    machines_periph_register_startup();
    spectrum_register_startup();
    machine_register_startup();
    display_register_startup( &display_context );
    if( fusestubs_run_startup() ) return 1;
    started = 1;
  }

  current_machine = FUSEULA_48K;
  if( machine_select( fuse_machines[ current_machine ] ) ) return 1;

  current.border = display_lores_border;
  frame_start = current;

  return 0;
}

void
fuseula_select( enum fuseula_machine Machine )
{
  if( Machine >= FUSEULA_MACHINES || Machine == current_machine ) return;

  if( machine_select( fuse_machines[ Machine ] ) ) return;
  current_machine = Machine;
}

enum fuseula_machine
fuseula_current_machine( void )
{
  return current_machine;
}

uint32_t
fuseula_frame_tstates( enum fuseula_machine Machine )
{
  if( Machine >= FUSEULA_MACHINES ) Machine = FUSEULA_48K;
  return libspectrum_timings_tstates_per_frame( fuse_machines[ Machine ] );
}

void
fuseula_memory_write( uint32_t nTState, uint16_t nAddress, uint8_t nData )
{
  tstates = nTState;
  writebyte_internal( nAddress, nData );
}

void
fuseula_io_write( uint32_t nTState, uint16_t nPort, uint8_t nData )
{
  tstates = nTState;
  writeport_internal( nPort, nData );
  ulaplus_write( nPort, nData );

  if( display_lores_border != current.border ) {
    current.border = display_lores_border;
    push_change( CHANGE_BORDER, current.border );
  }
}

void
fuseula_reset( uint32_t nTState )
{
  tstates = nTState;
  machine_reset( 0 );

  ulaplus_register = 0;
  if( current.mode != 0 ) {
    current.mode = 0;
    push_change( CHANGE_MODE, 0 );
    ulaplus_mark_changed();
  }
}

void
fuseula_end_frame( void )
{
  tstates = machine_current->timings.tstates_per_frame;
  display_frame();
  draw_outer_border();

  /* The next frame starts from the state at the end of this one. */
  frame_start = current;
  change_count = 0;
  cursor = current;
  cursor_tstate = 0;
  cursor_next = 0;

  ulaplus_changed_before = ulaplus_changed;
  ulaplus_changed = 0;
  if( ulaplus_changed_before ) display_refresh_all();

  tstates = 0;
}

uint8_t
fuseula_ram( unsigned nBank, unsigned nOffset )
{
  return RAM[ nBank & 7 ][ nOffset & 0x3fff ];
}

uint8_t
fuseula_border( void )
{
  return display_lores_border;
}

unsigned
fuseula_screen_bank( void )
{
  return memory_current_screen;
}

int
fuseula_ulaplus_on( void )
{
  return current.mode & 0x01;
}

uint32_t
fuseula_line_tstates( void )
{
  return machine_current->timings.tstates_per_line;
}

uint32_t
fuseula_top_left_border_tstate( void )
{
  return machine_current->line_times[ 0 ];
}
