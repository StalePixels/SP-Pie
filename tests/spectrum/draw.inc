; Shared drawing routines and picture data.
; Include after the program code, before the prog_end label.
; All routines take the screen base in HL (0x4000 for the normal map, 0xC000
; when a screen bank is paged there). They use LDIR and plain stores.

; fill32: copy a 32-byte tile to DE, then repeat it by copying forward.
;   HL = tile, DE = destination, BC = total length - 32
fill32:
        push de
        push bc
        ld bc,32
        ldir
        pop bc
        pop hl
        ldir
        ret

; clear_screen: pixels 0, attributes 0x38 (black ink on white paper).
clear_screen:
        push hl
        ld (hl),0
        ld d,h
        ld e,l
        inc de
        ld bc,6143
        ldir
        pop hl
        ld de,0x1800
        add hl,de
        ld (hl),0x38
        ld d,h
        ld e,l
        inc de
        ld bc,767
        ldir
        ret

; draw_pic_cols: vertical bars. Pixel bytes alternate 0xAA / 0xF0 across
; the row; attribute colour changes every column (see attr_tile_a).
draw_pic_cols:
        push hl
        ex de,hl
        ld hl,pix_tile_a
        ld bc,6144 - 32
        call fill32
        pop de
        ld hl,0x1800
        add hl,de
        ex de,hl
        ld hl,attr_tile_a
        ld bc,768 - 32
        jp fill32

; draw_pic_rows: horizontal bands. Pixel bytes alternate 0x3C / 0xC3; the
; attribute colour changes every character row (see attr_rows).
draw_pic_rows:
        push hl
        ex de,hl
        ld hl,pix_tile_b
        ld bc,6144 - 32
        call fill32
        pop hl
        ld de,0x1800
        add hl,de
        ex de,hl
        ld c,0
; attr_rows: fill attribute rows from row C to row 23.
;   DE = address of attribute row C. Row r gets paper = r & 7,
;   ink = 7 - (r & 7), BRIGHT when bit 3 of r is set.
attr_rows:
.row:   ld a,c
        and 7
        ld b,a
        add a,a
        add a,a
        add a,a
        ld h,a
        ld a,7
        sub b
        or h
        ld h,a
        ld a,c
        and 8
        rlca
        rlca
        rlca
        or h
        ld b,32
.cell:  ld (de),a
        inc de
        djnz .cell
        inc c
        ld a,c
        cp 24
        jr nz,.row
        ret

; wait_halves: A = number of waits of about 0.5 s (interrupts may be off).
wait_halves:
        push af
        ld bc,0xFFFF
.spin:  dec bc              ; 6
        ld a,b              ; 4
        or c                ; 4
        jr nz,.spin         ; 12
        pop af
        dec a
        jr nz,wait_halves
        ret

pix_tile_a:
        DUP 16
        DB 0xAA, 0xF0
        EDUP
pix_tile_b:
        DUP 16
        DB 0x3C, 0xC3
        EDUP
; paper = i & 7, ink = 7 - (i & 7), BRIGHT when bit 3 of i is set.
attr_tile_a:
tile_idx = 0
        DUP 32
        DB ((tile_idx & 7) << 3) | (7 - (tile_idx & 7)) | ((tile_idx & 8) << 3)
tile_idx = tile_idx + 1
        EDUP
