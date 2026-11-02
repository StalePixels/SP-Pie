; lock_test: draw bank 5 (bars, normal screen) and bank 7 (bands, shadow
; screen), then write 0x7FFD with bit 5 set (paging lock) and keep trying
; to flip the screen with bit 3. A working lock leaves the normal screen
; (bank 5) showing for ever. Only a reset clears the lock.
        DEVICE ZXSPECTRUM48
        INCLUDE "common.inc"
        ORG CODE_ORG
start:  di
        ld sp,STACK
        xor a
        out (PORT_ULA),a
        OUT7FFD P7FFD_ROM | 0
        ld hl,0x4000
        call draw_pic_cols
        OUT7FFD P7FFD_ROM | 7
        ld hl,0xC000
        call draw_pic_rows
        ; Lock with bank 7 at 0xC000 and the normal screen selected.
        OUT7FFD P7FFD_ROM | P7FFD_LOCK | 7
        ld a,2
        call wait_halves
.loop:  OUT7FFD P7FFD_ROM | P7FFD_LOCK | P7FFD_SHADOW | 7   ; ignored
        ld a,2
        call wait_halves
        OUT7FFD P7FFD_ROM | P7FFD_LOCK | 7                  ; ignored
        ld a,2
        call wait_halves
        jr .loop

        INCLUDE "draw.inc"
prog_end:
        MAKETAP "build/lock_test.tap", "lock_test"
