/*
 * fusestubs.c
 *
 * Definitions for the parts of Fuse and libspectrum that the files SP-Pie
 * builds refer to but that are not built. See fusestubs.h.
 *
 * SP-Pie runs no Z80 and has no sound, tape, disk, debugger, menus or other
 * peripherals. Each function here does nothing and returns success, and
 * each variable has the value Fuse gives it when the feature is absent or
 * off. The few that do work are marked.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <config.h>

#include <string.h>

#include "libspectrum.h"
#include "internals.h"		/* libspectrum's own declarations */

#include "compat.h"
#include "debugger/breakpoint.h"
#include "debugger/debugger.h"
#include "event.h"
#include "fuse.h"
#include "infrastructure/startup_manager.h"
#include "keyboard.h"
#include "loader.h"
#include "machine.h"
#include "machines/machines.h"
#include "machines/pentagon.h"
#include "machines/scorpion.h"
#include "machines/tc2068.h"
#include "module.h"
#include "movie.h"
#include "options.h"
#include "peripherals/disk/beta.h"
#include "peripherals/disk/disciple.h"
#include "peripherals/disk/disk.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/opus.h"
#include "peripherals/disk/plusd.h"
#include "peripherals/disk/upd_fdc.h"
#include "peripherals/if1.h"
#include "peripherals/multiface.h"
#include "peripherals/printer.h"
#include "peripherals/scld.h"
#include "peripherals/sound/ay.h"
#include "peripherals/sound/uspeech.h"
#include "peripherals/spectranet.h"
#include "peripherals/ttx2000s.h"
#include "phantom_typist.h"
#include "pokefinder/pokemem.h"
#include "profile.h"
#include "psg.h"
#include "rzx.h"
#include "settings.h"
#include "sound.h"
#include "tape.h"
#include "timer/timer.h"
#include "ui/ui.h"
#include "ui/uijoystick.h"
#include "ui/uimedia.h"
#include "utils.h"
#include "z80/z80.h"

#include "fusestubs.h"

/* --- start-up manager (works) ------------------------------------------ */

#define MAX_MODULES 16

static startup_manager_init_fn init_fns[ MAX_MODULES ];
static void *init_contexts[ MAX_MODULES ];
static int module_count;

void
startup_manager_register( startup_manager_module module GCC_UNUSED,
                          startup_manager_module *dependencies GCC_UNUSED,
                          size_t dependency_count GCC_UNUSED,
                          startup_manager_init_fn init_fn, void *init_context,
                          startup_manager_end_fn end_fn GCC_UNUSED )
{
  if( module_count == MAX_MODULES ) return;

  init_fns[ module_count ] = init_fn;
  init_contexts[ module_count ] = init_context;
  module_count++;
}

void
startup_manager_register_no_dependencies( startup_manager_module module,
                                          startup_manager_init_fn init_fn,
                                          void *init_context,
                                          startup_manager_end_fn end_fn )
{
  startup_manager_register( module, NULL, 0, init_fn, init_context, end_fn );
}

int
fusestubs_run_startup( void )
{
  int i;

  for( i = 0; i < module_count; i++ ) {
    if( init_fns[i] && init_fns[i]( init_contexts[i] ) ) return 1;
  }
  module_count = 0;

  return 0;
}

/* --- settings (zero: Fuse's defaults for every setting used here) ------ */

settings_info settings_current;
settings_info settings_default;

void
settings_set_string( char **string_setting, const char *value )
{
  libspectrum_free( *string_setting );
  *string_setting = value ? libspectrum_safe_strdup( value ) : NULL;
}

/* --- files (works) ----------------------------------------------------- */

/* Fuse loads each ROM through this. The display reads only RAM, and ROM
   pages are not writable, so a ROM of zeros is enough: every ROM the
   models built here load is 16 KB. */
int
utils_read_auxiliary_file( const char *filename, utils_file *file,
                           utils_aux_type type GCC_UNUSED )
{
  file->filename = filename;
  file->length = 0x4000;
  file->buffer = libspectrum_new0( unsigned char, file->length );
  return 0;
}

void
utils_close_file( utils_file *file )
{
  libspectrum_free( file->buffer );
  file->buffer = NULL;
}

char *
utils_safe_strdup( const char *src )
{
  return src ? libspectrum_safe_strdup( src ) : NULL;
}

