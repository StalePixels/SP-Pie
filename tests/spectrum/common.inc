; Shared definitions for the SP-Pie Spectrum test programs.
; Every program is assembled at 0x8000 and started from a BASIC loader.
; Include this file first, then ORG 0x8000.

CODE_ORG        EQU 0x8000
STACK           EQU 0xBFF0

PORT_ULA        EQU 0xFE
PORT_7FFD       EQU 0x7FFD
PORT_1FFD       EQU 0x1FFD
PORT_ULP_REG    EQU 0xBF3B
PORT_ULP_DATA   EQU 0xFF3B

; Port 0x7FFD value: RAM bank at 0xC000 (bits 2..0), shadow screen (bit 3),
; ROM select bit (bit 4, set = same ROM as BASIC after LOAD), lock (bit 5).
P7FFD_ROM       EQU 0x10
P7FFD_SHADOW    EQU 0x08
P7FFD_LOCK      EQU 0x20

; Write a constant to port 0x7FFD or 0x1FFD with no CALL, so it is safe to
; use when the write changes which RAM bank holds the stack.
        MACRO OUT7FFD val
        ld bc,PORT_7FFD
        ld a,val
        out (c),a
        ENDM

        MACRO OUT1FFD val
        ld bc,PORT_1FFD
        ld a,val
        out (c),a
        ENDM

; BASIC number: the digits, then the hidden 5-byte value the ROM expects.
        MACRO BASNUM txt, val
        DB txt, 0x0E, 0, 0, low(val), high(val), 0
        ENDM

; Writes a BASIC loader and the code to a .tap file.
; Call after the program, with prog_end defined.
;   10 CLEAR 32767: LOAD "" CODE: RANDOMIZE USR 32768
        MACRO MAKETAP file, tapname
        ORG 0x6000
bas_start:
        DB 0, 10
        DW bas_line_end - bas_line
bas_line:
        DB 0xFD                         ; CLEAR
        BASNUM "32767", 32767
        DB ':', 0xEF, '"', '"', 0xAF    ; : LOAD "" CODE
        DB ':', 0xF9, 0xC0              ; : RANDOMIZE USR
        BASNUM "32768", CODE_ORG
        DB 0x0D
bas_line_end:
bas_end:
        SAVETAP file, BASIC, tapname, bas_start, bas_end - bas_start, 10
        SAVETAP file, CODE, tapname, CODE_ORG, prog_end - CODE_ORG, CODE_ORG
        ENDM
