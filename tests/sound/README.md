# Sound and control port tests

Small Z80 programs that test the sound and control ports of the SP-Pie board. Each program prints on the screen what it plays, so you can follow along by ear.

The ports under test. The control ports are write-only. [docs/control-ports.md](../../docs/control-ports.md) defines them.

| Port | Use |
| --- | --- |
| 0xFFFD, 0xBFFD | AY register select and data. |
| 0xFFFD with value 0xFE or 0xFF | TurboSound chip select. 0xFE selects chip 0, the chip a reset selects. 0xFF selects chip 1. |
| 0xFE bit 4 | Beeper. |
| 0xFB, 0xDD, 0xDF | Covox DACs. 8-bit unsigned samples, 0x80 is the centre. |
| 0x043B | Feature select. |
| 0x053B | Feature data. |

Write the feature number to the select port, then the value to the data port. The features:

| Feature | Use | Default |
| --- | --- | --- |
| 0x00 | Reserved. | |
| 0x01 | Attached devices. Bit 0 AY, bit 1 TurboSound, bit 2 beeper, bit 3 Covox 0xFB, bit 4 Covox 0xDD, bit 5 Covox 0xDF, bit 6 Pentagon Gigascreen (port 0xEFF7). | 0x07 |
| 0x02 | Border. Bit 0 extends the border to 16:9. | 0x00 |
| 0x03 | Display. The bits are in the list below. | 0x11 |
| 0xFF | Debug. Bit 0 shows the HUD. | 0x01 |

Feature 3, display:

| Bits | Use |
| --- | --- |
| 0 | Automatic interlace detection. Default on. |
| 1 | Mixing locked on, whatever the detection says. |
| 3 and 2 | Mode. 00 is the 50/50 mix, 01 the CRT-style mix, 10 double high: both screens interleaved as alternate lines, 256 x 384 at 50 fps. 11 is line alternate. |
| 5 and 4 | Detection method. 00 per line, 01 per page. Default per page. 10 and 11 are reserved and must not be sent. |
| 6 | First bank of the double-high mode. 0 is bank 5 (primary) first, 1 is bank 7 (secondary) first. Default 0. |
| 7 | Reserved. |

A Spectrum reset restores the defaults.

## Build

```
./build.sh
```

This needs `sjasmplus`. The script looks first at `/u/GIT/ZX/bin/sjasmplus`, then at `sjasmplus` on the path. Set `SJASMPLUS=/path/to/sjasmplus` to use another copy. Each program is built twice from the same source. The `build/` folder is not tracked by git.

| Folder | Content |
| --- | --- |
| `build/tap/` | One `.tap` file for each test, for the 48K, 128K, +2 and +3. |
| `build/dot/` | One ESXDOS dot command for each test, and `spctl`. |
| `build/lst/` | Assembler listings. |

## Run

**Tape.** Play the `.tap` file into the tape input, or give it to a tape emulator. Type `LOAD ""` and press Enter. On a 128K, +2, +2A or +3, choose Tape Loader from the menu. The 48K ROM must be paged in, because the programs print with it. A one-line BASIC loader runs `CLEAR 32767`, loads the code at 32768 and starts it.

**ESXDOS.** Copy the files from `build/dot/` to the `BIN` folder of the SD card. Type the name, for example `.ay`. The dot commands run at 0x2000 and leave interrupts on. While a dot command runs, ESXDOS has its own ROM at 0x0000 to 0x1FFF. So the dot builds call every 48K ROM routine (printing, clear screen) with `RST 0x18` followed by the routine address, the ESXDOS ROM call. The tape builds call the ROM directly.

Every test clears the screen, prints its title and one line for each sound, and returns to BASIC when it is done. Interrupts are off while a beeper or Covox wave plays, so the keyboard does not respond for that time. Each test sets the features to their defaults at the start and the end.

## spctl

ESXDOS dot command only. It sets the features.

```
.spctl              print the usage
.spctl 1 7          devices: AY, TurboSound and beeper on
.spctl 1 $38        devices: Covox on all three ports, the others off
.spctl 255 0        HUD off
.spctl 2 1          border extension on
.spctl 1            select feature 1 only
```

Numbers are decimal, or hex with a `$` or `0x` prefix. The ports are write-only, so `spctl` cannot read a feature back.

From BASIC, without `spctl`, write the ports directly. Port 0x043B is 1083 and port 0x053B is 1339.

