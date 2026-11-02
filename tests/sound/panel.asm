; panel: SP-Pie control panel, drawn like a Spectrum 128K boot menu: a black
; title bar with a rainbow at the right end, a white menu box, and a cyan bar
; on the selected row. The program draws the screen itself with the attribute
; memory, a few pixel bytes, and the ROM character set through RST 0x10.
;
; Every change goes to the board at once: the feature number to port 0x043B,
; then the value to port 0x053B (docs/control-ports.md).
;
; The ports are write-only. The panel keeps its own copy of each feature
; value, starting at the documented defaults, and shows that copy. It does
; not show what the board holds. The program writes only to ports 0x043B,
; 0x053B and 0xFE (through the ROM, for the border), so no write can page a
; 128K or reach the AY.
;
; Keys:
;   cursor up, down   move the cyan bar (CAPS SHIFT with 7 and 6); they repeat
;   ENTER             toggle the selected item, or step the display mode
;   1 to 6            devices: AY, TurboSound, beeper, Covox 0xFB, 0xDD, 0xDF
;   7                 border extension to 16:9
;   8                 automatic interlace detection
;   9                 mixing always on
;   M                 step the display mode: 50/50 mix, CRT-style mix, line weave
;   H                 HUD
;   S                 send every value again
;   R                 restore the defaults and send them
;   Q                 quit
; A number or letter key moves the bar to its row and acts at once.
;
; The program scans the keyboard itself: IN from port 0xFE with one half-row
; selected on address lines A8 to A15. It needs no interrupt and no ROM
; keyboard routine.
        INCLUDE "sound.inc"

SYS_BORDCR      EQU 0x5C48
SYS_ATTR_P      EQU 0x5C8D
SYS_MASK_P      EQU 0x5C8E
SYS_ATTR_T      EQU 0x5C8F
SYS_MASK_T      EQU 0x5C90
ATTR_SCREEN     EQU 0x5800

; Indexes into the state table. The feature numbers are in featnums.
VAR_DEVICES     EQU 0
VAR_BORDER      EQU 1
VAR_DISPLAY     EQU 2
VAR_DEBUG       EQU 3

; Feature 3 bits.
DISP_AUTO       EQU 0x01
DISP_ALWAYS     EQU 0x02
DISP_MODE_MASK  EQU 0x0C

NITEMS          EQU 11
REPEAT_DELAY    EQU 25                  ; scans before a cursor key repeats
REPEAT_RATE     EQU 5                   ; scans between repeats
PAPER_WHITE     EQU 7
PAPER_CYAN      EQU 5

        SND_BEGIN
        ld hl,defaults
        ld de,state
        ld bc,4
        ldir
        ld a,0x38                       ; black ink on white paper, white border
        ld (SYS_ATTR_P),a
        ld (SYS_ATTR_T),a
        ld (SYS_BORDCR),a
        xor a
        ld (SYS_MASK_P),a
        ld (SYS_MASK_T),a
        ld (cursor),a
        call snd_init
        call draw_box
        ld hl,txt_static
        call print_ff
        call draw_all_rows

key_loop:
        call frame_delay
        call poll_key
        or a
        jr z,key_loop
        cp 'q'
        jp z,quit
        cp 's'
        jp z,do_send_all
        cp 'r'
        jp z,do_defaults
        cp 13
        jp z,do_enter
        cp 10
        jp z,do_down
        cp 11
        jp z,do_up
        ld b,a                          ; look the key up in the item table
        ld hl,items
        ld c,0
.find:  ld a,(hl)
        cp b
        jr z,.hit
        ld de,6
        add hl,de
        inc c
        ld a,c
        cp NITEMS
        jr nz,.find
        jp key_loop
.hit:   ld a,(cursor)                   ; move the bar to the row, then act
        ld d,a
        ld a,c
        ld (cursor),a
        push bc
        ld a,d
        call draw_row
        pop bc
        ld a,c
        call activate
        jp key_loop

do_enter:
        ld a,(cursor)
        call activate
        jp key_loop

do_down:
        ld a,(cursor)
        ld b,a
        inc a
        cp NITEMS
        jr c,move
        xor a
        jr move

