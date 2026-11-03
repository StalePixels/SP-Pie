#!/usr/bin/env python3
"""Converts the source picture into the three screens the demo shows.

Needs Python 3, numpy and Pillow. Writes:
  data/giga.bin     bank 5 screen, then bank 7 screen: the Gigascreen pair
  data/dhigh.bin    bank 5 screen, then bank 7 screen: the double-high picture
  data/ulaplus.bin  one screen, then the 64 palette bytes (GRB, GGGRRRBB)
  data/pal_std.bin  64 palette bytes that give the 48K colours
  preview/*.png     what each view should look like (see the README)

Each screen is 6912 bytes: 6144 pixel bytes in the Spectrum line order, then
768 attribute bytes. The picture fills the top 22 character rows. The last two
rows are left empty for the program's title and hint text.

Run from any folder: python3 convert.py
"""
import os
import sys
import numpy as np
from PIL import Image, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "source", "macaw.jpg")
# Crop of the source picture (left, top, right, bottom), 960 x 660, the shape of 256 x 176.
CROP = (0, 100, 960, 760)

W = 256
ROWS = 22                       # character rows holding the picture
H = ROWS * 8                    # picture lines of each bank
SEED = 1


# ---------------------------------------------------------------- colour
def spectrum_colours():
    """The 16 Spectrum colours: index = bright * 8 + colour (bit 0 blue, 1 red, 2 green)."""
    out = []
    for bright in (0, 1):
        level = 255 if bright else 205
        for c in range(8):
            out.append((level * ((c >> 1) & 1), level * ((c >> 2) & 1), level * (c & 1)))
    return np.array(out, float)


def rgb_to_lab(rgb):
    c = np.asarray(rgb, float) / 255.0
    lin = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = lin @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16.0 / 116.0)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def grb_to_rgb(byte):
    """ULAplus byte GGGRRRBB to 8-bit RGB."""
    byte = np.asarray(byte)
    g = (byte >> 5) & 7
    r = (byte >> 2) & 7
    b = byte & 3
    return np.stack([r * 255 / 7, g * 255 / 7, b * 255 / 3], -1)


def rgb_to_grb(rgb):
    rgb = np.clip(np.asarray(rgb, float), 0, 255)
    r = np.rint(rgb[..., 0] * 7 / 255).astype(int)
    g = np.rint(rgb[..., 1] * 7 / 255).astype(int)
    b = np.rint(rgb[..., 2] * 3 / 255).astype(int)
    return (g << 5) | (r << 2) | b


def load_target(w, h):
    """The source picture cropped and scaled to w x h, with a little more colour."""
    img = Image.open(SOURCE).convert("RGB").crop(CROP).resize((w, h), Image.LANCZOS)
    img = ImageEnhance.Color(img).enhance(1.3)
    img = ImageEnhance.Contrast(img).enhance(1.08)
    return np.asarray(img, float)


