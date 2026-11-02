; border_loop: tight OUT (0xFE) loop cycling the border colour.
; Bits 3 and 4 of every value written are 0, so MIC and the speaker stay
; off. Repeats until reset:
;   fast phase  - OUT every 56 T-states for about 1 s (colours blur into
;                 stripes at the screen edge);
;   slow phase  - each colour 0..7 held for about 0.5 s.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"
        ORG CODE_ORG
start:  di
        ld sp,STACK
.loop:  xor a
        ld de,0                     ; counts 65536 times
.fast:  out (PORT_ULA),a            ; 11
        inc a                       ; 4
        and 7                       ; 7
        dec de                      ; 6
        ld b,a                      ; 4
        ld a,d                      ; 4
        or e                        ; 4
        ld a,b                      ; 4
        jr nz,.fast                 ; 12
        ld c,0
.slow:  ld a,c
        out (PORT_ULA),a
        ld a,1
        call wait_halves
        inc c
        ld a,c
        cp 8
        jr nz,.slow
        jr .loop

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/border_loop.tap", "border_lp"