do_up:  ld a,(cursor)
        ld b,a
        or a
        jr nz,.dec
        ld a,NITEMS
.dec:   dec a
move:   ld (cursor),a
        push af
        ld a,b
        call draw_row
        pop af
        call draw_row
        jp key_loop

do_send_all:
        call send_all
        jp key_loop

do_defaults:
        ld hl,defaults
        ld de,state
        ld bc,4
        ldir
        call send_all
        call draw_all_rows
        jp key_loop

quit:   call snd_init
        SND_RETURN

; Wait one frame: 2690 passes of a 26 T-state loop, 69940 T-states.
frame_delay:
        ld bc,2690
.l:     dec bc
        ld a,b
        or c
        jr nz,.l
        ret

; Scan the keyboard once per frame. Returns A = a key code, or 0 for no key
; or for no new key. A key must read the same on two scans in a row, which
; is the debounce. Only the cursor keys repeat: after REPEAT_DELAY scans, and
; then every REPEAT_RATE scans.
poll_key:
        call scan_keys
        ld b,a
        ld a,(prev_raw)
        cp b
        ld a,b
        ld (prev_raw),a
        jr nz,.none                     ; not stable yet
        ld a,(last_key)
        cp b
        jr nz,.new
        ld a,b
        or a
        jr z,.none
        cp 10
        jr z,.rep
        cp 11
        jr nz,.none
.rep:   ld a,(rep_count)
        dec a
        ld (rep_count),a
        jr nz,.none
        ld a,REPEAT_RATE
        ld (rep_count),a
        ld a,b
        ret
.new:   ld a,b
        ld (last_key),a
        ld a,REPEAT_DELAY
        ld (rep_count),a
        ld a,b
        ret
.none:  xor a
        ret

; Read the eight half-rows of the keyboard. Returns A = the code of the first
; pressed key that has one, or 0. A bit that reads 0 is a pressed key. With
; CAPS SHIFT held, 7 gives 11 (up), 6 gives 10 (down), other digits give 0.
scan_keys:
        ld bc,0xFEFE
        in a,(c)
        and 1
        ld d,a                          ; D = 0 while CAPS SHIFT is held
        ld hl,keymap
        ld e,8
.row:   ld b,(hl)                       ; half-row address, high byte
        inc hl
        ld c,0xFE
        in a,(c)
        cpl                             ; a 1 bit is a pressed key
        ld b,5
.bit:   rra
        jr nc,.no
        ld c,a
        ld a,(hl)
        or a
        jr nz,.got
        ld a,c
.no:    inc hl
        djnz .bit
        dec e
        jr nz,.row
        xor a
        ret
.got:   ld c,a
        ld a,d
        or a
        ld a,c
        ret nz
        cp '6'
        jr z,.down
        cp '7'
        jr z,.up
        cp '1'
        ret c                           ; ENTER
        cp '9'+1
        ret nc                          ; letters
        xor a
        ret
.down:  ld a,10
        ret
.up:    ld a,11
        ret

; Half-rows: the high byte of the port address, then the codes of bits 0 to 4.
; 0 means a key the panel does not use.
keymap: DB 0xF7,'1','2','3','4','5'
        DB 0xEF,0,'9','8','7','6'
        DB 0xFB,'q',0,0,'r',0
        DB 0xDF,0,0,0,0,0
        DB 0xFD,0,'s',0,0,0
        DB 0xBF,13,0,0,0,'h'
        DB 0xFE,0,0,0,0,0
        DB 0x7F,0,0,'m',0,0

prev_raw:
        DB 0
last_key:
        DB 0
rep_count:
        DB 0

; Act on item A: toggle its bit, or step the display mode. Send and redraw.
activate:
        ld (a_idx),a
        ld l,a
        ld h,0
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        add hl,hl
        ld de,items
        add hl,de
        inc hl                          ; skip the key
        inc hl                          ; skip the row
        ld e,(hl)                       ; state variable
        inc hl
        ld c,(hl)                       ; bit mask, 0 for the display mode
        ld a,c
        or a
        jr z,.mode
        ld d,0
        ld hl,state
        add hl,de
        ld a,(hl)
        xor c
        ld (hl),a
        jr .send
