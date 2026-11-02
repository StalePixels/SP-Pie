; Shared definitions for the SP-Pie sound test programs.
; Assemble with -DTAP for a tape file (code at 0x8000, started from a BASIC
; loader) or -DDOT for an ESXDOS dot command (code at 0x2000).
; Include this file first. Include snd_lib.inc after the program code.

        DEVICE ZXSPECTRUM48

        IFDEF TAP
CODE_ORG        EQU 0x8000
        ELSE
CODE_ORG        EQU 0x2000
        ENDIF

ROM_CLS         EQU 0x0DAF
ROM_CHAN_OPEN   EQU 0x1601

PORT_ULA        EQU 0xFE
PORT_AY_REG     EQU 0xFFFD
PORT_AY_DATA    EQU 0xBFFD
PORT_CTL_SEL    EQU 0x043B
PORT_CTL_PAR    EQU 0x053B
PORT_COVOX_FB   EQU 0xFB
PORT_COVOX_DD   EQU 0xDD
PORT_COVOX_DD_AYDATA EQU 0xBFDD
PORT_COVOX_DF   EQU 0xDF

; Control select 0: bit 0 = HUD on.
; Control select 1: feature enables.
FEAT_AY         EQU 0x01
FEAT_TURBO      EQU 0x02
FEAT_BEEPER     EQU 0x04
FEAT_COVOX_FB   EQU 0x08
FEAT_COVOX_DD   EQU 0x10
FEAT_COVOX_DF   EQU 0x20
FEAT_DEFAULT    EQU FEAT_AY|FEAT_TURBO|FEAT_BEEPER

; Pseudo register number in a step table: the value is written to port
; 0xFFFD as a TurboSound chip select (0xFF = chip 0, 0xFE = chip 1).
CHIP_SELECT     EQU 0xF0
CHIP0           EQU 0xFF
CHIP1           EQU 0xFE

; Write A to control port (select 0x043B or parameter 0x053B).
        MACRO CTL sel, par
        ld bc,PORT_CTL_SEL
        ld a,sel
        out (c),a
        ld bc,PORT_CTL_PAR
        ld a,par
        out (c),a
        ENDM

; Restore the control ports to their defaults.
        MACRO CTL_DEFAULTS
        CTL 0, 1
        CTL 1, FEAT_DEFAULT
        ENDM

; Write one AY register.
        MACRO AYW reg, val
        ld bc,PORT_AY_REG
        ld a,reg
        out (c),a
        ld b,high(PORT_AY_DATA)
        ld a,val
        out (c),a
        ENDM

; BASIC number: the digits, then the hidden 5-byte value the ROM expects.
        MACRO BASNUM txt, val
        DB txt, 0x0E, 0, 0, low(val), high(val), 0
        ENDM

; Program start: the first code byte of the program.
        MACRO SND_BEGIN
        ORG CODE_ORG
start:
        ENDM

; Return to BASIC (tape) or to ESXDOS (dot command, carry clear = no error).
        MACRO SND_RETURN
        IFDEF TAP
        ret
        ELSE
        or a
        ret
        ENDIF
        ENDM

; Write the output file. Call after the program, with prog_end defined.
; Tape: 10 CLEAR 32767: LOAD "" CODE: RANDOMIZE USR 32768
        MACRO SND_FINISH tapfile, dotfile, name
        IFDEF TAP
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
        SAVETAP tapfile, BASIC, name, bas_start, bas_end - bas_start, 10
        SAVETAP tapfile, CODE, name, CODE_ORG, prog_end - CODE_ORG, CODE_ORG
        ELSE
        SAVEBIN dotfile, CODE_ORG, prog_end - CODE_ORG
        ENDIF
        ENDM
