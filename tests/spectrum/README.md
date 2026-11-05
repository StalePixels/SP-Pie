# Spectrum test programs

Small Z80 programs that draw on the Spectrum screen in ways that stress the SP-Pie board. Each one produces a picture that you can judge by eye. Put the Spectrum's own video output (TV or monitor) next to the HDMI output of the board. The two pictures must match at every moment, apart from the normal short delay of the HDMI path.

Every program disables interrupts, then runs in a loop until you press reset.

## Build

```
./build.sh
```

This needs `sjasmplus`. The script uses `sjasmplus` on the path. Set `SJASMPLUS=/path/to/sjasmplus` to use another copy. It writes one `.tap` file for each program, plus a listing, into `build/`. That folder is not tracked by git.

## Load

Each `.tap` file holds a one-line BASIC loader and the machine code. Play it into the tape input, or give it to a tape emulator, then type `LOAD ""` and press Enter. On a 128K, +2, +2A or +3 choose Tape Loader from the menu first. The loader runs `CLEAR 32767`, loads the code at 32768 and starts it.

Nothing prints while the program runs. The first thing each program does is set the border to black.

## The two shared pictures

Several programs use these. Each picture fills all 24 character rows and 32 character columns.

**Bars.** Attributes change along the row and repeat down the screen. In column c (counting from 0 at the left), the paper colour is c mod 8, the ink colour is 7 minus that, and columns 8 to 15 and 24 to 31 are BRIGHT. So the paper colours run black, blue, red, magenta, green, cyan, yellow, white, eight times over four groups, with the ink always the opposite end of that list. Even columns hold one-pixel vertical stripes (pixel byte 0xAA). Odd columns hold a block of four ink pixels then four paper pixels (pixel byte 0xF0).

**Bands.** Attributes change down the screen and repeat along the row. In character row r, the paper colour is r mod 8, the ink colour is 7 minus that, and rows 8 to 15 are BRIGHT. Pixel bytes alternate between 0x3C (ink in the middle of the cell) and 0xC3 (ink at both edges), so each row shows a repeating pattern of narrow vertical marks in the band colour.

## ldir_fill

- Machines. All (48K, 128K, +2, +2A, +3).
- What it does. Fills pixels and attributes with LDIR, alternating Bars and Bands. Each picture stays for about 1 second, then the next fill starts.
- Correct picture. Bars, then Bands, then Bars again, with no leftover cells from the previous picture and no wrong colours. Each fill takes a few tens of milliseconds, so a brief mix of both pictures may show during the change. Both screens must show it the same way.

## push_clear

- Machines. All.
- What it does. Moves the stack pointer onto the screen and fills it with PUSH instructions, the fastest way the Z80 can write memory. Interrupts are off. The sequence is clear, picture A, clear, picture B, repeated.
- Correct picture.
  - Clear. The whole screen is black ink on white paper, no pixels set. It stays about 0.5 seconds.
  - Picture A (about 1.5 seconds). The top third has white ink on red paper with 8-pixel-wide vertical bars. The middle third has white ink on cyan paper with fine one-pixel stripes. The bottom third has white ink on blue paper with 4-pixel-wide bars.
  - Clear again.
  - Picture B (about 1.5 seconds). The top third is solid yellow (bright white paper, yellow ink, every pixel set). The middle third has bright cyan paper with green ink in 2-pixel-wide bars. The bottom third has bright magenta paper with green ink in 4-pixel-wide bars.
- Any cell that stays from the previous picture, or any third with the wrong pattern, is a missed write.

## border_loop

- Machines. All.
- What it does. Writes port 0xFE in a tight loop. Bits 3 and 4 are always 0, so the MIC and speaker stay off.
- Correct picture. Border only. About 1 second of fast cycling through the eight colours, where the border shows as a blur of colour. Then each of the eight colours in order for about 0.5 seconds each (black, blue, red, magenta, green, cyan, yellow, white). Then it starts again. The speaker makes no sound. The fast phase will not match line for line between Spectrum and HDMI. Compare the slow phase.

## ulaplus