.mode:  ld e,VAR_DISPLAY                ; 50/50 mix, CRT-style mix, line weave
        ld a,(state+VAR_DISPLAY)
        ld b,a
        rrca
        rrca
        and 3
        inc a
        cp 3
        jr c,.ok
        xor a
.ok:    add a,a
        add a,a
        ld c,a
        ld a,b
        and 0xFF-DISP_MODE_MASK
        or c
        ld (state+VAR_DISPLAY),a
.send:  call send_var
        ld a,(a_idx)
        jp draw_row

; Send the value of state variable E to the board.
send_var:
        ld d,0
        ld hl,featnums
        add hl,de
        ld a,(hl)
        ld bc,PORT_FEAT_SEL
        out (c),a
        ld hl,state
        add hl,de
        ld a,(hl)
        ld bc,PORT_FEAT_DATA
        out (c),a
        ret

send_all:
        ld e,0
.l:     push de
        call send_var
        pop de
        inc e
        ld a,e
        cp 4
        jr nz,.l
        ret

; Print the string at HL up to a 0xFF byte. The strings hold control codes
; whose values include 0, so they cannot end in a zero byte.
print_ff:
        ld a,255                        ; stop the "scroll?" prompt
        ld (0x5C8C),a
        ld a,(hl)
        inc hl
        cp 255
        ret z
        push hl
        ROMPRINT
        pop hl
        jr print_ff

; Print the cursor position: B = row, C = column.
put_at:
        push bc
        ld a,22
        ROMPRINT
        pop bc
        push bc
        ld a,b
        ROMPRINT
        pop bc
        ld a,c
        ROMPRINT
        ret

draw_all_rows:
        ld a,0
.l:     push af
        call draw_row
        pop af
        inc a
        cp NITEMS
        jr nz,.l
        ret

; Draw item A: a cleared bar, the label, and the value. The bar is cyan when
; the item is under the cursor.
draw_row:
        ld (r_idx),a
        ld l,a
        ld h,0
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        add hl,hl
        ld de,items
        add hl,de
        inc hl
        ld a,(hl)
        ld (r_row),a
        inc hl
        ld a,(hl)
        ld (r_var),a
        inc hl
        ld a,(hl)
        ld (r_mask),a
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (r_lbl),de
        ld a,(cursor)
        ld b,a
        ld a,(r_idx)
        cp b
        ld a,PAPER_WHITE
        jr nz,.p
        ld a,PAPER_CYAN
.p:     ld (colbuf+1),a
        ld hl,colbuf
        call print_ff
        ld a,(r_row)
        ld b,a
        ld c,4
        call put_at
        ld hl,txt_blank
        call print_ff
        ld a,(r_row)
        ld b,a
        ld c,5
        call put_at
        ld hl,(r_lbl)
        call print_ff
        ld a,(r_mask)
        or a
        jr z,.mode
        ld a,(r_row)
        ld b,a
        ld c,24
        call put_at
        ld a,(r_var)
        ld e,a
        ld d,0
        ld hl,state
        add hl,de
        ld a,(r_mask)
        and (hl)
        ld hl,txt_on
        jr nz,.val
        ld hl,txt_off
.val:   jp print_ff
.mode:  ld a,(r_row)
        ld b,a
        ld c,13
        call put_at
        ld a,(state+VAR_DISPLAY)
        rrca
        rrca
        and 3
        add a,a
        add a,a
        add a,a
        add a,a                         ; each name takes 16 bytes
        ld e,a
        ld d,0
        ld hl,mode_names
        add hl,de
        jp print_ff

; Colour the box and draw the rainbow. Attribute 0x78 is bright white paper
; with black ink, 0x47 is a black bar with bright white ink.
draw_box:
        ld hl,ATTR_SCREEN+3
        ld a,0x47
        ld b,26
        call fill_attr
        ld hl,ATTR_SCREEN+32+3
        ld c,17