char *
libspectrum_safe_strdup( const char *src )
{
  size_t length = strlen( src ) + 1;
  char *copy = libspectrum_new( char, length );
  memcpy( copy, src, length );
  return copy;
}

/* --- libspectrum parts not built --------------------------------------- */

void
libspectrum_init_bits_set( void )
{
}

libspectrum_error
libspectrum_rzx_playback( libspectrum_rzx *rzx GCC_UNUSED,
                          libspectrum_byte *byte GCC_UNUSED )
{
  return LIBSPECTRUM_ERROR_INVALID;
}

/* --- Fuse core --------------------------------------------------------- */

void
fuse_abort( void )
{
  while( 1 ) {
  }
}

processor z80;

int
z80_interrupt( void )
{
  return 0;
}

int
module_register( module_info_t *module GCC_UNUSED )
{
  return 0;
}

void
module_reset( int hard_reset GCC_UNUSED )
{
}

void
module_romcs( void )
{
}

/* Fuse's event queue. Time here comes from the CPLD, not from events. */
int event_type_null;
int timer_event;

int
event_register( event_fn_t fn GCC_UNUSED, const char *description GCC_UNUSED )
{
  static int next_type = 1;
  return next_type++;
}

void
event_add_with_data( libspectrum_dword event_time GCC_UNUSED,
                     int type GCC_UNUSED, void *user_data GCC_UNUSED )
{
}

void
event_force_events( void )
{
}

void
event_frame( libspectrum_dword tstates_per_frame GCC_UNUSED )
{
}

void
event_reset( void )
{
}

enum debugger_mode_t debugger_mode = DEBUGGER_MODE_INACTIVE;

int
debugger_check( debugger_breakpoint_type type GCC_UNUSED,
                libspectrum_dword value GCC_UNUSED )
{
  return 0;
}

int
debugger_add_time_events( void )
{
  return 0;
}

void
debugger_breakpoint_reduce_tstates( libspectrum_dword tstates GCC_UNUSED )
{
}

int
debugger_event_register( const char *type GCC_UNUSED,
                         const char *detail GCC_UNUSED )
{
  return 0;
}

void
debugger_system_variable_register( const char *type GCC_UNUSED,
                                   const char *detail GCC_UNUSED,
                                   debugger_get_system_variable_fn_t get GCC_UNUSED,
                                   debugger_set_system_variable_fn_t set GCC_UNUSED )
{
}

/* --- other machines, never selected ------------------------------------ */

/* Fuse adds every machine it knows at start-up. Each of these gives its
   machine type and identifier only; SP-Pie never selects them. */
#define OTHER_MACHINE( function, type, name ) \
  int function( fuse_machine_info *machine ) \
  { \
    machine->machine = type; \
    machine->id = name; \
    return 0; \
  }

OTHER_MACHINE( spec16_init, LIBSPECTRUM_MACHINE_16, "16" )
OTHER_MACHINE( spec48_ntsc_init, LIBSPECTRUM_MACHINE_48_NTSC, "48_ntsc" )
OTHER_MACHINE( specplus3e_init, LIBSPECTRUM_MACHINE_PLUS3E, "plus3e" )
OTHER_MACHINE( tc2048_init, LIBSPECTRUM_MACHINE_TC2048, "2048" )
OTHER_MACHINE( tc2068_init, LIBSPECTRUM_MACHINE_TC2068, "2068" )
OTHER_MACHINE( ts2068_init, LIBSPECTRUM_MACHINE_TS2068, "ts2068" )
OTHER_MACHINE( pentagon_init, LIBSPECTRUM_MACHINE_PENT, "pentagon" )
OTHER_MACHINE( pentagon512_init, LIBSPECTRUM_MACHINE_PENT512, "pentagon512" )
OTHER_MACHINE( pentagon1024_init, LIBSPECTRUM_MACHINE_PENT1024, "pentagon1024" )
OTHER_MACHINE( scorpion_init, LIBSPECTRUM_MACHINE_SCORP, "scorpion" )
OTHER_MACHINE( spec_se_init, LIBSPECTRUM_MACHINE_SE, "se" )

libspectrum_byte
pentagon_select_1f_read( libspectrum_word port GCC_UNUSED,
                         libspectrum_byte *attached GCC_UNUSED )
{
  return 0xff;
}

libspectrum_byte
pentagon_select_ff_read( libspectrum_word port GCC_UNUSED,
                         libspectrum_byte *attached GCC_UNUSED )
{
  return 0xff;
}

