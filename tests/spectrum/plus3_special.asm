; plus3_special: +2A / +3 special paging (port 0x1FFD bit 0 = 1).
; Draws bank 7 in two ways and shows it with port 0x7FFD bit 3:
;   layout bits 2,1 = 1,1 (banks 4,7,6,3): bank 7 is at 0x4000. The top
;     third of bank 7 is drawn through 0x4000 (vertical bars).
;   layout bits 2,1 = 0,1 (banks 4,5,6,7): bank 7 is at 0xC000. The lower
;     two thirds of bank 7 are drawn through 0xC000 (horizontal bands).
; Bank 5, the normal screen, is cleared once and never written again.
; Then paging goes back to normal and the screen flips between bank 5
; (plain white) and bank 7 about every second, until reset.
;
; Layouts 1,1 and 0,1 both map bank 6 at 0x8000, and normal paging maps
; bank 2 there. The program image is copied into bank 6 at the same
; addresses it has in bank 2, so the Z80 keeps running when 0x8000 changes
; bank. The stack is at 0xBFF0 in both banks. Nothing is CALLed across a
; paging change, and interrupts stay off for the whole run (special paging
; has no ROM at 0x0000).
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"

P1FFD_SPECIAL_11 EQU 0x07           ; special, bits 2,1 = 1,1
P1FFD_SPECIAL_01 EQU 0x03           ; special, bits 2,1 = 0,1

        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a
        ; Copy the image into bank 6, seen at 0xC000 for now.
        OUT7FFD P7FFD_ROM | 6
        ld hl,CODE_ORG
        ld de,0xC000
        ld bc,prog_end - CODE_ORG
        ldir
        ; Normal paging, bank 0 at 0xC000, normal screen.
        OUT7FFD P7FFD_ROM | 0
        ld hl,0x4000
        call clear_screen

.cycle: ; Layout 1,1: bank 7 at 0x4000. Top third of the picture.
        OUT1FFD P1FFD_SPECIAL_11
        ld hl,pix_tile_a
        ld de,0x4000
        ld bc,2048 - 32
        call fill32
        ld hl,attr_tile_a
        ld de,0x5800
        ld bc,256 - 32
        call fill32
        ; Layout 0,1: bank 7 at 0xC000. Lower two thirds of the picture.
        OUT1FFD P1FFD_SPECIAL_01
        ld hl,pix_tile_b
        ld de,0xC800
        ld bc,4096 - 32
        call fill32
        ld de,0xC000 + 0x1800 + 8 * 32
        ld c,8
        call attr_rows
        ; Back to normal paging.
        OUT1FFD 0
        OUT7FFD P7FFD_ROM | P7FFD_SHADOW    ; show bank 7
        ld a,2
        call wait_halves
        OUT7FFD P7FFD_ROM                   ; show bank 5
        ld a,2
        call wait_halves
        jp .cycle

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/plus3_special.tap", "plus3_spec"
