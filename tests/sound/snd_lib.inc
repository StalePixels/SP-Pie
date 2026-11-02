; Shared routines for the SP-Pie sound test programs. Include after the
; program code. Uses the 48K ROM for printing, so the ROM must be paged in
; (a 128K or +3 tape load must come from the Tape Loader menu entry).

; Clear the screen and open the upper screen channel for printing.
snd_init:
        call ROM_CLS
        ld a,2
        jp ROM_CHAN_OPEN

; Print the zero-terminated string at HL. Returns HL after the zero.
print_str:
        ld a,255                        ; stop the "scroll?" prompt
        ld (0x5C8C),a
        ld a,(hl)
        inc hl
        or a
        ret z
        push hl
        rst 0x10
        pop hl
        jr print_str

; Print A as two hex digits.
print_hex8:
        push af
        rrca
        rrca
        rrca
        rrca
        call .nib
        pop af
.nib:   and 0x0F
        add a,'0'
        cp '9'+1
        jr c,.out
        add a,'A'-'9'-1
.out:   rst 0x10
        ret

; Wait A frames (50 Hz). Turns interrupts on.
wait_frames:
        ei
        ld b,a
.l:     halt
        djnz .l
        ret

; Run a step table at HL. Each step is
;   zero-terminated text, frame count, then (register, value) pairs
;   ended by 0xFF.
; A register of CHIP_SELECT writes the value to port 0xFFFD as a chip
; select. A step that starts with a zero byte ends the table.
run_steps:
        ld a,(hl)
        or a
        ret z
        call print_str
        ld a,(hl)
        inc hl
        push af
.pair:  ld a,(hl)
        inc hl
        cp 0xFF
        jr z,.done
        ld e,(hl)
        inc hl
        ld bc,PORT_AY_REG
        cp CHIP_SELECT
        jr z,.chip
        out (c),a
        ld b,high(PORT_AY_DATA)
.chip:  out (c),e
        jr .pair
.done:  pop af
        call wait_frames
        jr run_steps
