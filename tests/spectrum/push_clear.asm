; push_clear: clear and fill the screen with PUSH, the fastest Z80 memory
; write, with interrupts off. Sequence, repeated until reset:
; clear, picture A, clear, picture B, with a pause after each step.
; The stack pointer is moved onto the screen, so nothing is called while
; a fill runs.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"

; PUSHRUN word, blocks: push `word` 16 * blocks times, moving down in memory.
        MACRO PUSHRUN word, blocks
        ld hl,word
        ld b,blocks
        DUP 16
        push hl
        EDUP
        djnz $ - 16
        ENDM

; FILLSCREEN a3, a2, a1, p3, p2, p1: fill from the end of the screen down.
; a1..a3 are attribute words for the top, middle and bottom third
; (256 bytes each); p1..p3 are pixel words for the same thirds (2048 bytes
; each). A word is written low byte first.
        MACRO FILLSCREEN a1, a2, a3, p1, p2, p3
        ld (saved_sp),sp
        ld sp,0x5B00
        PUSHRUN a3, 8
        PUSHRUN a2, 8
        PUSHRUN a1, 8
        PUSHRUN p3, 64
        PUSHRUN p2, 64
        PUSHRUN p1, 64
        ld sp,(saved_sp)
        ENDM

        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a
.loop:  FILLSCREEN 0x3838, 0x3838, 0x3838, 0x0000, 0x0000, 0x0000
        ld a,1
        call wait_halves
        ; Picture A: white ink on red, cyan, blue paper (top to bottom).
        ; Pixels: 8 px bars, 1 px bars, 4 px bars (top to bottom).
        FILLSCREEN 0x1717, 0x2F2F, 0x0F0F, 0x00FF, 0xAAAA, 0xF0F0
        ld a,3
        call wait_halves
        FILLSCREEN 0x3838, 0x3838, 0x3838, 0x0000, 0x0000, 0x0000
        ld a,1
        call wait_halves
        ; Picture B: bright yellow, bright cyan, bright green paper;
        ; solid pixels, 2 px bars, 4 px bars (top to bottom).
        FILLSCREEN 0x7E7E, 0x6C6C, 0x5C5C, 0xFFFF, 0x3333, 0x0F0F
        ld a,3
        call wait_halves
        jp .loop

saved_sp: DW 0

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/push_clear.tap", "push_clear"
