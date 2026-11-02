; hud: switches the HUD off and on through feature 0xFF
; (debug, bit 0). Three rounds of two seconds
; off and two seconds on. The HUD is on again at the end.
        INCLUDE "sound.inc"
        SND_BEGIN
        call snd_init
        FEAT_DEFAULTS
        ld hl,txt_title
        call print_str
        ld d,3
.round: push de
        ld hl,txt_off
        call print_str
        FEAT FEAT_DEBUG, 0
        ld a,100
        call wait_frames
        ld hl,txt_on
        call print_str
        FEAT FEAT_DEBUG, 1
        ld a,100
        call wait_frames
        pop de
        dec d
        jr nz,.round
        ld hl,txt_done
        call print_str
        SND_RETURN

txt_title:
        DB "HUD test",13,13,0
txt_off:
        DB "HUD off",13,0
txt_on: DB "HUD on",13,0
txt_done:
        DB "Done. The HUD is on.",13,0

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/hud.tap", "build/dot/hud", "hud"
