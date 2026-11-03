; demo: a Gigascreen and ULAplus demonstration for the 128K, +2, +2A and +3.
; Tape build only. The program draws two pictures with its own code. Each
; picture exists in bank 5 (the normal screen at 0x4000) and bank 7 (the
; shadow screen, paged in at 0xC000 for the whole run).
;
; Picture 0, Gigascreen: the top eight rows are a colour chart. Each swatch has
; one colour in bank 5 and another in bank 7. Below it, diagonal lines whose
; screen lines are shared between the banks: picture line 2n is line n of
; bank 5 and picture line 2n+1 is line n of bank 7. Shown double high, the
; lines read as one clean 45 degree pattern of 256 x 256 pixels. Each bank
; alone shows only half of the lines.
; Picture 1, ULAplus: four bands, one for each colour table of the 64-colour
; palette. With ULAplus off the bands show the 8 standard colours. With it on,
; each band is a 16-step colour ramp.
;
; The program only writes to ports. Ports written:
;   0x7FFD   paging: bank 7 at 0xC000, ROM bit kept, screen bit 3 for the flip
;   0x043B, 0x053B   SP-Pie feature 3 (display), value in DISP_*
;   0xBF3B, 0xFF3B   ULAplus register and data: palette entries, and group
;                    0x40 with bit 0 as palette on. No other ULAplus mode.
;   0xFE     border
; All writes use A15 = 1 except 0x7FFD (wanted), and the 0x043B and 0x053B
; writes, whose A1 = 1 keeps them out of the 0x7FFD decode.
;
; Keys (the ROM keyboard interrupt supplies them):
;   1 primary   2 secondary   3 flip on/off   4 Gigascreen   5 show as
;   6 double-high top   7 ULAplus   8 picture   M menu   Q quit
        INCLUDE "../sound/sound.inc"

SYS_FLAGS       EQU 0x5C3B
SYS_LAST_K      EQU 0x5C08
SYS_BORDCR      EQU 0x5C48
SYS_BANKM       EQU 0x5B5C

PORT_PAGE       EQU 0x7FFD
PORT_ULA_REG    EQU 0xBF3B
PORT_ULA_DATA   EQU 0xFF3B
FEAT_DISPLAY    EQU 3

; Feature 3 bits (docs/control-ports.md). Bits 5-4 stay 01, detection per page.
DISP_AUTO       EQU 0x01
DISP_ALWAYS     EQU 0x02
DISP_PAGE       EQU 0x10
DISP_BANK7      EQU 0x40
DISP_DEFAULT    EQU 0x11

MENU_ROW        EQU 18
ATTR_MENU       EQU 0x38                ; black ink on white paper
ATTR_TITLE      EQU 0x07                ; white ink on black paper

        SND_BEGIN
        di                              ; bank 7 is written only with interrupts off
        ld a,(SYS_BANKM)
        and 0x10                        ; keep the ROM bit, bank 7, screen 0
        or 7
        ld (v_page),a
        ld bc,PORT_PAGE
        out (c),a
        xor a
        out (PORT_ULA),a                ; black border
        call redraw_all
        call load_palette
        call ula_send
        call disp_send
        ei

main:   halt
        ld a,(v_flip)
        or a
        jr z,.fixed
        ld a,(v_shown)
        xor 1
        jr .set
.fixed: ld a,(v_bank)
.set:   ld (v_shown),a
        call show_bank
        ld a,(SYS_FLAGS)
        bit 5,a
        jr z,main                       ; no new key
        res 5,a
        ld (SYS_FLAGS),a
        ld a,(SYS_LAST_K)
        or 0x20                         ; letters to lower case
        cp '1'
        jr z,k_primary
        cp '2'
        jr z,k_second
        cp '3'
        jr z,k_flip
        cp '4'
        jr z,k_giga
        cp '5'
        jr z,k_mode
        cp '6'
        jr z,k_top
        cp '7'
        jr z,k_ula
        cp '8'
        jr z,k_pic
        cp 'm'
        jr z,k_menu
        cp 'q'
        jp z,quit
        jp main

k_primary:
        xor a
        ld (v_flip),a
        ld (v_bank),a
        jr changed
k_second:
        xor a
        ld (v_flip),a
        inc a
        ld (v_bank),a
        jr changed
k_flip: ld a,(v_flip)
        xor 1
        ld (v_flip),a
        jr changed
k_giga: ld hl,v_giga
        ld b,3
        call step
        call disp_send
        jr changed
k_mode: ld hl,v_mode
        ld b,3
        call step
        call disp_send
        jr changed
k_top:  ld hl,v_top
        ld b,2
        call step
        call disp_send
        jr changed
