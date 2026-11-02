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
PORT_FEAT_SEL    EQU 0x043B
PORT_FEAT_DATA    EQU 0x053B
PORT_COVOX_FB   EQU 0xFB
PORT_COVOX_DD   EQU 0xDD
PORT_COVOX_DD_AYDATA EQU 0xBFDD
PORT_COVOX_DF   EQU 0xDF

; Features (see docs/control-ports.md). Write the feature number to port
; 0x043B and its value to port 0x053B.
; Feature 0xFF, debug: bit 0 = HUD.
; Feature 1, attached devices: the bits below.
; Feature 2, border: bit 0 = border extension to 16:9.
FEAT_DEBUG      EQU 0xFF
FEAT_DEVICES    EQU 0x01
FEAT_BORDER     EQU 0x02
FEAT_AY         EQU 0x01
FEAT_TURBO      EQU 0x02
FEAT_BEEPER     EQU 0x04
FEAT_COVOX_FB   EQU 0x08
FEAT_COVOX_DD   EQU 0x10
FEAT_COVOX_DF   EQU 0x20
FEAT_DEFAULT    EQU FEAT_AY|FEAT_TURBO|FEAT_BEEPER

; Pseudo register number in a step table: the value is written to port
; 0xFFFD as a TurboSound chip select (NedoPC order: 0xFE = chip 0, the chip
; a reset selects, and 0xFF = chip 1).
CHIP_SELECT     EQU 0xF0
CHIP0           EQU 0xFE
CHIP1           EQU 0xFF

; Call a routine in the 48K ROM. A tape program runs with the 48K ROM paged
; in, so it calls the routine directly. While a dot command runs, ESXDOS has
; its own ROM paged in at 0x0000-0x1FFF. ESXDOS provides RST 0x18 followed by
; the routine address for a ROM call (z88dk 2.4,
; libsrc/_DEVELOPMENT/target/zx/config/config_zxn_nextos.m4:
; __ESX_RST_ROM = 0x18, "zx rom call, address follows rst").
        MACRO ROMCALL addr
        IFDEF TAP
        call addr
        ELSE
        rst 0x18
        DW addr
        ENDIF
        ENDM

; Print the character in A through the ROM (RST 0x10, the ROM routine at 0x0010).
        MACRO ROMPRINT
        IFDEF TAP
        rst 0x10
        ELSE
        rst 0x18
        DW 0x0010
        ENDIF
        ENDM

; Write a feature value: select the feature (port 0x043B), then write the
; value (port 0x053B).
        MACRO FEAT num, val
        ld bc,PORT_FEAT_SEL
        ld a,num
        out (c),a
        ld bc,PORT_FEAT_DATA
        ld a,val
        out (c),a
        ENDM

; Restore the features to their defaults.
        MACRO FEAT_DEFAULTS
        FEAT FEAT_DEBUG, 1
        FEAT FEAT_DEVICES, FEAT_DEFAULT
        FEAT FEAT_BORDER, 0
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
