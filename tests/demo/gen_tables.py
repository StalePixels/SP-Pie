#!/usr/bin/env python3
"""Writes tables.inc, the data tables of demo.asm. Run it after changing a
formula; build.sh does not run it."""
import colorsys, sys

out = []
def db(label, vals, comment):
    out.append("; " + comment)
    out.append(label + ":")
    for i in range(0, len(vals), 8):
        out.append("        DB " + ",".join("0x%02X" % v for v in vals[i:i+8]))

def row_byte(k, sign):
    # Bit 7-j is set when (j + sign*k) mod 8 is 0 or 1: a diagonal line two pixels wide.
    b = 0
    for j in range(8):
        if ((j + sign * k) & 7) < 2:
            b |= 0x80 >> j
    return b

db("tab_plus", [row_byte(k, 1) for k in range(8)],
   "Screen byte of a line that slopes one way, indexed by the picture line number mod 8.")
db("tab_minus", [row_byte(k, -1) for k in range(8)],
   "Screen byte of a line that slopes the other way, indexed the same way.")

def grb(r, g, b):          # r, g, b in 0..1 -> ULAplus byte GGGRRRBB
    return (round(g * 7) << 5) | (round(r * 7) << 2) | round(b * 3)

def clamp(x):
    return max(0.0, min(1.0, x))

# Standard palette: entry 16*c + i is ink i, entry 16*c + 8 + i is paper i, of
# CLUT c. Bit 0 of c is BRIGHT. The 48K colours are used, so a screen looks as
# it does without ULAplus.
std = []
for c in range(4):
    for kind in range(2):
        for i in range(8):
            level = 1.0 if c & 1 else 0.8
            std.append(grb(level * ((i >> 1) & 1), level * ((i >> 2) & 1), level * (i & 1)))
db("pal_std", std, "ULAplus palette 0: the 48K colours, normal in CLUT 0 and 2, bright in CLUT 1 and 3.")

def ramp(c, t):
    if c == 0:
        return grb(*colorsys.hsv_to_rgb(t * 0.92, 1.0, 1.0))
    if c == 1:
        return grb(t, t, t)
    if c == 2:
        return grb(clamp(t * 3), clamp(t * 3 - 1), clamp(t * 3 - 2))
    return grb(clamp(t * 2 - 1), clamp(t * 1.6 - 0.2), clamp(0.35 + t * 0.65))

pic = []
for c in range(4):
    steps = [ramp(c, s / 15.0) for s in range(16)]
    # Ink g shows step 2g, paper g shows step 2g+1.
    pic += [steps[2 * g] for g in range(8)] + [steps[2 * g + 1] for g in range(8)]
db("pal_ramps", pic, "ULAplus palette 1: CLUT 0 hue sweep, 1 grey, 2 fire, 3 sea. 16 steps each.")
open("tables.inc", "w").write("\n".join(out) + "\n")
