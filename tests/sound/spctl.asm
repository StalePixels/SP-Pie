; spctl: sets the SP-Pie features. ESXDOS dot command only.
;   .spctl              prints the usage
;   .spctl NUM          writes the feature number NUM to the feature select
;                       port (0x043B)
;   .spctl NUM VAL      writes NUM to the feature select port (0x043B) and
;                       VAL to the feature data port (0x053B)
; The ports are write-only, so spctl cannot read a feature back.
; Numbers are decimal, or hex with a $ or 0x prefix.
        IFNDEF DOT
        DISPLAY "spctl is a dot command: assemble with -DDOT"
        ENDIF
        INCLUDE "sound.inc"
        SND_BEGIN
        push hl
        call snd_init
        pop hl
        call parse_num
        jp c,usage
        ld a,d
        or a
        jp nz,range
        ld a,e
        ld (sel),a
        call parse_num
        jr c,show
        ld a,d
        or a
        jp nz,range
        ld a,e
        ld (par),a
        ; set
        ld bc,PORT_FEAT_SEL
        ld a,(sel)
        out (c),a
        ld bc,PORT_FEAT_DATA
        ld a,(par)
        out (c),a
        ld hl,txt_set
        call print_str
        ld a,(sel)
        call print_hex8
        ld hl,txt_par
        call print_str
        ld a,(par)
        call print_hex8
        ld a,13
        ROMPRINT
        SND_RETURN

show:   ld bc,PORT_FEAT_SEL
        ld a,(sel)
        out (c),a
        ld hl,txt_selected
        call print_str
        ld a,(sel)
        call print_hex8
        ld a,13
        ROMPRINT
        SND_RETURN

usage:  ld hl,txt_usage
        call print_str
        SND_RETURN

range:  ld hl,txt_range
        call print_str
        scf
        ld a,9                          ; ESXDOS error: invalid argument
        ret

; Read a number at HL (spaces skipped). Result in DE, HL after the number.
; Carry set when there is no number.
parse_num:
        ld a,(hl)
        cp ' '
        jr nz,.start
        inc hl
        jr parse_num
.start: ld de,0
        ld c,10
        cp '$'
        jr z,.hex1
        cp '0'
        jr nz,.digits
        inc hl
        ld a,(hl)
        dec hl
        or 0x20
        cp 'x'
        jr nz,.digits
        inc hl
.hex1:  inc hl
        ld c,16
.digits:
        ld b,0                          ; digit count
.next:  ld a,(hl)
        sub '0'
        cp 10
        jr c,.got
        ld a,c
        cp 16
        jr nz,.end
        ld a,(hl)
        or 0x20
        sub 'a'
        cp 6
        jr nc,.end
        add a,10
.got:   push hl
        push af
        ld a,c
        cp 16
        jr z,.x16
        ld h,d                          ; DE = DE * 10
        ld l,e
        add hl,hl
        add hl,hl
        add hl,de
        add hl,hl
        jr .acc
.x16:   ld h,d                          ; DE = DE * 16
        ld l,e
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
.acc:   pop af
        ld e,a
        ld d,0
        add hl,de
        ex de,hl
        pop hl
        inc hl
        inc b
        jr .next
.end:   ld a,b
        or a
        jr nz,.ok
        scf
        ret
.ok:    or a
        ret

sel:    DB 0
par:    DB 0
txt_usage:
        DB "spctl NUM [VAL]",13
        DB "Write feature NUM to the feature",13
        DB "select port 0x043B and VAL to the",13
        DB "feature data port 0x053B.",13
        DB "Numbers: decimal, $hex or 0xhex.",13
        DB "1: devices. Bit 0 AY, 1 TurboSound,",13
        DB " 2 beeper, 3 Covox 0xFB,",13
        DB " 4 Covox 0xDD, 5 Covox 0xDF.",13
        DB "2: border. Bit 0 16:9 extension.",13
        DB "255: debug. Bit 0 HUD.",13
        DB "Reset restores the defaults.",13,0
txt_set:
        DB "Wrote feature 0x",0
txt_selected:
        DB "Selected feature 0x",0
txt_par:
        DB " value 0x",0
txt_range:
        DB "Values must be 0 to 255",13,0

        INCLUDE "snd_lib.inc"
prog_end:
        SND_FINISH "build/tap/spctl.tap", "build/dot/spctl", "spctl"
