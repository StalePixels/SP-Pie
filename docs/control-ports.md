# Control ports

A Spectrum program sets SP-Pie's features by writing to two I/O ports. The CPLD sends these writes to the Pi like every other I/O write ([protocol.md](protocol.md)), and the Pi acts on them. The ports are write-only.

## Ports

| Port | Name | Use |
|---|---|---|
| 0x043B | Feature select | The number of the feature that the next data writes go to. |
| 0x053B | Feature data | The value of the selected feature. |

Both ports are decoded on all 16 address bits. A feature number is one byte. Write the feature number to 0x043B, then the value to 0x053B. The feature stays selected until the next write to 0x043B, so more data writes go to the same feature.

From BASIC, port 0x043B is 1083 and port 0x053B is 1339:

```
OUT 1083,255: OUT 1339,0
```

This selects the debug feature and turns the HUD off.

## Features

| Feature | Name | Bits | Default |
|---|---|---|---|
| 0 | SP-Pie | Reserved. | 0x00 |
| 1 | Attached devices | Bit 0: AY. Bit 1: TurboSound, a second AY. Bit 2: beeper. Bit 3: Covox on port 0xFB. Bit 4: Covox on port 0xDD. Bit 5: Covox on port 0xDF. Bit 6: Pentagon Gigascreen (port #EFF7). Bit 7 is reserved. | 0x07: AY, TurboSound and beeper on, every Covox off, Pentagon Gigascreen off |
| 2 | Border | Bit 0: border extension. Bits 1 to 7 are reserved for later border functions. | 0x00: off |
| 3 | Display | Bit 0: interlace detection. Bit 1: interlace mixing always on. Bits 3 and 2: mixing mode, 00 average, 01 CRT-style, 10 double high, 11 line alternate. Bits 5 and 4: detection method, 00 per line, 01 per page; 10 and 11 are reserved. Bit 6: the first bank in double high and line alternate, 0 bank 5, 1 bank 7. Bit 7 is reserved. | 0x11: detection on, average, per page, bank 5 first |
| 0xFF | Debug | Bit 0: HUD. Bits 1 to 7 are reserved for later debug flags and parameters. | 0x01: HUD on |

Every other feature number is reserved. SP-Pie ignores data writes to a reserved feature and ignores reserved bits. Write 0 to reserved bits: a later version can give them a meaning.

These are the built-in defaults. The `[sp-pie]` section of `config.txt` on the SD card can set others (see [../pi/README.md](../pi/README.md), Settings in config.txt), with one key for each feature:

```
[sp-pie]
feature1=0x07
feature2=0x00
feature3=0x01
feature255=0x01
```

## Reset

When the Spectrum is reset, every feature returns to its default, the one from `config.txt` when it gives one, and feature 0 is selected. The reset also clears both AY chips: every register becomes 0, register 0 is selected in each chip, and chip 0 is selected.

## Attached devices

A change to feature 1 takes effect at the T-state of the data write. A device that is off makes no sound, and SP-Pie ignores writes to its ports.

### AY

Ports 0xFFFD (register select) and 0xBFFD (data). SP-Pie decodes them as Fuse decodes the AY on the 128K and the +2A/+3 (Fuse, `peripherals/sound/ay.c`): A15 = 1 and A1 = 0, with A14 = 1 for the register port and A14 = 0 for the data port. The other address bits are not decoded. A register select keeps the low four bits of the value.

The AY clock is 1,773,450 Hz on every model, half the 128K's Z80 clock. The 48K has no AY of its own. SP-Pie gives it the 128K's AY clock, so a tune plays at the same pitch on every model. The Melodik add-on for the 48K runs its AY at 1.75 MHz (Sinclair Wiki, AY-3-8912), so music written for it plays about 1.3 % higher on SP-Pie.

### TurboSound

With bits 0 and 1 both set there are two AY chips on the same ports. Writing 0xFE to port 0xFFFD selects chip 0, and writing 0xFF selects chip 1. The register select and data writes after that go to the selected chip, and each chip keeps its own selected register. After a reset chip 0 is selected.

This is the order of the NedoPC Turbo Sound FM manual (NedoPC group, 2007, "Principle of operation"). It gives the select value as 11111 followed by three bits, the last of which selects the chip: 0 for the chip that a reset selects, 1 for the other. MAME selects chips the same way. The ZX Spectrum Next uses the opposite order: 0xFF selects its AY 0.

SP-Pie treats only 0xFE and 0xFF as chip selects, as MAME does. Other values from 0xF8 up are register selects.

With bit 1 clear, there is one AY chip. Chip 1 makes no sound, and 0xFE and 0xFF are register selects of chip 0. Clearing bit 1 selects chip 0.

### Beeper

Port 0xFE, bit 4 (EAR) and bit 3 (MIC), as Fuse models the ULA's sound output (Fuse, `peripherals/ula.c` and `sound.c`). Writes to port 0xFE change the border as before, whether the beeper is on or off. When the beeper is turned on, it starts at the level of the last write to port 0xFE.

### Pentagon Gigascreen (port #EFF7)

Bit 6 of feature 1 attaches the Pentagon Gigascreen port, #EFF7 (0xEFF7). It is off by default. Many programs decode ports on only a few address bits, and some of them (JetPac, for example) write to addresses that a lazy decode of #EFF7 would take. With bit 6 off, SP-Pie ignores writes to #EFF7.

With bit 6 on, SP-Pie decodes the port on all 16 address bits: only a write to 0xEFF7 counts, and 0x00F7 or 0xFFF7, for example, do not. Only bit 4 of the data counts:

- Bit 4 = 1: SP-Pie keeps the value of feature 3, then sets bit 1 (mixing always on) and mode 11 (line alternate, see [Display](#display)). The other bits of feature 3 stay as they are. A second write with bit 4 = 1 keeps the value kept by the first.
- Bit 4 = 0: feature 3 gets back the value it had before the first bit 4 = 1 write. With no such write before, it does nothing.

A reset clears this: feature 3 and feature 1 return to their defaults, so the port is off again, and no value is kept.

That bit 4 = 1 turns the mode on is an assumption. The sources below give port #EFF7 bit 4 as the control, but not its polarity.

The Pentagon Gigascreen is a hardware change for the Pentagon by Dmitry Pugachev. These two Russian sources describe it:

- Dmitry Pugachev (SchemeMan, Dark Souls Group), "Gigascreen для Pentagon с интерлейсом: аппаратная модификация для расширения цветовой палитры", Black Crow #04 (2000): <https://zxpress.ru/ru/ezines/black-crow/04/gigascreen-dlya-pentagon-s-interleysom-apparatnaya-modifikaciya-dlya-rasshireniya-cvetovoy-palitry>
- "Автор аппаратного гигаскрина", real.speccy echo (2002): <https://zxpress.ru/ru/zxnet/real.speccy/avtor-aparatnogo-gigaskrina>

From them come these facts: the display shows one line from screen page 5, the next from page 7, alternating line by line; the order swaps in the next frame; the colours are not mixed; and bit 4 of port #EFF7 controls it.

### Covox

An 8-bit unsigned DAC on each enabled port: 0x80 is the centre, 0x00 the lowest level and 0xFF the highest. Each Covox port is decoded on its low eight address bits, as Fuse decodes Covox on 0xFB and 0xDD (Fuse, `peripherals/sound/covox.c`). Turning a Covox on or off sets its DAC to 0x80.

## Border

Bit 0 of feature 2 is the border extension. The Spectrum picture is 352 x 270 pixels in the middle of a 480 x 270 display, so there is an area left and right of it. With the extension on, each line of the left area has the colour of that line's leftmost picture pixel, and each line of the right area the colour of its rightmost one. The border then fills the 16:9 screen.

The border feature applies from the Spectrum frame that holds the data write.

## Display

Feature 3 sets interlace mixing. Some programs show two pictures in turn, one in each frame, by flipping between the normal and the shadow screen (port 0x7FFD bit 3), so that the eye mixes them on a CRT: more colours or finer detail than one frame holds. On HDMI the same program flickers, and a frame that SP-Pie skips or repeats shows one of the two pictures twice. Mixing shows the two pictures together instead: the average and CRT-style modes blend the last two complete Spectrum frames, double high shows both screens at once, on alternate lines, and line alternate shows them as the Pentagon Gigascreen does.

No detection method suits every program, so bits 5 and 4 choose one. Both use the screen bank alone, and neither compares the pictures.

**Per page (01, the default).** When any line of a frame flips (below), every line of the picture mixes, after 2 such frames in a row, and stops after 4 frames in a row with no flipping line. Lines that alternate in another way while the screen flips, such as a border colour that alternates below the last bank switch, mix too. A split screen whose other part animates is mixed there as well.

**Per line (00).** With bit 0 set, SP-Pie decides line by line, from the screen bank alone. For each line of the 352 x 270 picture in each frame, it records the screen bank that the line was drawn from: 0x7FFD bit 3 at the time the line's first screen pixels are drawn. A program can write 0x7FFD in the middle of a frame, so different lines of one frame can come from different banks. A line flips when its bank differs from its bank in the frame before. A line starts to mix after 2 frames in a row in which it flips, and stops after 4 frames in a row in which it does not flip. Only the lines that mix are mixed; the other lines show the newest frame. SP-Pie does not compare the pictures. A program that rewrites one screen with two pictures in turn is not mixed, and a game that flips the screens every frame, with a new picture each time, is mixed. On a 48K, which has no shadow screen, no line flips. With bit 1 set, SP-Pie mixes every line all the time, whatever bit 0 says.

Bits 3 and 2 give the mix:

| Bits 3, 2 | Mode | What the display shows |
|---|---|---|
| 00 | Average | Each pixel of a line that mixes is half of the newest frame and half of the same pixel in the frame before. |
| 01 | CRT-style | 5/8 of the newest frame and 3/8 of the frame before, on each line that mixes. On a CRT the eye sees the picture that the beam has just drawn brighter than the one before it, which has faded. This weight leans towards the newest frame while both stay clearly visible. It is a chosen weight, not a measurement. |
| 10 | Double high | No blending. Each display line that mixes shows as two lines, one above the other: one with the screen's pixels from bank 5 and one with the screen's pixels from bank 7, both at 50 frames per second (below). |
| 11 | Line alternate | No blending, normal height. Each screen line shows one bank, bank 5 or bank 7, and the next line the other bank. The order swaps in every Spectrum frame (below). |

In double high, each display line that mixes becomes two lines: bank 5 on one and bank 7 on the other, each drawn from its own memory, on every frame, whichever screen the Spectrum shows. Bit 6 gives the order: 0 puts bank 5 on the first (upper) line, 1 puts bank 7 there. The border parts of both lines are the border of that line. Lines that do not mix show the normal picture. With mode 10 and neither detection finding flips nor bit 1 set, the whole display is the normal picture.

Line alternate is the display of the Pentagon Gigascreen (see [Pentagon Gigascreen](#pentagon-gigascreen-port-eff7)). Count the lines of the 256 x 192 screen area from 0 at the top. In a Spectrum frame with an even number, line 0 and every even line show the first bank, and the odd lines show the other bank. In a frame with an odd number, the order is the other way round. Bit 6 gives the first bank: 0 for bank 5, 1 for bank 7. The frame numbers are SP-Pie's own count of Spectrum frames, so a program cannot choose which frames are even.

A line from a bank shows that bank's pixels and that bank's attributes, as the Pentagon hardware pages the whole line. For each 8-pixel cell, if the ULA drew the cell from that bank, the line shows the cell as drawn: the pixel byte and the ink and paper of that point of the frame. Otherwise the line shows the pixel byte and the attribute in that bank at the end of the frame, in the colours that Fuse gives them: BRIGHT, FLASH in the phase of that frame, and ULAplus as it was at that cell's point of the frame. So multicolour (attributes changed during the frame) inside the screen area shows only on the lines of the bank that was on show at the time; on the other bank's lines it is lost. The border parts of each line are the border as drawn. Each line is shown at normal height, the same as without mixing. The mode applies whenever mixing applies: with detection (bit 0) on the lines that it finds, or on every line with bit 1. A program can also turn it on through port #EFF7.

The display feature applies from the Spectrum frame that holds the data write. The splash screen is never mixed.

## Debug

Bit 0 of feature 0xFF is the HUD: the counters that a kernel built with `OVERLAY=1` shows in the areas left and right of the picture (see [../pi/README.md](../pi/README.md)). With the HUD off, those areas are black, or show the border extension. With the HUD and the border extension both on, the HUD is drawn over the extension. A kernel built with `OVERLAY=0` has no HUD, and bit 0 does nothing there.

The debug feature applies from the Spectrum frame that holds the data write.

## Decode notes

- Both ports have A2 = 0 (low byte 0x3B). The ZX Printer decodes A2 only, so a printer that is attached also sees writes to these ports, as it sees writes to the ULAplus ports 0xBF3B and 0xFF3B.
- Both ports have A0 = 1 and A1 = 1, so they are not the ULA port and not the AY or paging ports of the 128K and +2A/+3.
- Port 0xEFF7 has A0 = 1, A1 = 1 and A15 = 1, so it is not the ULA port, not the AY ports (A1 = 0) and not the paging ports of the 128K and +2A/+3 (A1 = 0).
- Port 0xFB is also the ZX Printer port. Keep Covox 0xFB off while a printer is attached.
- Port 0xDD has A1 = 0. A write with A15 = 1 also matches the AY decode: A14 = 1 makes it an AY register select, A14 = 0 an AY data write. `OUT (0xDD),A` puts A on A15 to A8, so with the AY on, every sample of 0x80 or more also goes to the AY. A write with A15 = 0 is not safe either on a 128K or +2: their 0x7FFD decode is A15 = 0, A1 = 0, so it pages memory and can crash the program. So on a 128K-class machine, write 0xDD samples with A15 = 1 (for example port 0xBFDD) with the AY turned off, or use another Covox port.
