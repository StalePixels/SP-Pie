; turbosound: tests TurboSound (NedoPC). Writing 0xFF to port 0xFFFD
; selects chip 0 and 0xFE selects chip 1. Different tones on each chip.
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
        DB "TurboSound test",13
        DB "Chip 0 plays 440 Hz.",13
        DB "Chip 1 plays 880 Hz.",13,13,0

steps:
        DB "Silence both chips",13,0
        DB 10
        DB CHIP_SELECT,CHIP1, 7,0x3F, 8,0, 9,0, 10,0
        DB CHIP_SELECT,CHIP0, 7,0x3F, 8,0, 9,0, 10,0
        DB 0xFF

        DB "Chip 0 only: 440 Hz",13,0
        DB 60
        DB 0,252, 1,0, 7,0x3E, 8,15
        DB 0xFF

        DB "Chip 1 only: 880 Hz",13,0
        DB 60
        DB 8,0
        DB CHIP_SELECT,CHIP1, 0,126, 1,0, 7,0x3E, 8,15
        DB 0xFF

        DB "Both chips: 440 Hz and 880 Hz",13,0
        DB 75
        DB CHIP_SELECT,CHIP0, 8,15
        DB 0xFF

        DB "Chip 0 noise, chip 1 880 Hz",13,0
        DB 60
        DB 6,8, 7,0x37
        DB 0xFF

        DB "Chip 1 off, chip 0 noise only",13,0
        DB 50
        DB CHIP_SELECT,CHIP1, 8,0
        DB 0xFF

        DB "Chip 1 only, channel C 1760 Hz",13,0
        DB 60
        DB 7,0x3B, 4,63, 5,0, 10,15
        DB CHIP_SELECT,CHIP0, 8,0
        DB 0xFF

        DB "Done",13,0
        DB 1
        DB CHIP_SELECT,CHIP1, 7,0x3F, 10,0
        DB CHIP_SELECT,CHIP0, 7,0x3F, 8,0
        DB 0xFF
        DB 0

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/turbosound.tap", "build/dot/turbosound", "turbosound"