.row:   push hl
        ld a,0x78
        ld b,26
        call fill_attr
        pop hl
        ld de,32
        add hl,de
        dec c
        jr nz,.row
        ld hl,rainbow_attr              ; four cells of stripes, then cyan
        ld de,ATTR_SCREEN+24
        ld bc,5
        ldir
        ld hl,diagonal                  ; each stripe cell is a slanted edge
        ld de,0x4000+24
        ld b,8
.y:     ld a,(hl)
        inc hl
        push de
        ld c,4
.x:     ld (de),a
        inc de
        dec c
        jr nz,.x
        pop de
        inc d                           ; next pixel line of the cell
        djnz .y
        ret

fill_attr:
.l:     ld (hl),a
        inc hl
        djnz .l
        ret

; Bright cells: paper is the colour before, ink the colour of the stripe.
; Black to red, red to yellow, yellow to green, green to cyan, then cyan.
rainbow_attr:
        DB 0x42,0x56,0x74,0x65,0x6D
diagonal:
        DB 0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF

; Item table: key, screen row, state variable, bit mask (0 = display mode),
; label address.
items:  DB '1',3,VAR_DEVICES,0x01
        DW lbl_ay
        DB '2',4,VAR_DEVICES,0x02
        DW lbl_turbo
        DB '3',5,VAR_DEVICES,0x04
        DW lbl_beeper
        DB '4',6,VAR_DEVICES,0x08
        DW lbl_fb
        DB '5',7,VAR_DEVICES,0x10
        DW lbl_dd
        DB '6',8,VAR_DEVICES,0x20
        DW lbl_df
        DB '7',10,VAR_BORDER,0x01
        DW lbl_border
        DB '8',12,VAR_DISPLAY,DISP_AUTO
        DW lbl_auto
        DB '9',13,VAR_DISPLAY,DISP_ALWAYS
        DW lbl_mix
        DB 'm',14,VAR_DISPLAY,0
        DW lbl_mode
        DB 'h',16,VAR_DEBUG,0x01
        DW lbl_hud

lbl_ay:     DB "1 AY chip",255
lbl_turbo:  DB "2 TurboSound",255
lbl_beeper: DB "3 Beeper",255
lbl_fb:     DB "4 Covox 0xFB",255
lbl_dd:     DB "5 Covox 0xDD",255
lbl_df:     DB "6 Covox 0xDF",255
lbl_border: DB "7 Extend to 16:9",255
lbl_auto:   DB "8 Auto interlace",255
lbl_mix:    DB "9 Mixing always on",255
lbl_mode:   DB "M Mode",255
lbl_hud:    DB "H HUD",255

; Documented defaults, and the feature number of each state variable.
defaults:
        DB FEAT_DEFAULT, 0x00, DISP_AUTO, 0x01
featnums:
        DB 1, 2, 3, 0xFF
state:  DS 4
cursor: DB 0
a_idx:  DB 0
r_idx:  DB 0
r_row:  DB 0
r_var:  DB 0
r_mask: DB 0
r_lbl:  DW 0

; PAPER (byte 1 is patched), INK 0, BRIGHT 1.
colbuf: DB 17,7,16,0,19,1,255
txt_blank:
        DB "                        ",255     ; the 24 cells of the bar
txt_on: DB 16,1,"ON ",255
txt_off:
        DB 16,2,"OFF",255

; Display mode names, 16 bytes each, in the order of mode bits 00, 01, 10, 11.
mode_names:
        DB "50/50 mix      ",255
        DB "CRT-style mix  ",255
        DB "Line weave     ",255
        DB "Reserved       ",255

; AT is 22,row,column. INK is 16, PAPER 17, BRIGHT 19.
txt_static:
        DB 22,0,4,17,0,16,7,19,1,"SP-Pie Panel"
        DB 22,2,4,17,7,16,1,19,1,"DEVICES"
        DB 22,9,4,"BORDER"
        DB 22,11,4,"DISPLAY"
        DB 22,15,4,"DEBUG"
        DB 22,18,1,17,7,16,0,19,0,"Arrows+ENTER, or press a key."
        DB 22,19,1,"S send all  R defaults  Q quit"
        DB 22,20,1,"This shows what the panel has"
        DB 22,21,1,"set, not what the board holds.",255

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/panel.tap", "build/dot/panel", "panel"
