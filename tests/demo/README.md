# Gigascreen and ULAplus demo

A tape program for the 128K, +2, +2A and +3. It shows two pictures that you can look at in every display mode of SP-Pie, and it has a menu that sets each mode by key. It does not detect anything: every setting is a key you press.

The program needs the 48K ROM, because it takes its character set from there. It writes to ports only, and never reads them. [docs/control-ports.md](../../docs/control-ports.md) defines the SP-Pie ports.

## Build and run

```
./build.sh
```

This needs `sjasmplus`. The script looks first at `/u/GIT/ZX/bin/sjasmplus`, then at `sjasmplus` on the path. Set `SJASMPLUS=/path/to/sjasmplus` to use another copy. The output is `build/demo.tap`. The `build/` folder is not tracked by git.

Play the tape, or give it to a tape emulator. On a 128K, +2, +2A or +3, choose Tape Loader from the menu, so that the 48K ROM is paged in. A one-line BASIC loader runs `CLEAR 32767`, loads the code at 32768 and starts it. Q returns to BASIC.

`tables.inc` holds the data tables (line patterns and palettes). `gen_tables.py` writes it. `build.sh` does not run the generator.

## The pictures

Both pictures are drawn by the program, once into bank 5 (the normal screen) and once into bank 7 (the shadow screen). Bank 7 is paged in at 0xC000 for the whole run.

**Picture 0, Gigascreen.** Key 8 selects it. It starts with this one.

- Top eight rows: a colour chart. Row a, swatch b has colour a as the paper in bank 5 and colour b as the paper in bank 7. Swatches 0 to 7 of a row use normal colours in bank 7, swatches 8 to 15 use bright colours. Bank 5 alone shows eight plain bands. Bank 7 alone shows sixteen vertical bars. Shown in turn, each swatch is a mix of its two colours.
- Below the chart: white diagonal lines on blue, two pixels wide, in 32-pixel blocks that slope one way, then the other. Picture line 2n is screen line n of bank 5, and picture line 2n+1 is screen line n of bank 7. Together the banks make a 256 x 256 pattern of clean 45 degree lines. Each bank alone has only every second line, so its lines look steeper and ragged.

**Picture 1, ULAplus.** Four bands of four rows, one for each of the four colour tables of the 64-colour palette (the table is chosen by the FLASH and BRIGHT bits of the attribute). The program writes only the palette mode: group 0x40 with bit 0 set. It does not use any other ULAplus mode.

- With ULAplus off, each band is eight solid bars of the standard colours. The bands for tables 1 and 3 are bright. FLASH changes nothing visible, because ink and paper are the same in each cell.
- With ULAplus on, each band is a smooth ramp of 16 colours: a hue sweep, a grey ramp, a fire ramp and a sea ramp.

Picture 1 is drawn the same in both banks.

With ULAplus on, picture 0 looks as it does with ULAplus off. The program loads a palette with the 48K colours for it.

## Keys

The ROM keyboard interrupt reads the keys. A key that you hold repeats after about three quarters of a second.

| Key | Action |
| --- | --- |
| 1 | Show primary: bank 5 only, no flipping. |
| 2 | Show secondary: bank 7 only, no flipping. |
| 3 | Gigascreen on or off. On: the program writes 0x7FFD bit 3 every frame, right after the frame interrupt, so the display alternates between bank 5 and bank 7. Off: back to the bank of key 1 or 2. |
| 4 | SP-Pie Gigascreen: Off, Auto, Always (feature 3 bits 1 and 0: 00, 01, 10). |
| 5 | SP-Pie show as: 50/50 blend, CRT blend, Double high (bits 3 and 2: 00, 01, 10). |
| 6 | SP-Pie double high top: Normal (bank 5 on the upper line) or Shadow (bank 7 upper). Bit 6. |
| 7 | ULAplus picture on or off. |
| 8 | Picture: Gigascreen or ULAplus. Draws both banks again and loads that picture's palette. |
| M | Hide or show the menu. |
| Q | Quit to BASIC. |

The SP-Pie keys send `OUT 0x043B,3` and then `OUT 0x053B,value`. Bits 5 and 4 stay at 01, detection per page. The program never sends the reserved mode 11 or the reserved methods. It sends the default value 0x11 at the start, and again when it ends, and it switches ULAplus off when it ends.