k_ula:  ld hl,v_ula
        ld b,2
        call step
        call ula_send
        jr changed
k_pic:  ld hl,v_pic
        ld b,2
        call step
        call redraw_all
        call load_palette
        call ula_send
        jp main
k_menu: ld a,(v_menu)
        xor 1
        ld (v_menu),a
        call redraw_all
        jp main
changed:
        ld a,(v_menu)
        or a
        call nz,draw_menu
        jp main

; Add 1 to the variable at HL, modulo B.
step:   ld a,(hl)
        inc a
        cp b
        jr c,.ok
        xor a
.ok:    ld (hl),a
        ret

; Leave: switch ULAplus off, feature 3 to its default, clear the shadow screen,
; restore the paging and the border, clear the screen and return to BASIC.
quit:   di
        xor a
        ld (v_ula),a
        call ula_send
        ld a,DISP_DEFAULT
        call feat_display
        ld hl,0xC000
        ld de,0xC001
        ld bc,0x17FF
        ld (hl),l
        ldir
        ld hl,0xD800
        ld de,0xD801
        ld bc,0x02FF
        ld (hl),ATTR_MENU
        ldir
        ld a,(SYS_BANKM)
        ld bc,PORT_PAGE
        out (c),a
        ld a,(SYS_BORDCR)
        rrca
        rrca
        rrca
        and 7
        out (PORT_ULA),a
        call ROM_CLS
        ei
        ret

; Show bank A (0 or 1): write the paging port with the screen bit.
show_bank:
        rlca
        rlca
        rlca                            ; bit 0 to bit 3
        ld hl,v_page
        or (hl)
        ld bc,PORT_PAGE
        out (c),a
        ret

; Send feature 3 from the menu variables.
disp_send:
        ld a,(v_giga)                   ; 0 off, 1 auto (bit 0), 2 always (bit 1)
        ld b,a
        ld a,(v_mode)                   ; 0 50/50, 1 CRT, 2 double high
        add a,a
        add a,a
        or b
        or DISP_PAGE
        ld b,a
        ld a,(v_top)
        or a
        ld a,b
        jr z,feat_display
        or DISP_BANK7
feat_display:
        push af
        ld bc,PORT_FEAT_SEL
        ld a,FEAT_DISPLAY
        out (c),a
        ld bc,PORT_FEAT_DATA
        pop af
        out (c),a
        ret

; ULAplus group 0x40: bit 0 = palette on (v_ula).
ula_send:
        ld bc,PORT_ULA_REG
        ld a,0x40
        out (c),a
        ld bc,PORT_ULA_DATA
        ld a,(v_ula)
        out (c),a
        ret

; Write the 64 palette entries of the current picture.
load_palette:
        ld hl,pal_std
        ld a,(v_pic)
        or a
        jr z,.go
        ld hl,pal_ramps
.go:    ld d,0
.l:     ld bc,PORT_ULA_REG
        out (c),d
        ld bc,PORT_ULA_DATA
        ld a,(hl)
        inc hl
        out (c),a
        inc d
        ld a,d
        cp 64
        jr nz,.l
        ret

; Clear both banks, draw the current picture, then the menu. Interrupts are
; off while bank 7 is written: on the +3 an interrupt in the middle of the
; drawing lost part of the shadow screen.
redraw_all:
        di
        ld hl,0x4000
        ld de,0x4001
        ld bc,0x17FF
        ld (hl),l
        ldir
        ld hl,0xC000
        ld de,0xC001
        ld bc,0x17FF
        ld (hl),l
        ldir
        ld hl,0x5800
        ld de,0x5801
        ld bc,0x02FF
        ld (hl),ATTR_TITLE
        ldir
        ld hl,0xD800
        ld de,0xD801
        ld bc,0x02FF
        ld (hl),ATTR_TITLE
        ldir
        ld a,(v_pic)
        or a
        call z,draw_pic0
        ld a,(v_pic)
        or a
        call nz,draw_pic1
        ld a,(v_menu)
        or a
        call nz,menu_body
        ei
        ret

; Next pixel line down: HL in the 0x4000 screen.
down_hl:
        inc h
        ld a,h
        and 7
        ret nz
        ld a,l
        add a,0x20
        ld l,a
        ret c
        ld a,h
        sub 8
        ld h,a
        ret

; ---- Picture 0 ----
draw_pic0:
        ; Colour chart: row a, swatch b of two cells. Bank 5 paper = a. Bank 7
        ; paper = b mod 8, bright when b >= 8.
        ld hl,0x5800
        ld d,0
