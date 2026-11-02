; border: draws eight horizontal border colour stripes, then switches the
; border extension (feature 2, bit 0) off and on. Three rounds of four
; seconds off and four seconds on. The extension is off again at the end.
;
; The program waits for the frame interrupt and then writes port 0xFE eight
; times, each time followed by a delay of about 33 lines (7392 T-states). The
; stripes are therefore not exact, and the 128K, whose lines are slightly
; longer, shows them a little lower.
        INCLUDE "sound.inc"
        SND_BEGIN
        call snd_init
        FEAT_DEFAULTS
        ld hl,txt_title
        call print_str
        ld a,3
        ld (rounds),a
.round: ld hl,txt_off
        call print_str
        FEAT FEAT_BORDER, 0
        ld a,200
        call stripe_frames
        ld hl,txt_on
        call print_str
        FEAT FEAT_BORDER, 1
        ld a,200
        call stripe_frames
        ld a,(rounds)
        dec a
        ld (rounds),a
        jr nz,.round
        FEAT FEAT_BORDER, 0
        ld a,7
        out (PORT_ULA),a
        ld hl,txt_done
        call print_str
        SND_RETURN

txt_title:
        DB "Border test",13
        DB "Eight stripes, top to bottom:",13
        DB "black blue red magenta",13
        DB "green cyan yellow white",13,13,0
txt_off:
        DB "Extension off",13,0
txt_on: DB "Extension on, stripes fill 16:9",13,0
txt_done:
        DB "Done",13,0

rounds: DB 0
count:  DB 0

; Draw the stripes for A frames.
stripe_frames:
        ld (count),a
.frame: ei
        halt
        ld c,0
.stripe:
        ld a,c
        out (PORT_ULA),a
        ld de,280
.delay: dec de                          ; 6
        ld a,d                          ; 4
        or e                            ; 4
        jr nz,.delay                    ; 12
        inc c
        ld a,c
        cp 8
        jr nz,.stripe
        ld a,(count)
        dec a
        ld (count),a
        jr nz,.frame
        ret

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/border.tap", "build/dot/border", "border"
