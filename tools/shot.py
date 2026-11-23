#!/usr/bin/env python3
"""A screenshot from an x16emu dump (-dump V), as VERA shows it: the
bitmap and the colour map (the SDK's vram2png.compose), then the sprites
(their attributes from the dump's sprite section, 4 or 8 bpp, flips, the
palette offset, z > 0; sprite 0 the pointer on top).

    shot.py <SDK tools dir> <dump.bin> <out.png> [--palette]

--palette prints palette entries 0-63 as $0RGB words (for the gate).
Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
"""
import sys

sys.path.insert(0, sys.argv[1])
import vram2png  # noqa: E402  (the SDK's)
from PIL import Image  # noqa: E402

VRAM, COMPOSER, PALETTE, LAYERS, SPRITES = 131072, 256, 512, 14, 1024


def main():
    tools, dump, out = sys.argv[1:4]
    v = vram2png.section(dump)
    pal = v[VRAM + COMPOSER:VRAM + COMPOSER + PALETTE]
    if '--palette' in sys.argv:
        print(' '.join('%03X' % ((pal[2 * i + 1] & 15) << 8 | pal[2 * i]) for i in range(64)))
    rows = vram2png.compose(v)
    im = Image.new('RGB', (640, 480))
    px = im.load()
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            px[x, y] = c
    spr = v[VRAM + COMPOSER + PALETTE + LAYERS:][:SPRITES]

    def rgb(i):
        return vram2png.rgb(v, i)
    for n in range(127, -1, -1):
        a = spr[8 * n:8 * n + 8]
        z = (a[6] >> 2) & 3
        if not z:
            continue
        addr = (a[0] | (a[1] & 15) << 8) << 5
        bpp8 = a[1] & 0x80
        x0 = a[2] | (a[3] & 3) << 8
        y0 = a[4] | (a[5] & 3) << 8
        if x0 >= 1024 - 64:
            x0 -= 1024
        if y0 >= 1024 - 64:
            y0 -= 1024
        w, h = 8 << ((a[7] >> 4) & 3), 8 << (a[7] >> 6)
        off = (a[7] & 15) * 16
        for yy in range(h):
            for xx in range(w):
                if bpp8:
                    c = v[addr + yy * w + xx]
                else:
                    b = v[addr + (yy * w + xx) // 2]
                    c = (b >> 4) if xx % 2 == 0 else (b & 15)
                if not c:
                    continue
                if not bpp8:
                    c += off
                X = x0 + (w - 1 - xx if a[6] & 1 else xx)
                Y = y0 + (h - 1 - yy if a[6] & 2 else yy)
                if 0 <= X < 640 and 0 <= Y < 480:
                    px[X, Y] = rgb(c)
    im.save(out)


if __name__ == '__main__':
    main()