# ------------------------------------------------------------- dithering
def dither(target, cell_w, cell_h, palettes):
    """Floyd-Steinberg error diffusion. palettes[cy][cx] is an (n, 3) array of RGB
    colours. Returns the index of the chosen colour for every pixel."""
    h, w, _ = target.shape
    work = target.copy()
    out = np.zeros((h, w), np.uint8)
    weights = np.array([2.0, 4.0, 3.0])
    for y in range(h):
        xs = range(w) if y % 2 == 0 else range(w - 1, -1, -1)
        sign = 1 if y % 2 == 0 else -1
        for x in xs:
            pal = palettes[y // cell_h][x // cell_w]
            old = work[y, x]
            k = int(np.argmin((((pal - old) ** 2) * weights).sum(1)))
            out[y, x] = k
            err = (old - pal[k]) * 0.85
            if 0 <= x + sign < w:
                work[y, x + sign] += err * 7 / 16
            if y + 1 < h:
                if 0 <= x - sign < w:
                    work[y + 1, x - sign] += err * 3 / 16
                work[y + 1, x] += err * 5 / 16
                if 0 <= x + sign < w:
                    work[y + 1, x + sign] += err * 1 / 16
    return out


def cell_cost(blk_rgb, blk_lab, sets_rgb, sets_lab):
    """For every candidate colour set (sets_rgb: (C, n, 3)) dither the block (h, w, 3)
    with error diffusion inside the block, and return the cost of each candidate: the
    squared Lab error of the result after a 3 x 3 blur (what the eye sees), plus a
    little of the unblurred error."""
    count, n, _ = sets_rgb.shape
    h, w, _ = blk_rgb.shape
    work = np.broadcast_to(blk_rgb, (count, h, w, 3)).copy()
    idx = np.zeros((count, h, w), int)
    ar = np.arange(count)
    weights = np.array([2.0, 4.0, 3.0])
    for y in range(h):
        for x in range(w):
            old = work[:, y, x]
            k = (((sets_rgb - old[:, None, :]) ** 2) * weights).sum(-1).argmin(1)
            idx[:, y, x] = k
            err = (old - sets_rgb[ar, k]) * 0.85
            if x + 1 < w:
                work[:, y, x + 1] += err * 7 / 16
            if y + 1 < h:
                if x > 0:
                    work[:, y + 1, x - 1] += err * 3 / 16
                work[:, y + 1, x] += err * 5 / 16
                if x + 1 < w:
                    work[:, y + 1, x + 1] += err * 1 / 16
    diff = sets_lab[ar[:, None, None], idx] - blk_lab
    pad = np.pad(diff, ((0, 0), (1, 1), (1, 1), (0, 0)), mode="edge")
    blur = sum(pad[:, dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)) / 9.0
    return (blur ** 2).sum((1, 2, 3)) + 0.05 * (diff ** 2).sum((1, 2, 3))


# ------------------------------------------------------------ screen file
def line_offset(y):
    return ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)


def make_screen(bits, attrs):
    """bits: (H, 256) of 0/1. attrs: (ROWS, 32) attribute bytes. Returns 6912 bytes."""
    screen = bytearray(6912)
    packed = np.packbits(bits.astype(np.uint8), axis=1)
    for y in range(bits.shape[0]):
        o = line_offset(y)
        screen[o:o + 32] = packed[y].tobytes()
    for r in range(attrs.shape[0]):
        screen[6144 + r * 32:6144 + r * 32 + 32] = bytes(int(a) for a in attrs[r])
    return bytes(screen)