void
pentagon1024_memoryport_write( libspectrum_word port GCC_UNUSED,
                               libspectrum_byte b GCC_UNUSED )
{
}

void
pentagon1024_v22_memoryport_write( libspectrum_word port GCC_UNUSED,
                                   libspectrum_byte b GCC_UNUSED )
{
}

libspectrum_byte
tc2068_ay_registerport_read( libspectrum_word port GCC_UNUSED,
                             libspectrum_byte *attached GCC_UNUSED )
{
  return 0xff;
}

libspectrum_byte
tc2068_ay_dataport_read( libspectrum_word port GCC_UNUSED,
                         libspectrum_byte *attached GCC_UNUSED )
{
  return 0xff;
}

/* Timex screen modes: off, so the Sinclair screen is used. */
scld scld_last_dec;

libspectrum_byte
hires_get_attr( void )
{
  return 0;
}

libspectrum_byte
hires_convert_dec( libspectrum_byte attr GCC_UNUSED )
{
  return 0;
}

/* --- sound, tape, recording -------------------------------------------- */

int sound_enabled;

void sound_init( const char *device GCC_UNUSED ) { }
void sound_end( void ) { }
void sound_frame( void ) { }
void sound_ay_reset( void ) { }

void
sound_ula( libspectrum_dword at_tstates GCC_UNUSED, int mic_on GCC_UNUSED,
           int beeper_on GCC_UNUSED )
{
}

void
ay_registerport_write( libspectrum_word port GCC_UNUSED,
                       libspectrum_byte b GCC_UNUSED )
{
}

void
ay_dataport_write( libspectrum_word port GCC_UNUSED,
                   libspectrum_byte b GCC_UNUSED )
{
}

int psg_frame( void ) { return 0; }

int tape_microphone;

int tape_stop( void ) { return 0; }

void loader_detect_loader( void ) { }
void loader_frame( libspectrum_dword frame_length GCC_UNUSED ) { }

libspectrum_rzx *rzx;
int rzx_playback;
int rzx_recording;

int rzx_frame( void ) { return 0; }
int rzx_stop_playback( int add_interrupt GCC_UNUSED ) { return 0; }
int rzx_stop_recording( void ) { return 0; }
int rzx_store_byte( libspectrum_byte value GCC_UNUSED ) { return 0; }

int movie_recording;

void movie_start_frame( void ) { }
void movie_stop( void ) { }

void
movie_add_area( int x GCC_UNUSED, int y GCC_UNUSED, int w GCC_UNUSED,
                int h GCC_UNUSED )
{
}

int profile_active;

void profile_frame( libspectrum_dword frame_length GCC_UNUSED ) { }

int timer_estimate_speed( void ) { return 0; }

void pokemem_clear( void ) { }

/* --- keyboard, joystick, mouse, menus ---------------------------------- */

libspectrum_byte
keyboard_read( libspectrum_byte porth GCC_UNUSED )
{
  return 0xff;
}

int ui_mouse_present, ui_mouse_grabbed;

int ui_init( int *argc GCC_UNUSED, char ***argv GCC_UNUSED ) { return 0; }
int ui_event( void ) { return 0; }
void ui_error_frame( void ) { }
void ui_joystick_poll( void ) { }
int ui_mouse_grab( int startup GCC_UNUSED ) { return 0; }
int ui_mouse_release( int suspend GCC_UNUSED ) { return 0; }
int ui_widgets_reset( void ) { return 0; }
void ui_menu_disk_update( void ) { }

int
ui_menu_activate( ui_menu_item item GCC_UNUSED, int active GCC_UNUSED )
{
  return 0;
}

int
ui_error( ui_error_level severity GCC_UNUSED, const char *format GCC_UNUSED,
          ... )
{
  return 0;
}

int
ui_media_drive_register( ui_media_drive_info_t *drive GCC_UNUSED )
{
  return 0;
}

int
ui_media_drive_insert( const ui_media_drive_info_t *drive GCC_UNUSED,
                       const char *filename GCC_UNUSED, int autoload GCC_UNUSED )
{
  return 0;
}

void
ui_media_drive_update_menus( const ui_media_drive_info_t *drive GCC_UNUSED,
                             unsigned flags GCC_UNUSED )
{
}

int option_enumerate_diskoptions_drive_plus3a_type( void ) { return 0; }
int option_enumerate_diskoptions_drive_plus3b_type( void ) { return 0; }

/* --- disks: the +3 drive controller and other interfaces, absent ------- */