- Machines. Any machine with ULAplus (for example a Spectrum Next, ZX-Uno, or a ULAplus modification). On a machine without ULAplus the Spectrum shows only the plain version described below, and only the HDMI shows palette colours.
- What it does. Writes a 64-entry palette (colour of entry e is built from green = (e >> 3) and 7, red = e and 7, blue = (green + red) and 3), draws attributes that use every entry, then turns ULAplus on for about 3 seconds and off for about 3 seconds, repeatedly.
- Layout. The screen has four bands of six character rows. Each band uses one of the four attribute palettes (the top band uses attribute bits 7,6 = 00, then 01, 10, 11). Each band has eight groups of four columns. Each group has two cells of ink colour, then two cells of paper colour.
- Correct picture with ULAplus on. Sixty-four distinct colours. In every band the red level rises from left to right across the eight groups. The green level rises from the top band to the bottom band, and in each group the paper half has a slightly higher green level than the ink half. The blue level is (green + red) mod 4, so it cycles from entry to entry. The border changes from black to a dark colour (entry 8) when ULAplus turns on, as in the ULAplus specification.
- Correct picture with ULAplus off. In every band, each group shows one solid standard colour, because ink and paper are equal. Left to right the groups are black, blue, red, magenta, green, cyan, yellow, white. The second and fourth bands are BRIGHT. The border is black.
- HDMI and the Spectrum must change together at each toggle.

## shadow_flip

- Machines. 128K, +2, +2A, +3. Not 48K.
- What it does. Draws Bars in bank 5 (through 0x4000) and Bands in bank 7 (bank 7 paged at 0xC000). Then it flips port 0x7FFD bit 3 about every second.
- Correct picture. Bars for about 1 second, Bands for about 1 second, repeating. Both screens flip at the same moment.

## bank5_c000

- Machines. 128K, +2, +2A, +3.
- What it does. Pages bank 5 at 0xC000 and draws only through addresses 0xC000 and up. Bank 5 is also the normal screen. The program first clears the screen through 0x4000.
- Correct picture. A blank white screen at the start, then Bands for about 2 seconds, then Bars for about 2 seconds, repeating. If the board ignores writes at 0xC000 while bank 5 is paged there, HDMI stays blank white while the Spectrum shows the pictures.

## plus3_special

- Machines. +2A and +3 only. The other machines do not have port 0x1FFD paging, and this program would hang there.
- What it does. Uses special paging to draw bank 7 two ways, then flips the screen between bank 5 and bank 7.
  - Special layout with bits 2,1 = 1,1 puts bank 7 at 0x4000. The program draws the top third of bank 7 (rows 0 to 7, the Bars picture) through 0x4000.
  - Special layout with bits 2,1 = 0,1 puts bank 7 at 0xC000. The program draws the lower two thirds of bank 7 (rows 8 to 23, the Bands picture, continuing the row numbering) through 0xC000.
  - It then returns to normal paging and flips port 0x7FFD bit 3 about every second. Bank 5 holds a plain white screen that is never written after the first clear.
  - The program image exists in both bank 2 and bank 6, at the same addresses, so the code keeps running when the special layouts replace bank 2 at 0x8000 with bank 6. There is no ROM in special mode, so interrupts stay off.
- Correct picture. A plain white screen for about 1 second, then a screen with Bars in the top third and Bands in the lower two thirds, for about 1 second, repeating. If the top third is missing or shows garbage, the write through 0x4000 in the 1,1 layout did not reach bank 7 on the board. If the lower two thirds are missing or show garbage, the write through 0xC000 in the 0,1 layout did not. Bank 7 holds random data after power on, so a missing write can show as noise.
- The program flips the screen only while paging is normal. Documentation does not say whether the screen bank still follows port 0x7FFD bit 3 in special mode.

## lock_test

- Machines. 128K, +2, +2A, +3. Not 48K.
- What it does. Draws Bars in bank 5 and Bands in bank 7. Then it writes port 0x7FFD with bit 5 set (the paging lock). After that, every second it tries to flip bit 3, both ways. The lock stays until reset.
- Correct picture. Bars, and only Bars, for ever, on both the Spectrum and HDMI. Bands never appear. If Bands appear on either screen, the lock failed there. HDMI must also keep Bars. The board should queue no event for the locked writes. Reset the Spectrum to run the test again.
