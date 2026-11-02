; shadow_flip: draw one picture in bank 5 (the normal screen, via 0x4000)
; and another in bank 7 (the shadow screen, via 0xC000), then flip
; port 0x7FFD bit 3 every second until reset.
; Bank 5: vertical bars. Bank 7: horizontal bands.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"
        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a
        OUT7FFD P7FFD_ROM | 0       ; bank 0 at 0xC000, normal screen
        ld hl,0x4000
        call draw_pic_cols
        OUT7FFD P7FFD_ROM | 7       ; bank 7 at 0xC000
        ld hl,0xC000
        call draw_pic_rows
.loop:  OUT7FFD P7FFD_ROM | P7FFD_SHADOW | 7    ; show bank 7
        ld a,2
        call wait_halves
        OUT7FFD P7FFD_ROM | 7                    ; show bank 5
        ld a,2
        call wait_halves
        jr .loop

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/shadow_flip.tap", "shadowflip"