/* Fuse's table has an entry per drive type; the +3 code reads the first
   few. Zero entries are disabled drives. */
const fdd_params_t fdd_params[ 16 ];

int
fdd_init( fdd_t *d GCC_UNUSED, fdd_type_t type GCC_UNUSED,
          const fdd_params_t *dt GCC_UNUSED, int reinit GCC_UNUSED )
{
  return 0;
}

void fdd_motoron( fdd_t *d GCC_UNUSED, int on GCC_UNUSED ) { }

int disk_preformat( disk_t *d GCC_UNUSED ) { return 0; }

/* The +3 code fills in fields of the controller it allocates. */
upd_fdc *
upd_fdc_alloc_fdc( upd_type_t type GCC_UNUSED, upd_clock_t clock GCC_UNUSED )
{
  return libspectrum_new0( upd_fdc, 1 );
}

void upd_fdc_master_reset( upd_fdc *f GCC_UNUSED ) { }

libspectrum_byte
upd_fdc_read_status( upd_fdc *f GCC_UNUSED )
{
  return 0xff;
}

libspectrum_byte
upd_fdc_read_data( upd_fdc *f GCC_UNUSED )
{
  return 0xff;
}

void upd_fdc_write_data( upd_fdc *f GCC_UNUSED, libspectrum_byte b GCC_UNUSED ) { }

int beta_active;
int beta_builtin;

#define BETA_READ( function ) \
  libspectrum_byte function( libspectrum_word port GCC_UNUSED, \
                             libspectrum_byte *attached GCC_UNUSED ) \
  { \
    return 0xff; \
  }
#define BETA_WRITE( function ) \
  void function( libspectrum_word port GCC_UNUSED, \
                 libspectrum_byte b GCC_UNUSED ) \
  { \
  }

BETA_READ( beta_dr_read )
BETA_READ( beta_sec_read )
BETA_READ( beta_sp_read )
BETA_READ( beta_tr_read )
BETA_WRITE( beta_cr_write )
BETA_WRITE( beta_dr_write )
BETA_WRITE( beta_sec_write )
BETA_WRITE( beta_sp_write )
BETA_WRITE( beta_tr_write )

int disciple_active, disciple_available;
int plusd_active, plusd_available;
int opus_active, opus_available;

libspectrum_byte opus_read( libspectrum_word address GCC_UNUSED ) { return 0xff; }
void opus_write( libspectrum_word address GCC_UNUSED, libspectrum_byte b GCC_UNUSED ) { }

void if1_update_menu( void ) { }
void multiface_status_update( void ) { }

void printer_frame( void ) { }
void printer_parallel_strobe_write( int on GCC_UNUSED ) { }

void phantom_typist_activate_disk( void ) { }
void phantom_typist_frame( void ) { }

libspectrum_byte
phantom_typist_ula_read( libspectrum_word port GCC_UNUSED )
{
  return 0xff;
}

/* --- memory-mapped peripherals, absent --------------------------------- */

int spectranet_paged;
int spectranet_w5100_paged_a, spectranet_w5100_paged_b;
int spectranet_flash_paged_a, spectranet_flash_paged_b;

libspectrum_byte
spectranet_flash_rom_read( memory_page *page GCC_UNUSED,
                           libspectrum_word address GCC_UNUSED )
{
  return 0xff;
}

void
spectranet_flash_rom_write( libspectrum_word address GCC_UNUSED,
                            libspectrum_byte b GCC_UNUSED )
{
}

libspectrum_byte
spectranet_w5100_read( memory_page *page GCC_UNUSED,
                       libspectrum_word address GCC_UNUSED )
{
  return 0xff;
}

void
spectranet_w5100_write( memory_page *page GCC_UNUSED,
                        libspectrum_word address GCC_UNUSED,
                        libspectrum_byte b GCC_UNUSED )
{
}

int ttx2000s_paged;

libspectrum_byte ttx2000s_sram_read( libspectrum_word address GCC_UNUSED ) { return 0xff; }
void ttx2000s_sram_write( libspectrum_word address GCC_UNUSED, libspectrum_byte b GCC_UNUSED ) { }

int uspeech_active, uspeech_available;

libspectrum_byte uspeech_busy( void ) { return 0; }
void uspeech_toggle( void ) { }
void uspeech_write( libspectrum_word address GCC_UNUSED, libspectrum_byte b GCC_UNUSED ) { }
