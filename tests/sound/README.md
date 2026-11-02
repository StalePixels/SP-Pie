# Sound and control port tests

Small Z80 programs that test the sound and control ports of the SP-Pie board. Each program prints on the screen what it plays, so you can follow along by ear.

The ports under test:

| Port | Use |
| --- | --- |
| 0xFFFD, 0xBFFD | AY register select and data. |
| 0xFFFD with value 0xFF or 0xFE | TurboSound chip select. 0xFF selects chip 0, 0xFE selects chip 1. |
| 0xFE bit 4 | Beeper. |
| 0xFB, 0xDD, 0xDF | Covox DACs. 8-bit unsigned samples, 0x80 is the centre. |
| 0x043B | Control select. |
| 0x053B | Control parameter. |

Control select 0, bit 0: HUD on. Control select 1: bit 0 AY, bit 1 TurboSound, bit 2 beeper, bit 3 Covox 0xFB, bit 4 Covox 0xDD, bit 5 Covox 0xDF. The defaults are HUD on, AY, TurboSound and beeper on, and Covox off. A Spectrum reset restores the defaults.

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

**ESXDOS.** Copy the files from `build/dot/` to the `BIN` folder of the SD card. Type the name, for example `.ay`. The dot commands run at 0x2000. They print with the ROM and leave interrupts on.

Every test clears the screen, prints its title and one line for each sound, and returns to BASIC when it is done. Interrupts are off while a beeper or Covox wave plays, so the keyboard does not respond for that time. Each test sets the control ports to their defaults at the start and the end.

## spctl

ESXDOS dot command only. It sets or reads the control ports.

```
.spctl              print the usage
.spctl 1 7          select 1, parameter 7: AY, TurboSound and beeper on
.spctl 1 $38        Covox on all three ports, the others off
.spctl 0 0          HUD off
.spctl 1            read the parameter port for select 1
```

Numbers are decimal, or hex with a `$` or `0x` prefix. With only a select value, `spctl` writes the select and prints what the parameter port returns. The parameter port must return a value for this to show anything useful.

From BASIC, without `spctl`, write the ports directly. Port 0x043B is 1083 and port 0x053B is 1339.

```
OUT 1083,1: OUT 1339,7
```

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

Plays a different tone on each TurboSound chip. Chip 0 plays 440 Hz and chip 1 plays 880 Hz.

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

Plays a sine wave of 32 samples on each Covox port. The samples run from 13 to 243 around the centre 0x80. For each port the test plays the wave twice. First the Covox is disabled in control select 1, and you must hear nothing. Then it is enabled, and you hear the wave. Only the Covox under test is enabled. AY, TurboSound and the beeper are off while it plays.

| Port | N | Frequency on a 48K |
| --- | --- | --- |
| 0xFB | 5 | about 919 Hz |
| 0xDD | 10 | about 595 Hz |
| 0xDF | 2 | about 1368 Hz |

One sample takes 53 plus 13 times N T-states. One wave cycle is 32 samples plus 31 T-states. Each wave plays for 1.5 seconds.

Port 0xDD needs care on a 128K. The 128K decodes the low byte 0xDD with address line A15 low as the memory paging port, and with A15 high as the AY. The test writes to port 0xBFDD, which is an AY data write, and first selects AY register 15. That register has no function on an AY-3-8912, so the samples change nothing. A write to 0x00DD instead pages RAM and ROM and crashes the program.

## hud

Switches the HUD off and on through control select 0. It shows three rounds of "HUD off" for 2 seconds and "HUD on" for 2 seconds. The HUD on the HDMI picture must follow the printed text. The HUD is on at the end.

## Reference sounds from MAME

The `mame/` folder holds scripts that run each `.tap` in a headless MAME and measure the recorded sound. MAME needs the drivers `spectrum` and `spec128`, and the ROMs for them.

```
MAME=/path/to/mame ROMPATH=/path/to/roms mame/run_refs.sh /path/to/output
```

The output folder gets a WAV file, a frequency list and a final screenshot for each run. No window opens, and MAME produces no sound on the speakers. The 128K runs use the `ay_turbosound` slot option.

Facts about MAME to keep in mind:

- MAME has no Spectrum Covox device. Its Covox run is silent. `mame/covox_expected.py` writes the expected sound from the test's own table and timing.
- MAME selects the TurboSound chips the other way round. It treats 0xFF as the second chip and 0xFE as the first. Compare the turbosound test against the board, not against MAME, for the order of the two pitches.
- `mame/analyse.py FILE.wav` prints the strongest frequency over time for any WAV.