def render_screen(bits, attrs):
    """RGB picture of one standard screen (no flash)."""
    cols = spectrum_colours()
    out = np.zeros((bits.shape[0], W, 3))
    for cy in range(attrs.shape[0]):
        for cx in range(32):
            a = int(attrs[cy, cx])
            ink = cols[((a >> 6) & 1) * 8 + (a & 7)]
            paper = cols[((a >> 6) & 1) * 8 + ((a >> 3) & 7)]
            blk = bits[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8, None]
            out[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = np.where(blk == 1, ink, paper)
    return out


def save_png(name, rgb, scale_x=2, scale_y=2):
    img = Image.fromarray(np.clip(np.rint(rgb), 0, 255).astype(np.uint8))
    img = img.resize((img.width * scale_x, img.height * scale_y), Image.NEAREST)
    img.save(os.path.join(HERE, "preview", name))


# ------------------------------------------------------------------ options
def options():
    """(bright, ink, paper) with ink <= paper: 72 attribute choices."""
    return [(b, i, p) for b in (0, 1) for i in range(8) for p in range(i, 8)]


# -------------------------------------------------------------- Gigascreen
def convert_giga(target):
    cols = spectrum_colours()
    opts = options()
    pair = np.array([[cols[b * 8 + i], cols[b * 8 + p]] for b, i, p in opts])     # (72, 2, 3)
    combos = [(a, b) for a in range(len(opts)) for b in range(a, len(opts))]
    mix = np.zeros((len(combos), 4, 3))
    for n, (a, b) in enumerate(combos):
        for k in range(4):
            mix[n, k] = (pair[a, k >> 1] + pair[b, k & 1]) / 2.0     # k: (ink5|paper5, ink7|paper7)
    mix_lab = rgb_to_lab(mix)
    tgt_lab = rgb_to_lab(target)
    best = np.zeros((ROWS, 32), int)
    for cy in range(ROWS):
        for cx in range(32):
            blk = target[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]
            lab = tgt_lab[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]
            best[cy, cx] = int(np.argmin(cell_cost(blk, lab, mix, mix_lab)))
    palettes = [[mix[best[cy, cx]] for cx in range(32)] for cy in range(ROWS)]
    idx = dither(target, 8, 8, palettes)
    bits5 = np.zeros((H, W), np.uint8)
    bits7 = np.zeros((H, W), np.uint8)
    att5 = np.zeros((ROWS, 32), np.uint8)
    att7 = np.zeros((ROWS, 32), np.uint8)
    for cy in range(ROWS):
        for cx in range(32):
            a, b = combos[best[cy, cx]]
            for screen_att, o in ((att5, a), (att7, b)):
                br, i, p = opts[o]
                screen_att[cy, cx] = (br << 6) | (p << 3) | i
    k = idx
    bits5[:] = np.where((k >> 1) == 0, 1, 0)         # k bit 1 = 0 means ink of bank 5
    bits7[:] = np.where((k & 1) == 0, 1, 0)
    return bits5, att5, bits7, att7


# -------------------------------------------------------------- double high
def convert_dhigh(target):
    """target: (2 * H, 256, 3). Cells are 8 wide and 16 canvas lines high."""
    cols = spectrum_colours()
    opts = options()
    pair = np.array([[cols[b * 8 + i], cols[b * 8 + p]] for b, i, p in opts])
    pair_lab = rgb_to_lab(pair)
    tgt_lab = rgb_to_lab(target)
    palettes = []
    attrs = np.zeros((ROWS, 32), np.uint8)
    choice = np.zeros((ROWS, 32), int)
    for cy in range(ROWS):
        row = []
        for cx in range(32):
            blk = target[cy * 16:cy * 16 + 16, cx * 8:cx * 8 + 8]
            lab = tgt_lab[cy * 16:cy * 16 + 16, cx * 8:cx * 8 + 8]
            o = int(np.argmin(cell_cost(blk, lab, pair, pair_lab)))
            br, i, p = opts[o]
            attrs[cy, cx] = (br << 6) | (p << 3) | i
            choice[cy, cx] = o
            row.append(pair[o])
        palettes.append(row)
    idx = dither(target, 8, 16, palettes)
    canvas = np.where(idx == 0, 1, 0).astype(np.uint8)        # colour 0 is the ink
    return canvas[0::2], canvas[1::2], attrs


# ------------------------------------------------------------------ ULAplus
def kmeans(x, k, rng, iters=25):
    centres = [x[rng.integers(len(x))]]
    for _ in range(1, k):
        d = np.min([((x - c) ** 2).sum(1) for c in centres], axis=0)
        centres.append(x[rng.choice(len(x), p=d / d.sum())] if d.sum() > 0 else x[rng.integers(len(x))])
    centres = np.array(centres)
    for _ in range(iters):
        label = np.argmin(((x[:, None] - centres[None]) ** 2).sum(-1), 1)
        for j in range(k):
            m = label == j
            if m.any():
                centres[j] = x[m].mean(0)
    return centres, label


def convert_ula(target):
    rng = np.random.default_rng(SEED)
    lab = rgb_to_lab(target)
    cells_rgb = target.reshape(ROWS, 8, 32, 8, 3).transpose(0, 2, 1, 3, 4).reshape(ROWS * 32, 64, 3)
    cells_lab = lab.reshape(ROWS, 8, 32, 8, 3).transpose(0, 2, 1, 3, 4).reshape(ROWS * 32, 64, 3)
    mean = cells_lab.mean(1)
    _, group = kmeans(mean, 4, rng)
    pal = np.zeros((4, 16, 3))                                  # [clut][slot] RGB; slots 0-7 ink, 8-15 paper

    def arrange(centres_rgb):
        quant = grb_to_rgb(rgb_to_grb(centres_rgb))
        order = np.argsort(rgb_to_lab(quant)[:, 0])
        quant = quant[order]
        out = np.zeros((16, 3))
        out[0:8] = quant[0::2]                                  # ink slots: even ranks
        out[8:16] = quant[1::2]                                 # paper slots: odd ranks
        return out

    for c in range(4):
        px = cells_rgb[group == c].reshape(-1, 3)
        if len(px) < 16:
            px = cells_rgb.reshape(-1, 3)
        centres, _ = kmeans(px[::max(1, len(px) // 4000)], 16, rng)
        pal[c] = arrange(centres)
    ij = [(i, p) for i in range(8) for p in range(8)]
    best_c = np.zeros(len(cells_lab), int)
    best_i = np.zeros(len(cells_lab), int)
    best_p = np.zeros(len(cells_lab), int)
    blocks_rgb = cells_rgb.reshape(-1, 8, 8, 3)
    blocks_lab = cells_lab.reshape(-1, 8, 8, 3)
    for it in range(8):
        pal_lab = rgb_to_lab(pal)
        final = it == 7
        for n in range(len(cells_lab)):
            sets_rgb = np.array([[pal[c, i], pal[c, 8 + p]] for c in range(4) for i, p in ij])
            sets_lab = np.array([[pal_lab[c, i], pal_lab[c, 8 + p]] for c in range(4) for i, p in ij])
            if final:
                k = int(np.argmin(cell_cost(blocks_rgb[n], blocks_lab[n], sets_rgb, sets_lab)))
            else:
                t = cells_lab[n].reshape(64, 1, 1, 3)
                k = int(np.argmin(((t - sets_lab[None]) ** 2).sum(-1).min(-1).sum(0)))
            best_c[n], (best_i[n], best_p[n]) = k // 64, ij[k % 64]
        if final:
            break
        sums = np.zeros((4, 16, 3))
        cnt = np.zeros((4, 16))
        for n in range(len(cells_lab)):
            c, i, p = best_c[n], best_i[n], best_p[n]
            ci = rgb_to_lab(pal[c, i]); cp = rgb_to_lab(pal[c, 8 + p])
            use_ink = ((cells_lab[n] - ci) ** 2).sum(1) <= ((cells_lab[n] - cp) ** 2).sum(1)
            sums[c, i] += cells_rgb[n][use_ink].sum(0); cnt[c, i] += use_ink.sum()
            sums[c, 8 + p] += cells_rgb[n][~use_ink].sum(0); cnt[c, 8 + p] += (~use_ink).sum()
        for c in range(4):
            for s_ in range(16):
                if cnt[c, s_] > 0:
                    pal[c, s_] = grb_to_rgb(rgb_to_grb(sums[c, s_] / cnt[c, s_]))
    palettes = []
    attrs = np.zeros((ROWS, 32), np.uint8)
    for cy in range(ROWS):
        row = []
        for cx in range(32):
            n = cy * 32 + cx
            c, i, p = best_c[n], best_i[n], best_p[n]
            attrs[cy, cx] = (c << 6) | (p << 3) | i
            row.append(np.array([pal[c, i], pal[c, 8 + p]]))
        palettes.append(row)
    idx = dither(target, 8, 8, palettes)
    bits = np.where(idx == 0, 1, 0).astype(np.uint8)
    pal_bytes = rgb_to_grb(pal.reshape(64, 3)).astype(np.uint8)
    return bits, attrs, pal, pal_bytes, palettes


def render_ula(bits, attrs, pal):
    out = np.zeros((H, W, 3))
    for cy in range(ROWS):
        for cx in range(32):
            a = int(attrs[cy, cx])
            c = a >> 6
            ink = pal[c, a & 7]
            paper = pal[c, 8 + ((a >> 3) & 7)]
            blk = bits[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8, None]
            out[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = np.where(blk == 1, ink, paper)
    return out


def pad_bank(bits):
    full = np.zeros((192, W), np.uint8)
    full[:bits.shape[0]] = bits
    return full


def pad_attr(attrs):
    full = np.zeros((24, 32), np.uint8)
    full[:attrs.shape[0]] = attrs
    return full


def standard_palette():
    """64 ULAplus bytes that give the 48K colours: entry 16c + i is ink i and 16c + 8 + i
    is paper i of table c, bright in tables 1 and 3."""
    cols = spectrum_colours()
    out = []
    for c in range(4):
        for _kind in range(2):
            for i in range(8):
                out.append(int(rgb_to_grb(cols[(c & 1) * 8 + i])))
    return bytes(out)


def main():
    os.makedirs(os.path.join(HERE, "data"), exist_ok=True)
    open(os.path.join(HERE, "data", "pal_std.bin"), "wb").write(standard_palette())
    os.makedirs(os.path.join(HERE, "preview"), exist_ok=True)
    only = sys.argv[1:] or ["giga", "dhigh", "ula"]
    save_png("source.png", load_target(W, H), 2, 2)
    if "giga" in only:
        b5, a5, b7, a7 = convert_giga(load_target(W, H))
        open(os.path.join(HERE, "data", "giga.bin"), "wb").write(
            make_screen(pad_bank(b5), pad_attr(a5)) + make_screen(pad_bank(b7), pad_attr(a7)))
        r5, r7 = render_screen(b5, a5), render_screen(b7, a7)
        save_png("giga_bank5.png", r5)
        save_png("giga_bank7.png", r7)
        save_png("giga_blend_50_50.png", (r5 + r7) / 2)
        save_png("giga_blend_crt.png", r5 * 3 / 8 + r7 * 5 / 8)
    if "dhigh" in only:
        b5, b7, att = convert_dhigh(load_target(W, 2 * H))
        open(os.path.join(HERE, "data", "dhigh.bin"), "wb").write(
            make_screen(pad_bank(b5), pad_attr(att)) + make_screen(pad_bank(b7), pad_attr(att)))
        cols = spectrum_colours()
        canvas = np.zeros((2 * H, W, 3))
        for y in range(H):
            for bank, bits in ((0, b5), (1, b7)):
                for cx in range(32):
                    a = int(att[y // 8, cx])
                    ink = cols[((a >> 6) & 1) * 8 + (a & 7)]
                    paper = cols[((a >> 6) & 1) * 8 + ((a >> 3) & 7)]
                    row = bits[y, cx * 8:cx * 8 + 8, None]
                    canvas[2 * y + bank, cx * 8:cx * 8 + 8] = np.where(row == 1, ink, paper)
        save_png("dhigh_256x352.png", canvas, 2, 1)
        save_png("dhigh_bank5.png", render_screen(b5, att))
        save_png("dhigh_bank7.png", render_screen(b7, att))
    if "ula" in only:
        bits, attrs, pal, pal_bytes, _ = convert_ula(load_target(W, H))
        open(os.path.join(HERE, "data", "ulaplus.bin"), "wb").write(
            make_screen(pad_bank(bits), pad_attr(attrs)) + bytes(int(v) for v in pal_bytes))
        save_png("ulaplus.png", render_ula(bits, attrs, pal))
        sw = np.zeros((16, 64, 3))
        for n in range(64):
            sw[:, n] = pal[n // 16, n % 16]
        save_png("ulaplus_palette.png", sw, 8, 8)


if __name__ == "__main__":
    main()
