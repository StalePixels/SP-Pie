; bank5_c000: page RAM bank 5 at 0xC000 and draw the screen through
; 0xC000. Bank 5 is also the normal screen at 0x4000, so the picture
; changes on the normal screen. Runs until reset:
; clear through 0x4000, then alternate the bands picture and the bars
; picture through 0xC000, about 2 s each.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"
        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a
        OUT7FFD P7FFD_ROM | 5       ; bank 5 at 0xC000, normal screen
        ld hl,0x4000
        call clear_screen
.loop:  ld hl,0xC000
        call draw_pic_rows
        ld a,4
        call wait_halves
        ld hl,0xC000
        call draw_pic_cols
        ld a,4
        call wait_halves
        jr .loop

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/bank5_c000.tap", "bank5_c000"
