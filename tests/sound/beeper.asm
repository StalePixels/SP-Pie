; beeper: tests the beeper (port 0xFE bit 4). Three square waves.
; One period is 26 * N + 63 T-states, where N is the delay count of each
; half. The CPU clock is 3.5 MHz on a 48K and 3.5469 MHz on a 128K.
        INCLUDE "sound.inc"
        SND_BEGIN
        call snd_init
        FEAT_DEFAULTS
        ld hl,txt_title
        call print_str

        ld hl,txt_a
        call print_str
        ld a,255                        ; N = 255, about 523 Hz
        ld de,785                       ; 1.5 seconds
        call beep
        ld a,25
        call wait_frames

        ld hl,txt_b
        call print_str
        ld a,133                        ; N = 133, about 994 Hz
        ld de,1491
        call beep
        ld a,25
        call wait_frames

        ld hl,txt_c
        call print_str
        ld a,64                         ; N = 64, about 2026 Hz
        ld de,3039
        call beep
        ld a,25
        call wait_frames

        ld hl,txt_done
        call print_str
        SND_RETURN

txt_title:
        DB "Beeper test",13,13,0
txt_a:  DB "Square wave about 523 Hz, 1.5 s",13,0
txt_b:  DB "Square wave about 994 Hz, 1.5 s",13,0
txt_c:  DB "Square wave about 2026 Hz, 1.5 s",13,0
txt_done:
        DB "Done",13,0

; Play DE periods, delay count A per half period. Interrupts are off
; while it runs and on again at the end.
beep:   di
        ld c,a
.per:   ld a,0x10                       ; 7
        out (PORT_ULA),a                ; 11
        ld b,c                          ; 4
.h1:    djnz .h1                        ; 13 / 8
        xor a                           ; 4
        out (PORT_ULA),a                ; 11
        ld b,c                          ; 4
.h2:    djnz .h2
        dec de                          ; 6
        ld a,d                          ; 4
        or e                            ; 4
        jr nz,.per                      ; 12
        ei
        ret

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/beeper.tap", "build/dot/beeper", "beeper"
