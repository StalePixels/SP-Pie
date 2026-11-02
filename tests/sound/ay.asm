; ay: tests the AY chip. Tone on each channel at a known period, then all
; three, volume steps, noise, tone with noise, and the envelope shapes.
; Tone frequency = AY clock / (16 * period). The 128K AY clock is
; 1.7734475 MHz.
        INCLUDE "sound.inc"
        SND_BEGIN
        call snd_init
        CTL_DEFAULTS
        ld hl,txt_title
        call print_str
        ld hl,steps
        call run_steps
        CTL_DEFAULTS
        SND_RETURN

txt_title:
        DB "AY test",13
        DB "Each line is one sound.",13,13,0

; AY register numbers
; 0,1 period A   2,3 period B   4,5 period C   6 noise   7 mixer
; 8,9,10 volume  11,12 envelope period   13 envelope shape
; Mixer bits: 0 to 2 tone A, B, C off; 3 to 5 noise A, B, C off.

steps:
        DB "Select chip 0, silence",13,0
        DB 10
        DB CHIP_SELECT,CHIP0
        DB 7,0x3F, 8,0, 9,0, 10,0
        DB 0xFF

        DB "A 440 Hz, period 252",13,0
        DB 50
        DB 0,252, 1,0, 7,0x3E, 8,15
        DB 0xFF

        DB "B 880 Hz, period 126",13,0
        DB 50
        DB 8,0, 2,126, 3,0, 7,0x3D, 9,15
        DB 0xFF

        DB "C 1760 Hz, period 63",13,0
        DB 50
        DB 9,0, 4,63, 5,0, 7,0x3B, 10,15
        DB 0xFF

        DB "A, B and C together",13,0
        DB 75
        DB 7,0x38, 8,15, 9,15, 10,15
        DB 0xFF

        DB "A 440 Hz at volume 15",13,0
        DB 20
        DB 7,0x3E, 8,15, 9,0, 10,0
        DB 0xFF

        DB "A at volume 11",13,0
        DB 20
        DB 8,11
        DB 0xFF

        DB "A at volume 7",13,0
        DB 20
        DB 8,7
        DB 0xFF

        DB "A at volume 3",13,0
        DB 20
        DB 8,3
        DB 0xFF

        DB "Noise period 4 (high hiss)",13,0
        DB 50
        DB 6,4, 7,0x37, 8,15
        DB 0xFF

        DB "Noise period 20 (low rumble)",13,0
        DB 50
        DB 6,20
        DB 0xFF

        DB "A tone 440 Hz plus noise",13,0
        DB 50
        DB 6,8, 7,0x36
        DB 0xFF

        DB "Env 8: saw down, repeating",13,0
        DB 75
        DB 7,0x3E, 8,0x10, 11,0xDC, 12,0x05, 13,8
        DB 0xFF

        DB "Env 10: triangle, starts loud",13,0
        DB 75
        DB 13,10
        DB 0xFF

        DB "Env 12: saw up, repeating",13,0
        DB 75
        DB 13,12
        DB 0xFF

        DB "Env 14: triangle, starts quiet",13,0
        DB 75
        DB 13,14
        DB 0xFF

        DB "Env 0: one decay, then silent",13,0
        DB 75
        DB 13,0
        DB 0xFF

        DB "Env 4: one rise, then silent",13,0
        DB 75
        DB 13,4
        DB 0xFF

        DB "Env 11: decay, then hold loud",13,0
        DB 75
        DB 13,11
        DB 0xFF

        DB "Mixer: tone and noise both off",13,0
        DB 50
        DB 7,0x3F, 8,15
        DB 0xFF

        DB "Done",13,0
        DB 1
        DB 8,0, 9,0, 10,0
        DB 0xFF
        DB 0

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/ay.tap", "build/dot/ay", "ay"
