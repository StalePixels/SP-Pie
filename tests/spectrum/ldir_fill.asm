; ldir_fill: fill the whole screen (pixels and attributes) with LDIR,
; alternating two pictures about every second. Runs until reset.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"
        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a            ; black border
.loop:  ld hl,0x4000
        call draw_pic_cols
        ld a,2
        call wait_halves
        ld hl,0x4000
        call draw_pic_rows
        ld a,2
        call wait_halves
        jr .loop

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/ldir_fill.tap", "ldir_fill"