.a:     ld e,0
.b:     ld a,e
        and 7
        add a,a
        add a,a
        add a,a
        ld c,a
        ld a,e
        and 8
        rlca
        rlca
        rlca
        or c
        ld c,a                          ; bank 7 attribute
        ld a,d
        add a,a
        add a,a
        add a,a                         ; bank 5 attribute
        ld (hl),a
        inc hl
        ld (hl),a
        set 7,h
        ld (hl),c
        dec hl
        ld (hl),c
        res 7,h
        inc hl
        inc hl
        inc e
        ld a,e
        cp 16
        jr nz,.b
        inc d
        ld a,d
        cp 8
        jr nz,.a
        ; Lines. Attributes of rows 8 to 23: bright white on blue.
        ld hl,0x5900
        ld de,0x5901
        ld bc,0x01FF
        ld (hl),0x4F
        ldir
        ld hl,0xD900
        ld de,0xD901
        ld bc,0x01FF
        ld (hl),0x4F
        ldir
        ; Pixels: 128 screen lines of each bank from row 8.
        ld hl,0x4800
        xor a
        ld (v_n),a
.row:   push hl
        ld a,(v_n)
        add a,a                         ; picture line 2n
        and 7
        call fill_line
        pop hl
        push hl
        set 7,h
        ld a,(v_n)
        add a,a
        inc a                           ; picture line 2n+1
        and 7
        call fill_line
        pop hl
        call down_hl
        ld a,(v_n)
        inc a
        ld (v_n),a
        cp 128
        jr nz,.row
        ret

; One screen line at HL for picture line A (mod 8): blocks of 4 bytes
; alternate between the two slopes.
fill_line:
        push hl
        ld e,a
        ld d,0
        ld hl,tab_minus
        add hl,de
        ld c,(hl)
        ld hl,tab_plus
        add hl,de
        ld d,(hl)
        ld e,c
        pop hl
        ld b,4
.l:     DUP 4
        ld (hl),d
        inc hl
        EDUP
        DUP 4
        ld (hl),e
        inc hl
        EDUP
        djnz .l
        ret

; ---- Picture 1 ----
; Attribute of cell (row, x): CLUT = row / 4 in bits 7-6, ink and paper =
; x / 4. Pixels: two cells of ink, then two cells of paper, so each group of
; four cells shows ink g and paper g.
draw_pic1:
        ld hl,0x5800
        xor a
        ld (v_n),a                      ; row 0 to 15
.rw:    ld a,(v_n)
        rrca
        rrca
        and 3                           ; row / 4 ...
        rrca
        rrca                            ; ... to bits 7-6
        ld d,a
        ld e,0                          ; g
.g:     ld a,e
        add a,a
        add a,a
        add a,a
        add a,e                         ; g * 9: ink g and paper g
        or d
        ld b,4
.c:     ld (hl),a
        set 7,h
        ld (hl),a
        res 7,h
        inc hl
        djnz .c
        inc e
        ld a,e
        cp 8
        jr nz,.g
        ld a,(v_n)
        inc a
        ld (v_n),a
        cp 16
        jr nz,.rw
        ld hl,0x4000
        ld a,128
        ld (v_n),a
.ln:    push hl
        call fill_p1
        pop hl
        push hl
        set 7,h
        call fill_p1
        pop hl
        call down_hl
        ld a,(v_n)
        dec a
        ld (v_n),a
        jr nz,.ln
        ret

fill_p1:
        ld b,8
.l:     ld (hl),0xFF
        inc hl
        ld (hl),0xFF
        inc hl
        ld (hl),0
        inc hl
        ld (hl),0
        inc hl
        djnz .l
        ret

; ---- Menu ----
; Print the zero-terminated string at HL at row B, column C, with the
; attribute in v_attr, into both banks. Characters come from the ROM font at
; 0x3D00 (character 32), which is why the 48K ROM must be paged in.
puts:
        ld a,b
        and 0x18
        or 0x40
        ld d,a
        ld a,b
        and 7
        rrca
        rrca
        rrca
        add a,c
        ld e,a                          ; DE = pixel address of the cell
        push hl
        ld h,0
        ld l,b
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,c
        add a,l
        ld l,a
        ld a,h
        add a,0x58
        ld h,a
        ld (v_aptr),hl
        pop hl
.ch:    ld a,(hl)
        inc hl
        or a
        ret z
        push hl
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,0x3C00
        add hl,bc
        ld b,8