```
OUT 1083,255: OUT 1339,0
```

## panel

A control panel for the board, drawn like the Spectrum 128K boot menu: a black title bar with a rainbow at the right end, a white menu box, and a cyan bar on the selected row. Every change goes to the board at once, as the feature number to port 0x043B and then the value to port 0x053B.

The main page has the devices, the border and the debug item. The display settings (feature 3) are on the GIGASCREEN page, which G opens from the main page.

Main page:

| Row | Feature | Key |
| --- | --- | --- |
| AY chip | 1, bit 0 | 1 |
| TurboSound | 1, bit 1 | 2 |
| Beeper | 1, bit 2 | 3 |
| Covox 0xFB | 1, bit 3 | 4 |
| Covox 0xDD | 1, bit 4 | 5 |
| Covox 0xDF | 1, bit 5 | 6 |
| Pentagon Gigascreen (port #EFF7). The screen shows "Pentagon Giga". | 1, bit 6 | 7 |
| Extend to 16:9 | 2, bit 0 | 8 |
| HUD | 0xFF, bit 0 | H |
| Open the GIGASCREEN page | | G |

GIGASCREEN page. Each row shows its current value on a line under its label.

| Row | Values | Feature 3 | Key |
| --- | --- | --- | --- |
| Gigascreen | Off, Auto, Always | Off: bits 0 and 1 clear. Auto: bit 0 set, bit 1 clear. Always: bit 1 set, bit 0 clear. | 1 |
| Show as | 50/50 blend, CRT blend, Double high, Line alternate | bits 3 and 2: 00, 01, 10, 11 | 2 |
| Auto watches | Whole screen, Each line | bits 5 and 4: 01, 00 | 3 |
| Double high top | Normal screen, Shadow screen | bit 6: 0 (bank 5 first), 1 (bank 7 first) | 4 |
| Back to the main page | | | G, SPACE or BREAK |

Each key steps its row to the next value and wraps round: Off, Auto, Always, Off for the first row. The panel never sends the reserved methods or bit 7.

- Cursor up and down (CAPS SHIFT with 7 and 6) move the cyan bar, and repeat while held. ENTER toggles or steps the selected row.
- A row key moves the bar to that row and acts at once.
- S sends every value again. R restores the documented defaults and sends them. Q quits. All three work on both pages.

The ports are write-only, so the panel cannot read the board. It keeps its own copy of each value, starting at the documented defaults, and shows that copy. A panel that started after another program changed a feature, or after a board that did not reset, can differ from the board. Press R or S to make them agree.

The panel reads the keyboard itself, by reading port 0xFE with each half-row address, once per frame. A key must read the same on two scans in a row. It needs no interrupt and no ROM keyboard routine, so it works the same in the tape and dot builds.

The panel writes only to ports 0x043B, 0x053B and 0xFE, so none of its writes can page a 128K or reach the AY. It draws with the attribute memory, a few pixel bytes for the rainbow, and the 48K ROM print routine. It needs the 48K ROM paged in, as the other tests do. The tape version loads from the Tape Loader entry on a 128K, +2 or +3.

## ay

Plays the AY chip. A tone has the frequency of the AY clock divided by 16 times the period. The 128K AY clock is 1.7734 MHz.

1. Silence, then channel A alone at 440 Hz (period 252).
2. Channel B alone at 880 Hz (period 126).
3. Channel C alone at 1760 Hz (period 63).
4. All three together.
5. Channel A at 440 Hz with volume 15, 11, 7 and 3. Each step is quieter.
6. Noise with period 4 (a high hiss), then period 20 (a lower rumble).
7. A 440 Hz tone with noise added.
8. Envelope shapes on channel A, each for 1.5 seconds. The envelope repeats about 4.6 times each second.
   - Shape 8: volume falls and jumps back up, repeating.
   - Shape 10: volume falls, rises, falls again, starting loud.
   - Shape 12: volume rises and drops back, repeating.
   - Shape 14: volume rises, falls, rises again, starting quiet.
   - Shape 0: one fall, then silence.
   - Shape 4: one rise, then silence.
   - Shape 11: one fall, then full volume held.
9. Mixer with tone and noise both off: silence.

## turbosound

Plays a different tone on each TurboSound chip. Chip 0 plays 440 Hz and chip 1 plays 880 Hz. The test selects chip 0 with 0xFE and chip 1 with 0xFF, the NedoPC order. A reset selects chip 0.

1. Silence on both chips.
2. Chip 0 alone: 440 Hz.
3. Chip 1 alone: 880 Hz.
4. Both chips: 440 Hz and 880 Hz together, an octave apart.
5. Chip 0 noise with the 880 Hz tone of chip 1.
6. Chip 0 noise alone.
7. Chip 1 alone on channel C: 1760 Hz.

If the chip select is wrong you hear the wrong pitch in steps 2 and 3. The pitches are swapped, or both steps sound the same.

## beeper

Writes square waves to port 0xFE bit 4. One period takes 26 times N plus 63 T-states, where N is the delay count of each half.

| Wave | N | Frequency on a 48K | Frequency on a 128K |
| --- | --- | --- | --- |
| 1 | 255 | about 523 Hz | about 530 Hz |
| 2 | 133 | about 994 Hz | about 1007 Hz |
| 3 | 64 | about 2026 Hz | about 2054 Hz |

Each wave plays for 1.5 seconds, with a short pause between them. The 128K CPU clock is 1.3 percent faster, so the pitch is slightly higher.

## covox

Plays a sine wave of 32 samples on each Covox port. The samples run from 13 to 243 around the centre 0x80. For each port the test plays the wave twice. First the Covox is disabled in feature 1, and you must hear nothing. Then it is enabled, and you hear the wave. Only the Covox under test is enabled. AY, TurboSound and the beeper are off while it plays.

| Port | N | Frequency on a 48K |
| --- | --- | --- |
| 0xFB | 5 | about 919 Hz |
| 0xDD | 10 | about 595 Hz |
| 0xDF | 2 | about 1368 Hz |

One sample takes 53 plus 13 times N T-states. One wave cycle is 32 samples plus 31 T-states. Each wave plays for 1.5 seconds.

Port 0xDD needs care on a 128K. The 128K decodes the low byte 0xDD with address line A15 low as the memory paging port, and with A15 high as the AY. The test writes to port 0xBFDD, which is an AY data write, and first selects AY register 15. That register has no function on an AY-3-8912, so the samples change nothing. A write to 0x00DD instead pages RAM and ROM on a 128K and crashes the program. The AY is off while the Covox plays, so the board ignores the AY write.

## hud

Switches the HUD off and on through feature 0xFF. It shows three rounds of "HUD off" for 2 seconds and "HUD on" for 2 seconds. The HUD on the HDMI picture must follow the printed text. The HUD is on at the end.

## border

Draws eight horizontal stripes in the border, then switches the border extension (feature 2, bit 0) off and on. Three rounds of four seconds off and four seconds on.

- The stripes run from the top: black, blue, red, magenta, green, cyan, yellow, white. They are not exact, because the program counts T-states and a 128K line is slightly longer than a 48K line.
- With the extension off, the stripes show around the 352 by 270 picture only.
- With the extension on, each line of the areas left and right of the picture takes the colour of that line's edge pixel, so the stripes fill the 16:9 screen.
- The extension is off at the end.

The `mame/panel_keys.lua` script runs the panel in MAME and presses CAPS SHIFT with 6 and 7, and ENTER, through the emulated keyboard matrix. The `mame/panel_pages.lua` script opens and closes the GIGASCREEN page, presses each of its keys and takes a screenshot after each one. It prints the panel's own state table at each screenshot. Set `STATE_ADDR` to the address of the label `state` in `build/lst/panel-tap.lst`. Use both with `-autoboot_script`.

## Reference sounds from MAME

The `mame/` folder holds scripts that run each `.tap` in a headless MAME and measure the recorded sound. MAME needs the drivers `spectrum` and `spec128`, and the ROMs for them.

```
MAME=/path/to/mame ROMPATH=/path/to/roms mame/run_refs.sh /path/to/output
```

The output folder gets a WAV file, a frequency list and a final screenshot for each run. No window opens, and MAME produces no sound on the speakers. The 128K runs use the `ay_turbosound` slot option.

Facts about MAME to keep in mind:

- MAME has no Spectrum Covox device. Its Covox run is silent. `mame/covox_expected.py` writes the expected sound from the test's own table and timing.
- MAME selects the TurboSound chips in the same order as the test: 0xFE is the first chip and 0xFF the second.
- `mame/analyse.py FILE.wav` prints the strongest frequency over time for any WAV.
