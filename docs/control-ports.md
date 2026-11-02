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
| 1 | Attached devices | Bit 0: AY. Bit 1: TurboSound, a second AY. Bit 2: beeper. Bit 3: Covox on port 0xFB. Bit 4: Covox on port 0xDD. Bit 5: Covox on port 0xDF. Bits 6 and 7 are reserved. | 0x07: AY, TurboSound and beeper on, every Covox off |
| 2 | Border | Bit 0: border extension. Bits 1 to 7 are reserved for later border functions. | 0x00: off |
| 0xFF | Debug | Bit 0: HUD. Bits 1 to 7 are reserved for later debug flags and parameters. | 0x01: HUD on |

Every other feature number is reserved. SP-Pie ignores data writes to a reserved feature and ignores reserved bits. Write 0 to reserved bits: a later version can give them a meaning.

## Reset

When the Spectrum is reset, every feature returns to its default and feature 0 is selected. The reset also clears both AY chips: every register becomes 0, register 0 is selected in each chip, and chip 0 is selected.

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

### Covox

An 8-bit unsigned DAC on each enabled port: 0x80 is the centre, 0x00 the lowest level and 0xFF the highest. Each Covox port is decoded on its low eight address bits, as Fuse decodes Covox on 0xFB and 0xDD (Fuse, `peripherals/sound/covox.c`). Turning a Covox on or off sets its DAC to 0x80.

## Border

Bit 0 of feature 2 is the border extension. The Spectrum picture is 352 x 270 pixels in the middle of a 480 x 270 display, so there is an area left and right of it. With the extension on, each line of the left area has the colour of that line's leftmost picture pixel, and each line of the right area the colour of its rightmost one. The border then fills the 16:9 screen.

The border feature applies from the Spectrum frame that holds the data write.

## Debug

Bit 0 of feature 0xFF is the HUD: the counters that a kernel built with `OVERLAY=1` shows in the areas left and right of the picture (see [../pi/README.md](../pi/README.md)). With the HUD off, those areas are black, or show the border extension. With the HUD and the border extension both on, the HUD is drawn over the extension. A kernel built with `OVERLAY=0` has no HUD, and bit 0 does nothing there.

The debug feature applies from the Spectrum frame that holds the data write.

## Decode notes

- Both ports have A2 = 0 (low byte 0x3B). The ZX Printer decodes A2 only, so a printer that is attached also sees writes to these ports, as it sees writes to the ULAplus ports 0xBF3B and 0xFF3B.
- Both ports have A0 = 1 and A1 = 1, so they are not the ULA port and not the AY or paging ports of the 128K and +2A/+3.
- Port 0xFB is also the ZX Printer port. Keep Covox 0xFB off while a printer is attached.
- Port 0xDD has A1 = 0. A write with A15 = 1 also matches the AY decode: A14 = 1 makes it an AY register select, A14 = 0 an AY data write. `OUT (0xDD),A` puts A on A15 to A8, so with the AY on, every sample of 0x80 or more also goes to the AY. Write such samples with `OUT (C),A` and B below 0x80, or turn the AY off.