.ln:    ld a,(hl)
        ld (de),a
        set 7,d
        ld (de),a
        res 7,d
        inc hl
        inc d
        djnz .ln
        ld a,d
        sub 8
        ld d,a
        inc e
        ld hl,(v_aptr)
        ld a,(v_attr)
        ld (hl),a
        set 7,h
        ld (hl),a
        res 7,h
        inc hl
        ld (v_aptr),hl
        pop hl
        jr .ch

; Print item A of the pointer table at HL, at row B, column C.
put_choice:
        add a,a
        ld e,a
        ld d,0
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        jr puts

draw_menu:
        di
        call menu_body
        ei
        ret

menu_body:
        ld a,ATTR_TITLE
        ld (v_attr),a
        ld hl,s_title
        ld bc,MENU_ROW*256
        call puts
        ld hl,0x5A40+28                 ; rainbow at the right end of the bar
        ld de,rainbow
        ld b,4
.rb:    ld a,(de)
        inc de
        ld (hl),a
        set 7,h
        ld (hl),a
        res 7,h
        inc hl
        djnz .rb
        ld a,ATTR_MENU
        ld (v_attr),a
        ld hl,s_keys1
        ld bc,(MENU_ROW+1)*256
        call puts
        ld hl,s_keys2
        ld bc,(MENU_ROW+2)*256
        call puts
        ld hl,s_keys3
        ld bc,(MENU_ROW+3)*256
        call puts
        ld hl,s_state1
        ld bc,(MENU_ROW+4)*256
        call puts
        ld hl,s_state2
        ld bc,(MENU_ROW+5)*256
        call puts
        ld a,(v_flip)
        or a
        ld a,2
        jr nz,.v
        ld a,(v_bank)
.v:     ld hl,t_view
        ld bc,(MENU_ROW+4)*256+5
        call put_choice
        ld a,(v_ula)
        ld hl,t_onoff
        ld bc,(MENU_ROW+4)*256+20
        call put_choice
        ld a,(v_pic)
        ld hl,t_pic
        ld bc,(MENU_ROW+4)*256+28
        call put_choice
        ld a,(v_giga)
        ld hl,t_giga
        ld bc,(MENU_ROW+5)*256+5
        call put_choice
        ld a,(v_mode)
        ld hl,t_mode
        ld bc,(MENU_ROW+5)*256+17
        call put_choice
        ld a,(v_top)
        ld hl,t_top
        ld bc,(MENU_ROW+5)*256+28
        jp put_choice

        INCLUDE "tables.inc"

rainbow:
        DB 0x50,0x70,0x60,0x68          ; bright red, yellow, green, cyan paper

s_title:
        DB " SP-Pie Gigascreen / ULAplus    ",0
        ASSERT $ - s_title == 33
s_keys1:
        DB "1 Primary  2 Secondary  3 Flip  ",0
        ASSERT $ - s_keys1 == 33
s_keys2:
        DB "4 Giga   5 Show as   6 Top bank ",0
        ASSERT $ - s_keys2 == 33
s_keys3:
        DB "7 ULA+ 8 Picture  M Hide  Q Quit",0
        ASSERT $ - s_keys3 == 33
s_state1:
        DB "View:"
        DS 10,' '
        DB "ULA+:"
        DS 4,' '
        DB "Pic:"
        DS 4,' '
        DB 0
        ASSERT $ - s_state1 == 33
s_state2:
        DB "Giga:"
        DS 7,' '
        DB "Show:"
        DS 7,' '
        DB "Top:"
        DS 4,' '
        DB 0
        ASSERT $ - s_state2 == 33

t_view: DW sv0,sv1,sv2
sv0:    DB "Primary  ",0
sv1:    DB "Secondary",0
sv2:    DB "Flip     ",0
t_onoff: DW so0,so1
so0:    DB "Off",0
so1:    DB "On ",0
t_pic:  DW sp0,sp1
sp0:    DB "Giga",0
sp1:    DB "ULA+",0
t_giga: DW sg0,sg1,sg2
sg0:    DB "Off   ",0
sg1:    DB "Auto  ",0
sg2:    DB "Always",0
t_mode: DW sm0,sm1,sm2
sm0:    DB "50/50 ",0
sm1:    DB "CRT   ",0
sm2:    DB "Double",0
t_top:  DW st0,st1
st0:    DB "Norm",0
st1:    DB "Shad",0

v_page: DB 0
v_flip: DB 0
v_bank: DB 0
v_shown: DB 0
v_giga: DB 1                            ; Auto, as after a reset
v_mode: DB 0
v_top:  DB 0
v_ula:  DB 0
v_pic:  DB 0
v_menu: DB 1
v_n:    DB 0
v_attr: DB 0
v_aptr: DW 0

prog_end:
        SND_FINISH "build/demo.tap", "", "gigademo"
