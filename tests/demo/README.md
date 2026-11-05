# Gigascreen and ULAplus demo

A tape program for the 128K, +2, +2A and +3. It shows one photograph, a blue-and-gold macaw, in three versions made for three display methods of SP-Pie: Gigascreen, double high and ULAplus. A menu sets every mode by key. The program does not detect anything.

The program needs the 48K ROM, because it takes its character set from there. It writes to ports only and never reads them. [docs/control-ports.md](../../docs/control-ports.md) defines the SP-Pie ports.

## The picture

| | |
| --- | --- |
| Title | Ara ararauna portrait Rhodes |
| Source | https://commons.wikimedia.org/wiki/File:Ara_ararauna_portrait_Rhodes.jpg |
| Author | Jebulon (https://commons.wikimedia.org/wiki/User:Jebulon) |
| Licence | CC0 1.0 Universal, public domain dedication (https://creativecommons.org/publicdomain/zero/1.0/) |
| File in this folder | `source/macaw.jpg`, the 960 x 826 pixel version, downloaded from https://upload.wikimedia.org/wikipedia/commons/thumb/4/45/Ara_ararauna_portrait_Rhodes.jpg/960px-Ara_ararauna_portrait_Rhodes.jpg |

CC0 allows the copy in this repository. `convert.py` crops the picture to the shape of the screen, adds a little colour, and converts it. The converter is our own code. It uses numpy and Pillow, and no other converter.

## Build and run

```
python3 convert.py     # only to regenerate data/ and preview/
./build.sh
```

`convert.py` needs Python 3 with numpy and Pillow. It takes about half a minute. The files in `data/` are kept in the repository, so `build.sh` does not need Python. `build.sh` needs `sjasmplus`. The script uses `sjasmplus` on the path. Set `SJASMPLUS=/path/to/sjasmplus` to use another copy. The output is `build/demo.tap`. The `build/` folder is not tracked by git.

Play the tape, or give it to a tape emulator. On a 128K, +2, +2A or +3, choose Tape Loader from the menu, so that the 48K ROM is paged in. The tape holds a BASIC loader, three picture blocks (about 34 KB) and the program, so loading takes several minutes at normal tape speed. The picture blocks load into RAM banks 1, 3 and 4 and the program copies them to the screens. Q returns to BASIC.

## Pictures

Each picture fills the top 22 character rows. The last two rows hold the title and a one-line hint.

| Key | Picture | Made for |
| --- | --- | --- |
| 1 | Gigascreen pair. Bank 5 and bank 7 each have their own attributes and pixels. Each pixel is one of the four colours that the two banks make together. Per 8 x 8 cell, the converter picks ink, paper and bright for both banks, and the pixel patterns, so that the average of the two screens is close to the photograph. | Gigascreen with 50/50 blend or CRT blend, or two screens flipped on a CRT |
| 2 | Double-high picture, 256 x 352. Bank 5 holds the even lines and bank 7 the odd lines of the picture. Both banks have the same attributes, because SP-Pie double high gives both lines of a pair the attribute of the bank on show. Each cell is 8 x 16 picture lines with two colours. | Double high |
| 3 | ULAplus picture. One 64-colour palette (4 tables of 16), made from the photograph. Each cell has its own table, ink and paper. Both banks hold the same screen. | ULAplus palette mode on |

Keys 1 to 3 also set the modes that suit the picture, and send them to the board:

| Key | Flip | Gigascreen | Show as | ULAplus |
| --- | --- | --- | --- | --- |
| 1 | on | Always | 50/50 blend | off |
| 2 | on | Always | Double high | off |
| 3 | off | Off | 50/50 blend | on |

The program starts in picture 1.

## Keys

The ROM keyboard interrupt reads the keys. A key that you hold repeats after about three quarters of a second. The menu is hidden at the start. M shows and hides it. The menu uses rows 18 to 23, over the bottom of the picture, and has the 128K menu style: a black title bar with a rainbow at its right end, and white rows. Its last two rows show the current settings. The program keeps these values itself. SP-Pie does not report back, so the shown settings are what the program sent.

| Key | Action |
| --- | --- |
| 1, 2, 3 | Picture, as above. |
| 4 | Show primary: bank 5 only, no flipping. |
| 5 | Show secondary: bank 7 only, no flipping. |
| 6 | Gigascreen flip on or off. On: the program writes 0x7FFD bit 3 every frame, right after the frame interrupt, so the display alternates between bank 5 and bank 7. Off: back to the bank of key 4 or 5. |
| 7 | SP-Pie Gigascreen: Off, Auto, Always (feature 3 bits 1 and 0: 00, 01, 10). |
| 8 | SP-Pie show as: 50/50 blend, CRT blend, Double high (bits 3 and 2: 00, 01, 10). |
| 9 | SP-Pie double high top: Normal (bank 5 on the upper line) or Shadow (bank 7 upper). Bit 6. |
| 0 | ULAplus on or off. |
| M | Show or hide the menu. |
| Q | Quit to BASIC. |

The SP-Pie keys send `OUT 0x043B,3` and then `OUT 0x053B,value`. Bits 5 and 4 stay at 01, detection per page. The program never sends the reserved mode 11 or the reserved methods. It sends the default value 0x11 when it ends, and it switches ULAplus off when it ends.

## What to look for

The `preview/` folder has what each view should look like. The converter writes these files. Each is scaled up, 2 x 2 unless noted.

| File | Content |
| --- | --- |
| `source.png` | The source picture cropped and scaled to 256 x 176. |
| `giga_bank5.png`, `giga_bank7.png` | The two Gigascreen screens alone. Each looks coarse and wrong on its own. This is what keys 4 and 5 show without SP-Pie mixing. |
| `giga_blend_50_50.png` | The 50/50 blend of the two screens: the picture with SP-Pie in 50/50 mode, or on a CRT. |
| `giga_blend_crt.png` | The CRT blend, 5/8 of one screen and 3/8 of the other. |
| `dhigh_256x352.png` | The double-high picture, 256 x 352, scaled to twice the width only. |
| `dhigh_bank5.png`, `dhigh_bank7.png` | The two screens of the double-high picture alone: the even lines and the odd lines. |
| `ulaplus.png`, `ulaplus_palette.png` | The ULAplus picture, and its 64 colours in table order. |

On SP-Pie:

| Setting | What you should see |
| --- | --- |
| Picture 1, defaults | The macaw, steady, with fine dither texture. SP-Pie blends the two banks, as `giga_blend_50_50.png` shows. |
| Key 8 once (CRT blend) | The same, with the weight on the newest frame. Compare with `giga_blend_crt.png`. |
| Key 7 until Off | No mixing. The two banks alternate every frame, 50 times a second. On HDMI this flickers. |
| Key 6 off, key 4 or 5 | One bank alone: `giga_bank5.png` or `giga_bank7.png`. |
| Picture 2, defaults | The macaw at 256 x 352, as `dhigh_256x352.png` shows. The detail is finer than picture 1, with fewer colours. |
| Picture 2, key 9 (Shadow) | The two lines of every pair swap places, and the picture looks wrong: a notch at every edge. Key 9 again makes it right. |
| Picture 2, key 8 until 50/50 | The two interleaved screens blend in one 256 x 176 picture, with less detail. |
| Picture 3, ULAplus on | The macaw in 64 palette colours, as `ulaplus.png` shows. |
| Picture 3, key 0 (ULAplus off) | The same screen with the standard colours. The attribute bits for FLASH and BRIGHT now select the table, so the colours are wrong and the picture is garish. This is the difference that the ULAplus picture shows. |

## MAME

`mame/run.sh` runs the tape in MAME with no window (`-video none -sound none`), presses the keys through the emulated keyboard matrix and saves a screenshot after each. `mame/demo_keys.lua` is the key script. Set `MAME` to a MAME binary with the `spec128` and `specpls3` drivers (the default is `mame` on the path), and `ROMPATH` to a MAME rompath that holds their ROMs. The tape takes about five minutes of emulated time to load, so the run starts its keys at 300 seconds:

```
mame/run.sh spec128 OUTDIR 300
mame/run.sh specpls3 OUTDIR 300
```

MAME shows:

- All three pictures in both banks, the menu, the title and hint, and every key.
- Which bank is on show. With flipping on, consecutive frames alternate between bank 5 and bank 7.
- The return to BASIC.

MAME does not show:

- ULAplus. MAME's 128K machines ignore ports 0xBF3B and 0xFF3B, so the ULAplus picture always looks as it does with ULAplus off, and the palette is not checked.
- Any SP-Pie mode. MAME has no feature ports and no mixing or double high. One screenshot is one frame, so a flipping picture shows as one bank.
- That the flip and the frame interrupt line up on the board.

On the +3 (`specpls3`) the run ends with "Drive not ready" in MAME, because it has no disk drive. The sound tests end the same way.

## Ports and paging

- 0x7FFD is written with the ROM bit of the system variable BANKM kept, a RAM bank at 0xC000, and bit 3 for the screen. The program does not touch 0x1FFD. When it ends, it writes the BANKM value back.
- The ROM interrupt routine writes BANKM to port 0x7FFD at every frame interrupt. A bare `OUT 32765` from BASIC is undone at the next interrupt. The BASIC loader therefore does `POKE 23388,n` (BANKM) together with `OUT 32765,n` before each picture block, and the flip loop writes bit 3 right after the interrupt.
- The program writes the RAM banks only with interrupts off. With interrupts on, the pictures lost parts of the shadow screen on the +3 in MAME. The flip loop runs with interrupts on, after a `HALT`.
- The ULAplus ports 0xBF3B and 0xFF3B have A15 = 1, so they cannot page a 128K. The ports 0x043B and 0x053B have A15 = 0 and A1 = 1, which keeps them out of the 0x7FFD decode.
- Quit clears the shadow screen of bank 7, because the program wrote to it. It does not clear banks 1, 3 and 4, which hold the picture blocks.
