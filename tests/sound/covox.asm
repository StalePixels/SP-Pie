; covox: tests the Covox DACs on ports 0xFB, 0xDD and 0xDF.
; Each port plays a 32-sample sine wave. The samples are 8-bit unsigned
; (0x80 is the centre). The three ports use different sample delays, so
; the waves have different pitches. For each port the wave plays twice:
; first with that Covox disabled in the control port (silent), then with it
; enabled (audible). Only the Covox under test is enabled, and AY,
; TurboSound and the beeper are off while it plays.
;
; Time for one sample is 53 + 13 * N T-states, so one cycle of the wave
; is 32 * (53 + 13 * N) + 31 T-states.
;
; Port 0xDD is also decoded by the 128K machine: with address line A15 low
; it is the memory paging port, and with A15 high it is the AY. The wave
; for 0xDD therefore uses the port address 0xBFDD (the AY data port) and
; first selects AY register 15, which has no function on an AY-3-8912, so
; the samples change nothing.
        INCLUDE "sound.inc"
        SND_BEGIN
        call snd_init
        ld hl,txt_title
        call print_str

        ld hl,txt_fb
        call print_str
        FEAT FEAT_DEVICES, 0
        ld bc,PORT_COVOX_FB
        ld a,5                          ; N = 5, about 919 Hz
        ld de,1378
        call wave
        ld hl,txt_on
        call print_str
        FEAT FEAT_DEVICES, FEAT_COVOX_FB
        ld bc,PORT_COVOX_FB
        ld a,5
        ld de,1378
        call wave

        ld hl,txt_dd
        call print_str
        FEAT FEAT_DEVICES, 0
        AYW 15, 0
        ld bc,PORT_COVOX_DD_AYDATA
        ld a,10                         ; N = 10, about 595 Hz
        ld de,892
        call wave
        ld hl,txt_on
        call print_str
        FEAT FEAT_DEVICES, FEAT_COVOX_DD
        ld bc,PORT_COVOX_DD_AYDATA
        ld a,10
        ld de,892
        call wave

        ld hl,txt_df
        call print_str
        FEAT FEAT_DEVICES, 0
        ld bc,PORT_COVOX_DF
        ld a,2                          ; N = 2, about 1368 Hz
        ld de,2052
        call wave
        ld hl,txt_on
        call print_str
        FEAT FEAT_DEVICES, FEAT_COVOX_DF
        ld bc,PORT_COVOX_DF
        ld a,2
        ld de,2052
        call wave

        ; Leave the DAC at the centre value.
        ld a,0x80
        ld bc,PORT_COVOX_FB
        out (c),a
        ld bc,PORT_COVOX_DD_AYDATA
        out (c),a
        ld bc,PORT_COVOX_DF
        out (c),a
        FEAT_DEFAULTS
        ld hl,txt_done
        call print_str
        SND_RETURN

txt_title:
        DB "Covox test",13,13,0
txt_fb: DB "Port 0xFB, about 919 Hz",13
        DB " Covox disabled, silent",13,0
txt_dd: DB "Port 0xDD, about 595 Hz",13
        DB " Covox disabled, silent",13,0
txt_df: DB "Port 0xDF, about 1368 Hz",13
        DB " Covox disabled, silent",13,0
txt_on: DB " Covox enabled, sine wave",13,0
txt_done:
        DB "Done",13,0

; Sine wave, 32 samples, 8-bit unsigned: 128 + 120 * sin(2 * pi * i / 32)
; The table sits at a multiple of 64, so L reaches 0x20 after the last sample.
        ALIGN 64
sine:   DB 128,152,175,196,214,228,237,242
        DB 243,242,237,228,214,196,175,152
        DB 128,104, 81, 60, 42, 28, 19, 14
        DB  13, 14, 19, 28, 42, 60, 81,104

; Play DE cycles to port BC. Delay count A per sample.
; The delay loop counts in B', because B is the high byte of the port
; address and must stay fixed. Interrupts are off while it runs and on again
; at the end.
wave:   di
        ld (.n+1),a
        exx
        push bc
        exx
.cyc:   ld hl,sine                      ; 10
.smp:   ld a,(hl)                       ; 7
        out (c),a                       ; 12
        inc l                           ; 4
        exx                             ; 4
.n:     ld b,0                          ; 7, count patched above
.d:     djnz .d                         ; 13 / 8
        exx                             ; 4
        bit 5,l                         ; 8
        jr z,.smp                       ; 12 / 7
        dec de
        ld a,d
        or e
        jr nz,.cyc
        exx
        pop bc
        exx
        ei
        ret

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/covox.tap", "build/dot/covox", "covox"
