; ulaplus: load a 64-entry ULAplus palette, draw attributes that use every
; entry, then switch ULAplus on for about 3 s and off for about 3 s, until
; reset.
;
; Palette entry e (0..63) is G3R3B2 = G<<5 | R<<2 | B with
; G = (e >> 3) & 7, R = e & 7, B = (G + R) & 3. All 64 colours differ.
;
; Screen: four bands of six character rows, top to bottom attribute
; palettes 0..3 (attribute bits 7,6). In each band, eight groups of four
; character columns, k = 0..7 left to right. Each group has two cells of
; ink (entry 16*band + k) then two cells of paper (entry 16*band + 8 + k).
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"

        MACRO ATTR_BAND s
tile_idx = 0
        DUP 32
        DB (s << 6) | ((tile_idx >> 2) << 3) | (tile_idx >> 2)
tile_idx = tile_idx + 1
        EDUP
        ENDM

        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a

        ; Pixels: two cells of ink (0xFF) then two of paper (0x00), repeated.
        ld hl,pix_tile
        ld de,0x4000
        ld bc,6144 - 32
        call fill32

        ; Attributes: six rows per band.
        MACRO BAND_FILL tile, dest
        ld hl,tile
        ld de,dest
        ld bc,192 - 32
        call fill32
        ENDM
        BAND_FILL band0, 0x5800
        BAND_FILL band1, 0x5800 + 192
        BAND_FILL band2, 0x5800 + 384
        BAND_FILL band3, 0x5800 + 576

        ; Palette: write entry number to the register port, colour to the data port.
        ld e,0
.pal:   ld bc,PORT_ULP_REG
        ld a,e
        out (c),a
        ld a,e
        rrca
        rrca
        rrca
        and 7
        ld d,a                      ; G
        ld a,e
        and 7
        ld h,a                      ; R
        add a,d
        and 3
        ld l,a                      ; B
        ld a,d
        rlca
        rlca
        rlca
        rlca
        rlca                        ; G << 5
        ld d,a
        ld a,h
        add a,a
        add a,a                     ; R << 2
        or d
        or l
        ld b,0xFF                   ; BC = PORT_ULP_DATA
        out (c),a
        inc e
        ld a,e
        cp 64
        jr nz,.pal

.loop:  ; Mode group (register 0x40), data bit 0 = 1: palette on.
        ld bc,PORT_ULP_REG
        ld a,0x40
        out (c),a
        ld b,0xFF
        ld a,1
        out (c),a
        ld a,6
        call wait_halves
        ; Palette off.
        ld bc,PORT_ULP_REG
        ld a,0x40
        out (c),a
        ld b,0xFF
        xor a
        out (c),a
        ld a,6
        call wait_halves
        jr .loop

pix_tile:
        DUP 8
        DB 0xFF, 0xFF, 0x00, 0x00
        EDUP
band0:  ATTR_BAND 0
band1:  ATTR_BAND 1
band2:  ATTR_BAND 2
band3:  ATTR_BAND 3

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/ulaplus.tap", "ulaplus"
