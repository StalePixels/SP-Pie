/*
 * fusesound_rename.h
 *
 * Fuse's sound.c (fuse/, pinned) keeps one sound generator in
 * globals. SP-Pie needs several: one per AY chip and one for the ULA and
 * the Covox DACs. Each is a separate compile of the unmodified sound.c,
 * from a wrapper file that defines FUSESOUND_INSTANCE and includes this
 * header before it. The macros below give every external name that
 * sound.c defines or that belongs to one generator a prefix, so the
 * compiles do not share state or clash at link time:
 *
 *   FUSESOUND_INSTANCE = fusesound_ay0  makes  fusesound_ay0_sound_frame
 *
 * machine_current is renamed too: each generator reads its timings and
 * capabilities from a machine description of its own (fusesound.c), not
 * from the machine the display uses. The low-level output functions
 * (sound_lowlevel_*) are renamed, and fusesound.c defines them for each
 * generator. Everything else sound.c calls (the Blip_Buffer, the filters,
 * the settings, the stubs) is shared.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSESOUND_RENAME_H
#define SPPIE_FUSESOUND_RENAME_H

#ifndef FUSESOUND_INSTANCE
#error "define FUSESOUND_INSTANCE before including fusesound_rename.h"
#endif

#define FUSESOUND_PASTE2( prefix, name ) prefix##_##name
#define FUSESOUND_PASTE( prefix, name ) FUSESOUND_PASTE2( prefix, name )
#define FUSESOUND_NAME( name ) FUSESOUND_PASTE( FUSESOUND_INSTANCE, name )

/* Variables sound.c defines */
#define sound_enabled			FUSESOUND_NAME( sound_enabled )
#define sound_stereo_ay			FUSESOUND_NAME( sound_stereo_ay )
#define sound_framesiz			FUSESOUND_NAME( sound_framesiz )
#define left_buf			FUSESOUND_NAME( left_buf )
#define right_buf			FUSESOUND_NAME( right_buf )
#define samples				FUSESOUND_NAME( samples )
#define ay_a_synth			FUSESOUND_NAME( ay_a_synth )
#define ay_b_synth			FUSESOUND_NAME( ay_b_synth )
#define ay_c_synth			FUSESOUND_NAME( ay_c_synth )
#define ay_a_synth_r			FUSESOUND_NAME( ay_a_synth_r )
#define ay_b_synth_r			FUSESOUND_NAME( ay_b_synth_r )
#define ay_c_synth_r			FUSESOUND_NAME( ay_c_synth_r )
#define left_specdrum_synth		FUSESOUND_NAME( left_specdrum_synth )
#define right_specdrum_synth		FUSESOUND_NAME( right_specdrum_synth )
#define left_covox_synth		FUSESOUND_NAME( left_covox_synth )
#define right_covox_synth		FUSESOUND_NAME( right_covox_synth )
#define left_sp0256_synth		FUSESOUND_NAME( left_sp0256_synth )
#define right_sp0256_synth		FUSESOUND_NAME( right_sp0256_synth )

/* Functions sound.c defines */
#define sound_get_effective_processor_speed FUSESOUND_NAME( sound_get_effective_processor_speed )
#define sound_init			FUSESOUND_NAME( sound_init )
#define sound_pause			FUSESOUND_NAME( sound_pause )
#define sound_unpause			FUSESOUND_NAME( sound_unpause )
#define sound_end			FUSESOUND_NAME( sound_end )
#define sound_register_startup		FUSESOUND_NAME( sound_register_startup )
#define sound_ay_write			FUSESOUND_NAME( sound_ay_write )
#define sound_ay_reset			FUSESOUND_NAME( sound_ay_reset )
#define sound_specdrum_write		FUSESOUND_NAME( sound_specdrum_write )
#define sound_covox_write		FUSESOUND_NAME( sound_covox_write )
#define sound_sp0256_write		FUSESOUND_NAME( sound_sp0256_write )
#define sound_frame			FUSESOUND_NAME( sound_frame )
#define sound_ula_levels		FUSESOUND_NAME( sound_ula_levels )
#define sound_ula			FUSESOUND_NAME( sound_ula )
#define sound_tape			FUSESOUND_NAME( sound_tape )
#define sound_ula_mic_output		FUSESOUND_NAME( sound_ula_mic_output )
#define sound_ula_beeper_output		FUSESOUND_NAME( sound_ula_beeper_output )
#define sound_ula_mic_output_count	FUSESOUND_NAME( sound_ula_mic_output_count )
#define sound_ula_beeper_output_count	FUSESOUND_NAME( sound_ula_beeper_output_count )

/* Per generator, defined in fusesound.c */
#define sound_lowlevel_init		FUSESOUND_NAME( sound_lowlevel_init )
#define sound_lowlevel_end		FUSESOUND_NAME( sound_lowlevel_end )
#define sound_lowlevel_frame		FUSESOUND_NAME( sound_lowlevel_frame )
#define machine_current			FUSESOUND_NAME( machine_current )

#endif
