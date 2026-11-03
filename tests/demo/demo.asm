; demo: a Gigascreen and ULAplus picture demo for the 128K, +2, +2A and +3.
; Tape build only.
;
; The picture is a macaw (source and licence in README.md), converted by
; convert.py into three screens, each stored in its own RAM bank on the tape:
;   bank 1  Gigascreen pair: the bank 5 screen, then the bank 7 screen
;   bank 3  double-high pair: both screens share one set of attributes
;   bank 4  ULAplus screen, then 64 palette bytes
; The BASIC loader pages each bank in at 0xC000 and loads its block there. The
; program copies the pair it shows into bank 5 (0x4000) and bank 7 (0xC000).
; Bank 7 stays paged in at 0xC000 for the whole run.
;
; The program only writes to ports. Ports written:
;   0x7FFD   paging: bank at 0xC000, ROM bit kept, screen bit 3 for the flip
;   0x043B, 0x053B   SP-Pie feature 3 (display), value in DISP_*
;   0xBF3B, 0xFF3B   ULAplus register and data: palette entries, and group
;                    0x40 with bit 0 as palette on. No other ULAplus mode.
;   0xFE     border
; All writes use A15 = 1 except 0x7FFD (wanted), and the 0x043B and 0x053B
; writes, whose A1 = 1 keeps them out of the 0x7FFD decode.
;
; Keys (the ROM keyboard interrupt supplies them):
;   1 2 3  picture: Gigascreen, double high, ULAplus (each sets matching modes)
;   4 primary  5 secondary  6 flip on/off
;   7 Gigascreen  8 show as  9 double-high top  0 ULAplus
;   M menu  Q quit
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
DISP_PAGE       EQU 0x10
DISP_BANK7      EQU 0x40
DISP_DEFAULT    EQU 0x11

STAGE           EQU 0xA000              ; holds the bank 7 screen while bank 7 is paged in
BANK_GIGA       EQU 1
BANK_DHIGH      EQU 3
BANK_ULA        EQU 4

MENU_ROW        EQU 18
ATTR_MENU       EQU 0x38                ; black ink on white paper
ATTR_TITLE      EQU 0x07                ; white ink on black paper
ATTR_NAME       EQU 0x47                ; bright white on black
ATTR_HINT       EQU 0x45                ; cyan on black

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
        xor a
        call set_picture                ; picture 0 with its modes
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
        ld b,a
        ld hl,keytab
.find:  ld a,(hl)
        or a
        jr z,main
        cp b
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        jr nz,.find
        ex de,hl
        jp (hl)

keytab: DB '1'
        DW k_pic1
        DB '2'
        DW k_pic2
        DB '3'
        DW k_pic3
        DB '4'
        DW k_primary
        DB '5'
        DW k_second
        DB '6'
        DW k_flip
        DB '7'
        DW k_giga
        DB '8'
        DW k_mode
        DB '9'
        DW k_top
        DB '0'
        DW k_ula
        DB 'm'
        DW k_menu
        DB 'q'
        DW quit
        DB 0

k_pic1: xor a
        jr k_pic
k_pic2: ld a,1
        jr k_pic
k_pic3: ld a,2
k_pic:  call set_picture
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

; Make picture A (0 Gigascreen, 1 double high, 2 ULAplus) the one on show, with
; the modes that suit it, and send them.
set_picture:
        ld (v_pic),a
        ld hl,presets
        ld b,a
        add a,a
        add a,a
        add a,b                         ; 5 bytes for each picture
        ld e,a
        ld d,0
        add hl,de
        ld de,v_flip
        ld bc,5
        ldir                            ; flip, giga, mode, top, ula
        xor a
        ld (v_bank),a
        call redraw_all
        call load_palette
        call ula_send
        jp disp_send

presets:
        DB 1,2,0,0,0                    ; Gigascreen: flip, Always, 50/50, Normal, ULAplus off
        DB 1,2,2,0,0                    ; double high: flip, Always, Double high
        DB 0,0,0,0,1                    ; ULAplus: no flip, Gigascreen off, ULAplus on

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

; Page RAM bank A in at 0xC000, keeping the ROM bit and the screen on show.
map_bank:
        ld b,a
        ld a,(v_shown)
        rlca
        rlca
        rlca
        or b
        ld hl,v_page
        ld b,(hl)
        res 0,b
        res 1,b
        res 2,b
        or b
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

; Write the 64 palette entries of the current picture: the 48K colours for the
; two Gigascreen pictures, the picture's own palette for the ULAplus one.
load_palette:
        ld hl,pal_std
        ld a,(v_pic)
        cp 2
        jr c,.go
        ld hl,pal_pic
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

; Copy the current picture into both screens, then the text: the menu if it is
; on, else the title and the hint. Interrupts are off while bank 7 is written:
; on the +3 an interrupt in the middle of the drawing lost part of the shadow
; screen.
redraw_all:
        di
        ld a,(v_pic)
        ld c,a
        ld b,0
        ld hl,pic_banks
        add hl,bc
        ld a,(hl)
        call map_bank
        ld hl,0xC000
        ld de,0x4000
        ld bc,6912
        ldir                            ; the bank 5 screen
        ld a,(v_pic)
        cp 2
        ld hl,0xDB00                    ; the bank 7 screen follows the bank 5 screen
        jr c,.stage
        ld de,pal_pic                   ; ULAplus: the palette follows the one screen
        ld bc,64
        ldir
        ld hl,0xC000                    ; both screens show the same picture
