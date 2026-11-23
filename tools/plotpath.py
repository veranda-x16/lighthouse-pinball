#!/usr/bin/env python3
"""(development) the ball's path from host/sim's "path" over the preview:
plotpath.py <preview.png> <path.csv> <out.png> [from to]"""
import sys
from PIL import Image, ImageDraw
im = Image.open(sys.argv[1]).convert('RGB')
d = ImageDraw.Draw(im)
a = int(sys.argv[4]) if len(sys.argv) > 4 else 0
b = int(sys.argv[5]) if len(sys.argv) > 5 else 1 << 30
prev = None
for ln in open(sys.argv[2]):
    t, x, y, on, st, ne = map(int, ln.split())
    if not (a <= t < b):
        continue
    p = (x / 64, y / 64)
    if prev and on:
        d.line([prev, p], fill=(255, 255, 0) if st == 2 else (0, 255, 0))
    prev = p if on else None
im.save(sys.argv[3])