The menu uses the last six rows of the screen, in the style of the 128K menu: a black title bar with a rainbow at its right end, then white rows. The last two rows show the current settings: view (Primary, Secondary, Flip), ULAplus, picture, Gigascreen, show as, and double high top. The program keeps these values itself. SP-Pie does not report back, so the shown settings are what the program sent. Key M removes the menu, so that the whole picture shows.

## What to look for on SP-Pie

Start from the defaults: Gigascreen Auto, 50/50 blend, double high top Normal, ULAplus off, picture 0.

| Setting | What you should see |
| --- | --- |
| Key 1 | Bank 5 alone: eight colour bands, and the diagonal lines in the lines of bank 5 only. No flicker. |
| Key 2 | Bank 7 alone: sixteen colour bars, and the lines of bank 7 only. |
| Key 3 on, Gigascreen Off (key 4 until Off) | The picture of bank 5 and bank 7 alternate every frame, 50 times a second. On HDMI this flickers. SP-Pie does not mix. |
| Key 3 on, Gigascreen Auto, 50/50 | After 2 frames in a row that flip, SP-Pie mixes every line (per page detection). Each chart swatch becomes a colour half way between its two colours, steady, with no flicker. The lines become a softer, wider pattern. |
| Key 3 on, Gigascreen Always, 50/50 | The same, from the first frame, with no wait for detection. |
| CRT blend | As 50/50, but the newest frame counts 5/8 and the one before 3/8. The mix leans towards the frame that was drawn last. |
| Double high | No blending. The picture is 256 x 384: each display line becomes two lines, bank 5 above and bank 7 below. The diagonal lines show as one clean pattern, with the full number of lines. Both lines of a cell use the colours of the bank on show, so the chart swatches are plain, not mixed. |
| Double high top Shadow | The two lines of every pair swap places. The diagonal lines now show a notch at every step, because the lines are in the wrong order. Set Normal again for the clean pattern. |
| Key 3 off with Gigascreen Always and Double high | Not verified. The mode shows the bank 7 pixel bytes as they are at the end of the frame, so with no flipping it should still show both banks as two lines. Check this on the board. |
| Picture 1, key 7 | Off: standard bars. On: four smooth ramps of colour. |

## What MAME can and cannot show

`mame/run.sh` runs the tape in MAME with no window (`-video none -sound none`), presses the keys through the emulated keyboard matrix and saves a screenshot after each. `mame/demo_keys.lua` is the key script. MAME comes from the scratchpad build described in `../sound/README.md`. Example:

```
mame/run.sh spec128 OUTDIR
mame/run.sh specpls3 OUTDIR
```

MAME shows:

- Both pictures, drawn into both banks, and the menu text and the settings it shows.
- Which bank is on show. A run with key 3 on saves consecutive frames that alternate between the picture of bank 5 and the picture of bank 7.
- Every key, and the return to BASIC.

MAME does not show:

- ULAplus. MAME's 128K machines ignore ports 0xBF3B and 0xFF3B, so picture 1 always looks as it does with ULAplus off. The ramps are not verified in any emulator here.
- Any SP-Pie mode. MAME has no feature ports and no mixing. A single screenshot shows one frame, so flipping looks like one picture.
- That the frame interrupt, the flip and SP-Pie line up. This needs the board.

On the +3 (`specpls3`) the program ends with "Drive not ready, 10:3" in MAME. The sound tests end the same way in this MAME, because it has no disk drive.

## Notes on ports and paging

- 0x7FFD is written with the ROM bit of the system variable BANKM kept, bank 7 at 0xC000, and bit 3 for the screen. The program does not touch 0x1FFD. When it ends, it writes the BANKM value back.
- The program writes bank 7 only with interrupts off. With interrupts on, a +3 in MAME lost part of the shadow screen. It runs the flip loop with interrupts on, after a `HALT`.
- The ULAplus ports 0xBF3B and 0xFF3B have A15 = 1, so they cannot page a 128K. The ports 0x043B and 0x053B have A15 = 0 and A1 = 1, which keeps them out of the 0x7FFD decode.
- Quit clears the shadow screen of bank 7, because the program wrote to it.