.stage: ld de,STAGE
        ld bc,6912
        ldir
        ld a,7
        call map_bank
        ld hl,STAGE
        ld de,0xC000
        ld bc,6912
        ldir
        ld a,(v_menu)
        or a
        jr nz,.menu
        call text_rows
        ei
        ret
.menu:  call menu_body
        ei
        ret

pic_banks:
        DB BANK_GIGA,BANK_DHIGH,BANK_ULA

; The title and the one-line hint in the last two rows.
text_rows:
        ld a,ATTR_NAME
        ld (v_attr),a
        ld hl,s_name
        ld bc,22*256
        call puts
        ld a,ATTR_HINT
        ld (v_attr),a
        ld hl,s_hint
        ld bc,23*256
        jp puts

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


rainbow:
        DB 0x50,0x70,0x60,0x68          ; bright red, yellow, green, cyan paper

s_name:
        DB "SP-Pie Gigascreen + ULAplus demo",0
        ASSERT $ - s_name == 33
s_hint:
        DB "Press M for the menu, Q to quit ",0
        ASSERT $ - s_hint == 33
s_title:
        DB " Menu        M hide   Q quit    ",0
        ASSERT $ - s_title == 33
s_keys1:
        DB "1 Giga 2 Double high 3 ULAplus  ",0
        ASSERT $ - s_keys1 == 33
s_keys2:
        DB "4 Primary 5 Secondary 6 Flip    ",0
        ASSERT $ - s_keys2 == 33
s_keys3:
        DB "7 Giga 8 Show as 9 Top 0 ULA+   ",0
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
t_pic:  DW sp0,sp1,sp2
sp0:    DB "Giga",0
sp1:    DB "Dble",0
sp2:    DB "ULA+",0
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

pal_std:
        INCBIN "data/pal_std.bin"

; Variables. v_flip to v_ula are the five bytes that set_picture copies.
v_flip: DB 0
v_giga: DB 0
v_mode: DB 0
v_top:  DB 0
v_ula:  DB 0
v_page: DB 0
v_bank: DB 0
v_shown: DB 0
v_pic:  DB 0
v_menu: DB 0
v_attr: DB 0
v_aptr: DW 0
pal_pic:
        DS 64

prog_end:
        ASSERT prog_end < STAGE

; The tape: the BASIC loader, a block for each data bank, then the program.
; 10 CLEAR 32767: POKE 23388,17: OUT 32765,17: LOAD "" CODE: ... for banks 3
;    and 4 (19, 20), then POKE 23388,16: OUT 32765,16: LOAD "" CODE: RANDOMIZE USR 32768
; The value 16 + n pages RAM bank n at 0xC000 with the 48K ROM still in. The
; POKE sets the system variable BANKM, because the ROM interrupt routine writes
; BANKM to port 0x7FFD at every frame interrupt and would undo a bare OUT.
        MACRO BASOUT txt, val
        DB ':', 0xF4
        BASNUM "23388", 23388           ; BANKM: the ROM interrupt writes it back to port 0x7FFD
        DB ','
        BASNUM txt, val
        DB ':', 0xDF
        BASNUM "32765", 32765
        DB ','
        BASNUM txt, val
        DB ':', 0xEF, '"', '"', 0xAF
        ENDM

        ORG 0x6000
bas_start:
        DB 0, 10
        DW bas_line_end - bas_line
bas_line:
        DB 0xFD
        BASNUM "32767", 32767
        BASOUT "17", 17
        BASOUT "19", 19
        BASOUT "20", 20
        DB ':', 0xF4
        BASNUM "23388", 23388
        DB ','
        BASNUM "16", 16
        DB ':', 0xDF
        BASNUM "32765", 32765
        DB ','
        BASNUM "16", 16
        DB ':', 0xEF, '"', '"', 0xAF
        DB ':', 0xF9, 0xC0
        BASNUM "32768", CODE_ORG
        DB 0x0D
bas_line_end:
bas_end:
        SAVETAP "build/demo.tap", BASIC, "gigademo", bas_start, bas_end - bas_start, 10

        ORG 0xC000
        INCBIN "data/giga.bin"
        SAVETAP "build/demo.tap", CODE, "giga", 0xC000, 13824, 0xC000
        ORG 0xC000
        INCBIN "data/dhigh.bin"
        SAVETAP "build/demo.tap", CODE, "dhigh", 0xC000, 13824, 0xC000
        ORG 0xC000
        INCBIN "data/ulaplus.bin"
        SAVETAP "build/demo.tap", CODE, "ulaplus", 0xC000, 6976, 0xC000
        SAVETAP "build/demo.tap", CODE, "gigademo", CODE_ORG, prog_end - CODE_ORG, CODE_ORG
