#!/usr/bin/env python3
"""Lighthouse Pinball's table in full colour: the night harbour drawn
from the same geometry as the physics (tools/mktable.py), as VERA 4 bpp
tiles under the bitmap (Veranda's vr_pic: DECISIONS V105).

    mkart.py <gen dir> <sdk dir>

Draws two 640 x 480 pictures, every lamp off and every lamp lit (a lamp's
glow kept inside its own cards), and converts them together with the
SDK's mkwall.py --tiles (palette rows 4-13: rows 2 and 3 are the
sprites', the ball's and the flippers'; up to 480 tiles): the same tiles
and rows, a map each. Writes into <gen dir>:
    pic.vram.1 ...  the records (record 1: the palette rows)
    pic.h           PIC_RECORDS; each lamp's map entries off and lit
                    (lamp_at: its first entry in lamp_e0 / lamp_e1, a
                    lamp's cards row by row)
    pic-0.png pic-1.png  the pictures as VERA shows them (lamps off, lit)
    art-off.png art-lit.png  the pictures as drawn
The art is this program's own: a harbour at night, a lighthouse, a ferry
ramp, fog banks. No other game's table or art.

Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
"""
import math
import os
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mktable as T  # noqa: E402  (the geometry: its module level builds it)

S = 4                           # supersampling
W, H = 640, 480
TOP = 16
PAPER = 0x112                   # palette entry 0 (the game's paper)
BAND = int(os.environ.get('PB_BAND', '24'))   # a gradient's steps: whole card rows (solid tiles)
NCOL = int(os.environ.get('PB_NCOL', '0'))  # the colours of the picture before the conversion


def hexrgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[k] + (b[k] - a[k]) * t)) for k in range(3))


class Canvas:
    """an RGB picture at S times the size; coordinates in screen pixels"""

    def __init__(self):
        self.im = Image.new('RGB', (W * S, H * S), (0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def P(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def rect(self, x0, y0, x1, y1, c):
        self.d.rectangle([x0 * S, y0 * S, x1 * S - 1, y1 * S - 1], fill=c)

    def vgrad(self, x0, y0, x1, y1, stops, mask=None):
        """a vertical gradient over the box (stops: (t, colour))"""
        h = (y1 - y0) * S
        g = Image.new('RGB', (1, h))
        for j in range(h):
            y = y0 + j / S                  # a colour a card row: solid tiles
            t = ((y // BAND) * BAND + BAND / 2 - y0) / max(1, y1 - y0)
            for (ta, ca), (tb, cb) in zip(stops, stops[1:]):
                if ta <= t <= tb:
                    g.putpixel((0, j), mix(ca, cb, (t - ta) / max(1e-6, tb - ta)))
                    break
        g = g.resize(((x1 - x0) * S, h))
        if mask is None:
            self.im.paste(g, (x0 * S, y0 * S))
        else:
            self.im.paste(g, (x0 * S, y0 * S), mask.crop((x0 * S, y0 * S, x1 * S, y1 * S)))

    def line(self, pts, w, c):
        self.d.line(self.P(pts), fill=c, width=max(1, int(round(w * S))), joint='curve')
        r = w * S / 2
        for x, y in self.P([pts[0], pts[-1]]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=c)

    def poly(self, pts, c):
        self.d.polygon(self.P(pts), fill=c)

    def disc(self, cx, cy, r, c):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=c)

    def ring(self, cx, cy, r, w, c):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], outline=c, width=int(w * S))

    def radial(self, cx, cy, r, inner, outer, hx=0.0, hy=0.0):
        """a disc shaded from inner (at the highlight cx + hx r, cy + hy r) to outer"""
        n = int(2 * r * S) + 2
        g = Image.new('RGB', (n, n))
        m = Image.new('L', (n, n), 0)
        px, pm = g.load(), m.load()
        for j in range(n):
            for i in range(n):
                dx, dy = (i + 0.5) / S - r, (j + 0.5) / S - r
                d = math.hypot(dx, dy)
                if d <= r:
                    pm[i, j] = 255
                    e = math.hypot(dx - hx * r, dy - hy * r) / (r * (1 + math.hypot(hx, hy)))
                    px[i, j] = mix(inner, outer, e)
        self.im.paste(g, (int((cx - r) * S), int((cy - r) * S)), m)

    def glow(self, cx, cy, r, c, strength=1.0):
        """light added round a point (a lamp's)"""
        n = int(4 * r * S)
        layer = Image.new('RGB', (W * S, H * S))
        ImageDraw.Draw(layer).ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
                                      fill=tuple(int(v * strength) for v in c))
        layer = layer.crop((int((cx - 2 * r) * S), int((cy - 2 * r) * S), int((cx - 2 * r) * S) + n,
                            int((cy - 2 * r) * S) + n)).filter(ImageFilter.GaussianBlur(r * S / 2))
        box = (int((cx - 2 * r) * S), int((cy - 2 * r) * S))
        base = self.im.crop((box[0], box[1], box[0] + n, box[1] + n))
        self.im.paste(ImageChops.add(base, layer), box)

    def text(self, x, y, s, c, scale=1, shadow=None, spacing=1):
        for ch in s:
            g = T.FONT.get(ch, T.FONT[' '])
            for r in range(7):
                for k in range(5):
                    if g[r * 5 + k] == '#':
                        if shadow:
                            self.rect(x + k * scale + 1, y + r * scale + 1, x + (k + 1) * scale + 1,
                                      y + (r + 1) * scale + 1, shadow)
            for r in range(7):
                for k in range(5):
                    if g[r * 5 + k] == '#':
                        cc = c(r / 6.0) if callable(c) else c
                        self.rect(x + k * scale, y + r * scale, x + (k + 1) * scale, y + (r + 1) * scale, cc)
            x += (5 + spacing) * scale
        return x

    def done(self):
        return self.im.resize((W, H), Image.BOX)


def text_w(s, scale=1, spacing=1):
    return len(s) * (5 + spacing) * scale - spacing * scale


# ------------------------------------------------------------------ colours
C = {k: hexrgb(v) for k, v in dict(
    paper=0x111122, sky0=0x070b22, sky1=0x18204c, sky2=0x2f2a5e, sea0=0x0c2440, sea1=0x061226,
    water0=0x2a2358, water1=0x163f6e, water2=0x0b2a52, water3=0x081a36,
    apron0=0x1c2233, apron1=0x10141f, panel=0x0d111c, glass=0x161d33, dmd=0x07090f,
    rail0=0x1a1d26, rail1=0x8d97ab, rail2=0xe9eef7, rubber=0xf2f2ee, rubber_d=0x7a7a74,
    gold0=0xffe9a0, gold1=0xd09a2c, gold2=0x7a5418, lamp_off=0x4a3c1c, yellow=0xffd84a, yellow_hi=0xfff6c8,
    red=0xd8343c, red_d=0x5a1418, red_hi=0xff9c9c, green=0x46d26a, green_d=0x14401e, cyan=0x46c8f0,
    cyan_d=0x123a4a, orange=0xff9a3a, orange_d=0x4a2810, purple=0x6a58b8, purple_d=0x2c2258,
    fog0=0xf4f2ff, fog1=0x9c94d0, wood0=0xa8703c, wood1=0x6e4220, wood2=0x3c2410, white=0xf4f6ff,
    star=0xfff6d8, moon=0xf6efcf, moon_d=0xb8b090, label=0xb8c4e8, beam=0xffe7a0,
).items()}


# ------------------------------------------------------------------ the masks
def table_mask():
    m = Image.new('L', (W * S, H * S), 0)
    d = ImageDraw.Draw(m)
    pts = [(172, 479)] + T.arch + [(460, 479)]
    d.polygon([(x * S, y * S) for x, y in pts], fill=255)
    return m


# ------------------------------------------------------------------ the picture
def draw(lit):
    """the screen, every lamp off (lit False) or lit (True)"""
    cv = Canvas()
    tm = table_mask()
    # the panels' backgrounds
    cv.vgrad(0, TOP, 168, 352, [(0, C['sky0']), (0.7, C['sky1']), (1, C['sky2'])])
    cv.vgrad(0, 352, 168, H, [(0, C['sea0']), (1, C['sea1'])])
    cv.vgrad(168, TOP, 470, H, [(0, C['apron0']), (1, C['apron1'])])
    cv.vgrad(470, TOP, 640, H, [(0, C['panel']), (1, C['paper'])])
    # the playfield's water, inside the outline
    cv.vgrad(172, 28, 461, H, [(0, C['water0']), (0.32, C['water1']), (0.75, C['water2']), (1, C['water3'])], tm)
    left_panel(cv)
    playfield(cv, tm, lit)
    right_panel(cv, lit)
    return cv.done()


def left_panel(cv):
    # stars (a fixed scatter), the moon and its light
    rnd = [12345]

    def rand():
        rnd[0] = (rnd[0] * 1103515245 + 12345) & 0x7FFFFFFF
        return rnd[0] >> 8
    for i in range(46):
        x, y = rand() % 156 + 6, rand() % 280 + 24
        if 96 < y < 140 or (110 < x < 156 and 40 < y < 80):
            continue
        b = 0.45 + (rand() % 100) / 180
        cv.disc(x + 0.5, y + 0.5, 0.55 if i % 7 else 0.9, mix(C['sky1'], C['star'], b))
    cv.radial(132, 60, 14, C['moon'], C['moon_d'], -0.4, -0.4)
    cv.radial(139, 55, 11.5, C['sky0'], C['sky0'])     # a crescent: the night over its right part
    for (x, y, r) in [(124, 64, 2.2), (127, 54, 1.4), (121, 57, 1.1)]:
        cv.disc(x, y, r, mix(C['moon'], C['moon_d'], 0.6))
    # the title, gold with a shadow
    gold = lambda t: mix(C['gold0'], C['gold1'], t)
    tw = T.text_w('LIGHTHOUSE', 2)
    cv.text((168 - tw) // 2, 104, 'LIGHTHOUSE', gold, 2, shadow=C['gold2'])
    tw = T.text_w('PINBALL', 2)
    cv.text((168 - tw) // 2, 122, 'PINBALL', gold, 2, shadow=C['gold2'])
    # the lighthouse's beams (light added), the tower, the lantern
    for (a0, a1), c in [((-13, 11), (60, 62, 74)), ((167, 191), (60, 62, 74)), ((-5, 4), (104, 98, 84)),
                        ((175, 184), (104, 98, 84))]:
        pts = [(80, 206)]
        for a in (a0, a1):
            r = math.radians(a)
            pts.append((80 + 92 * math.cos(r), 206 + 40 * math.sin(r)))
        cv.poly(pts, c)
    # the rock and the sea
    cv.poly([(34, 362), (48, 346), (64, 340), (98, 340), (114, 348), (126, 362)], (58, 54, 66))
    cv.poly([(40, 362), (52, 350), (66, 345), (80, 346), (70, 352), (56, 362)], (88, 84, 98))
    for i, y in enumerate(range(366, 386, 7)):
        for x0 in range(-6 + (i % 2) * 9, 168, 18):
            cv.line([(x0, y + 1.5), (x0 + 5, y - 1), (x0 + 10, y + 1.5)], 1.2, mix(C['sea0'], C['label'], 0.45 - i * 0.1))
    # the tower: red and white bands, the gallery, the lantern room
    for y in range(220, 352, 2):
        w = 9 + (y - 220) * 7 / 132
        band = ((y - 220) // 22) % 2
        c0 = (226, 66, 70) if band == 0 else (236, 236, 240)
        c1 = (120, 26, 34) if band == 0 else (150, 150, 168)
        for k in range(int(2 * w * 2)):
            t = k / (4 * w)
            cv.rect(80 - w + k / 2, y, 80 - w + (k + 1) / 2, y + 2, mix(c0, c1, abs(t - 0.35) * 1.6))
    cv.rect(62, 216, 98, 220, (40, 42, 56))
    cv.rect(64, 214, 96, 216, (120, 124, 140))
    cv.rect(70, 196, 90, 214, (30, 30, 44))
    cv.radial(80, 205, 7, (255, 250, 210), (255, 196, 60))
    cv.glow(80, 205, 10, (90, 70, 20))
    for x in (70, 76, 84, 90):
        cv.rect(x, 196, x + 1, 214, (40, 40, 56))
    cv.poly([(68, 196), (80, 185), (92, 196)], (180, 40, 46))
    cv.poly([(80, 185), (92, 196), (86, 196)], (110, 22, 30))
    cv.disc(80, 184, 1.6, (200, 200, 210))
    # the keys
    for i, (k, v) in enumerate(T.KEYS):
        cv.text(8, 394 + 20 * i, k, C['gold0'])
        cv.text(160 - T.text_w(v), 402 + 20 * i, v, C['label'])


def rail(cv, pts, w=3.2):
    """a chrome wire guide along the points"""
    cv.line([(x + 0.6, y + 0.9) for x, y in pts], w + 0.6, (8, 10, 18))
    cv.line(pts, w, C['rail0'])
    cv.line(pts, w - 1.2, C['rail1'])
    cv.line([(x - 0.35, y - 0.35) for x, y in pts], max(0.7, w - 2.6), C['rail2'])


def plate(cv, x0, y0, x1, y1, on, c_on, c_off, label=None, txt_on=(30, 16, 8), txt_off=None):
    """a lamp insert: a rounded plate, lit or not, its label"""
    c = c_on if on else c_off
    cv.d.rounded_rectangle([x0 * S, y0 * S, x1 * S - 1, y1 * S - 1], radius=2 * S, fill=c,
                           outline=mix(c, (0, 0, 0), 0.5), width=S // 2)
    if on:
        cv.d.rounded_rectangle([(x0 + 1) * S, y0 * S + S // 2, (x1 - 1) * S, (y0 + 2) * S],
                               radius=S, fill=mix(c, (255, 255, 255), 0.45))
    if label:
        tw = T.text_w(label)
        cv.text(x0 + (x1 - x0 - tw) / 2, y0 + (y1 - y0 - 7) / 2 + 0.5, label,
                txt_on if on else (txt_off or mix(c_off, (255, 255, 255), 0.62)))


def insert(cv, cx, cy, r, on, c_on, c_off, letter=None):
    """a round lamp insert"""
    if on:
        cv.glow(cx, cy, r * 1.6, tuple(v // 3 for v in c_on))
        cv.radial(cx, cy, r, mix(c_on, (255, 255, 255), 0.7), c_on, -0.3, -0.3)
    else:
        cv.radial(cx, cy, r, mix(c_off, (255, 255, 255), 0.25), c_off, -0.3, -0.3)
    cv.ring(cx, cy, r, 0.6, (12, 12, 20))
    if letter:
        cv.text(cx - 2.5, cy - 3.5, letter, (40, 22, 6) if on else mix(c_off, (255, 255, 255), 0.5))


def playfield(cv, tm, lit):
    L = dict.fromkeys([l[0] for l in T.lamps], lit)
    # the water: a few swells, the deep by the flippers
    for i, y in enumerate(range(196, 470, 26)):
        for x0 in range(176 + (i % 3) * 22, 452, 66):
            pts = [(x0 + k * 3, y + 1.6 * math.sin((x0 + k * 3) / 9.0)) for k in range(9)]
            if all(T.inside_table(x, yy) for x, yy in pts):
                cv.line(pts, 1.0, mix(C['water1'], C['white'], 0.18 - 0.012 * i))
    # a compass rose painted on the water (the harbour's chart)
    cx, cy = 304, 352
    cv.ring(cx, cy, 22, 1.0, mix(C['water2'], C['label'], 0.35))
    cv.ring(cx, cy, 15, 0.7, mix(C['water2'], C['label'], 0.25))
    for a in range(0, 360, 90):
        r = math.radians(a)
        rr = math.radians(a + 90)
        tip = (cx + 26 * math.cos(r), cy + 26 * math.sin(r))
        cv.poly([tip, (cx + 5 * math.cos(rr), cy + 5 * math.sin(rr)), (cx, cy)], mix(C['water2'], C['label'], 0.55))
        cv.poly([tip, (cx - 5 * math.cos(rr), cy - 5 * math.sin(rr)), (cx, cy)], mix(C['water2'], C['label'], 0.3))
    # the shooter lane: its floor, the arrows
    cv.vgrad(437, 150, 460, H, [(0, (24, 28, 44)), (1, (12, 14, 24))])
    for y in range(300, 440, 34):
        cv.poly([(442, y + 8), (448, y), (454, y + 8), (451, y + 8), (448, y + 4), (445, y + 8)], (70, 76, 100))
    # the ferry's deck: planks between the ramp's walls
    deck = [(366, 280), (408, 200), (436, 186), (436, 206), (394, 286)]
    cv.poly(deck, C['wood1'])
    for i in range(14):
        f = (i + 0.5) / 14
        ax, ay = 366 + (408 - 366) * f, 280 + (200 - 280) * f
        bx, by = 394 + (436 - 394) * f, 286 + (206 - 286) * f
        cv.line([(ax + 1.5, ay + 0.3), (bx - 1.5, by)], 1.0, C['wood2'])
        cv.line([(ax + 1.5, ay - 1.0), (bx - 1.5, by - 1.3)], 0.6, C['wood0'])
    for k, (fx, fy) in enumerate([(375, 263), (391, 239), (407, 215)]):
        on = L['FERRY%d' % k]
        c = C['yellow'] if on else (110, 72, 30)
        if on:
            cv.glow(fx, fy + 1, 5, (80, 60, 10))
        cv.poly([(fx - 6, fy + 5), (fx, fy - 4), (fx + 6, fy + 5), (fx + 3, fy + 5), (fx, fy + 1), (fx - 3, fy + 5)], c)
    cv.text(400, 290, 'FERRY', C['gold0'], shadow=(8, 8, 16))
    # the ferry's rail: two steel wires
    for side in (-3, 3):
        pts = []
        for a, b in zip(T.RAIL, T.RAIL[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            n = math.hypot(dx, dy)
            pts.append((a[0] - dy / n * side, a[1] + dx / n * side))
        pts.append((T.RAIL[-1][0] - dy / n * side, T.RAIL[-1][1] + dx / n * side))
        cv.line([(x + 0.5, y + 0.8) for x, y in pts], 1.2, (6, 8, 16))
        cv.line(pts, 1.1, (150, 160, 182))
    # the lanes' inserts, B E A M
    for i, ch in enumerate('BEAM'):
        x = T.LANE_X[i] + 16
        insert(cv, x, 96, 6.6, L['LANE%d' % i], C['yellow'], C['lamp_off'], ch)
    # the fog banks: their shadows, skirts and caps
    for i, (x, y) in enumerate(T.BUMPERS):
        cv.disc(x + 1.5, y + 2.5, 15, (6, 8, 20))
        cv.radial(x, y, 14.5, mix(C['purple'], C['white'], 0.25), C['purple_d'], -0.4, -0.5)
        cv.ring(x, y, 14.5, 0.8, (14, 10, 34))
        on = L['BUMP%d' % i]
        if on:
            cv.radial(x, y, 10, C['yellow_hi'], C['yellow'], -0.3, -0.4)
        else:
            cv.radial(x, y, 10, C['fog0'], C['fog1'], -0.3, -0.4)
        for a in range(0, 360, 60):
            r = math.radians(a + 15)
            cv.disc(x + 5.5 * math.cos(r), y + 5.5 * math.sin(r), 2.4,
                    mix(C['yellow'] if on else C['fog1'], (255, 255, 255), 0.55))
        cv.disc(x, y, 3.2, (255, 255, 255) if on else mix(C['fog0'], C['fog1'], 0.3))
    # the lighthouse island: a rock, the striped tower, the lantern, the target
    cv.poly([(368, 134), (372, 120), (378, 112), (394, 112), (400, 120), (404, 134)], (58, 54, 70))
    for y in range(94, 132):
        f = (y - 94) / 38
        x0, x1 = 378 - 4 * f, 394 + 4 * f
        band = ((y - 106) // 7) % 2 if y >= 106 else 2
        c = [(220, 60, 66), (236, 236, 244), (34, 36, 50)][band]
        for k in range(int((x1 - x0) * 2)):
            t = k / ((x1 - x0) * 2)
            cv.rect(x0 + k / 2, y, x0 + (k + 1) / 2, y + 1, mix(c, (0, 0, 0), abs(t - 0.35) * 0.9))
    on = L['LANTERN']
    if on:
        for a in range(-60, 61, 30):
            r = math.radians(a - 90)
            cv.line([(386 + 9 * math.cos(r), 100 + 9 * math.sin(r)), (386 + 13 * math.cos(r), 100 + 13 * math.sin(r))],
                    1.2, C['yellow'])
        cv.glow(386, 102, 9, (110, 90, 30))
        cv.radial(386, 102, 5.5, C['yellow_hi'], C['yellow'], -0.2, -0.2)
    else:
        cv.radial(386, 102, 5.5, (120, 104, 60), (60, 48, 24), -0.2, -0.2)
    cv.ring(386, 102, 5.5, 0.7, C['gold1'])
    rail(cv, [(374, 132), (378, 94), (394, 94), (398, 132)], 2.6)
    cv.rect(374, 131, 398, 134, (250, 210, 60))
    for x in range(375, 398, 4):
        cv.poly([(x, 134), (x + 2, 131), (x + 3.5, 131), (x + 1.5, 134)], (30, 30, 30))
    plate(cv, 360, 136, 416, 144, L['JACKPOT'], C['red'], C['red_d'], 'JACKPOT', (255, 250, 240))
    # the buoys: red and white posts (drop targets), their lamps
    for i, (a, b) in enumerate(T.BUOYS):
        for y in range(a, b, 4):
            cv.rect(204.5, y, 207.5, min(b, y + 4), (232, 60, 60) if (y - a) // 4 % 2 == 0 else (240, 240, 244))
        cv.rect(204, a, 204.6, b, (40, 10, 10))
        insert(cv, 219.5, (a + b) / 2, 3.2, L['BUOY%d' % i], C['green'], C['green_d'])
    cv.text(212, 184, 'BUOYS', C['label'], shadow=(4, 6, 14))
    # the spinner across the orbit, its insert
    cv.rect(173, 249, 199, 251.5, (180, 186, 200))
    cv.rect(173, 251.5, 199, 252.5, (60, 64, 80))
    plate(cv, 176, 256, 198, 264, L['SPIN'], C['cyan'], C['cyan_d'], 'SPIN')
    # the missions' inserts and labels; the multipliers; extra ball; combo
    for i, (k, s) in enumerate([('MBEAM', 'BEAM'), ('MBUOY', 'BUOYS'), ('MFERRY', 'FERRY')]):
        insert(cv, 267.5, 228 + 16 * i, 3.2, L[k], C['cyan'], C['cyan_d'])
        cv.text(274, 225 + 16 * i, s, C['label'], shadow=(4, 6, 14))
    for i in range(4):
        plate(cv, 272 + 16 * i + 1, 280, 272 + 16 * i + 15, 288, L['MULT%d' % i], C['orange'], C['orange_d'],
              '%dX' % (i + 2))
    plate(cv, 273, 304, 335, 312, L['EXTRA'], C['green'], C['green_d'], 'EXTRA BALL')
    plate(cv, 353, 296, 391, 304, L['COMBO'], C['red'], C['red_d'], 'COMBO', (255, 250, 240))
    plate(cv, 289, 448, 319, 456, L['AGAIN'], C['red'], C['red_d'], 'SHOOT', (255, 250, 240))
    cv.text(290, 459, 'AGAIN', C['label'], shadow=(4, 6, 14))
    # the slingshots: red plastic, white rubber
    for m in (lambda x: x, T.mirror):
        A, B, Cc = (m(216), 340), (m(216), 376), (m(238), 390)
        cv.poly([A, B, Cc], (150, 26, 34))
        inner = [(A[0] + (2.5 if m(0) == 0 else -2.5), A[1] + 8), (B[0] + (2.5 if m(0) == 0 else -2.5), B[1] - 2),
                 (Cc[0] - (7 if m(0) == 0 else -7), Cc[1] - 4)]
        cv.poly(inner, (220, 60, 70))
        cv.line([A, Cc], 3.0, C['rubber_d'])
        cv.line([A, Cc], 2.2, C['rubber'])
        cv.line([A, B, Cc], 2.0, C['rubber_d'])
    # the flippers' posts
    for (x, y) in T.PIVOTS:
        cv.disc(x, y, 5, (20, 22, 30))
        cv.radial(x, y, 3.5, (220, 224, 236), (90, 96, 112), -0.4, -0.4)
    # every wall a chrome guide; the one-way gate thin; the outline
    for (x0, y0, x1, y1, kind, ident) in T.segs:
        if kind == T.K_WALL:
            rail(cv, [(x0, y0), (x1, y1)])
        elif kind == T.K_ONEWAY:
            for k in range(6):
                f0, f1 = k / 6, (k + 0.5) / 6
                cv.line([(x0 + (x1 - x0) * f0, y0 + (y1 - y0) * f0), (x0 + (x1 - x0) * f1, y0 + (y1 - y0) * f1)],
                        1.4, C['rail1'])
        elif kind == T.K_RUBBER:
            cv.line([(x0, y0), (x1, y1)], 2.2, C['rubber'])
    rail(cv, [(172, 479)] + T.arch + [(460, 479)], 3.6)
    for (x0, y0, x1, y1, kind, ident) in T.segs:
        if kind in (T.K_WALL, T.K_RUBBER, T.K_SLING):
            for (x, y) in ((x0, y0), (x1, y1)):
                cv.radial(x, y, 2.3, (240, 244, 252), (90, 96, 112), -0.4, -0.4)


def frame(cv, x0, y0, x1, y1, fill):
    """a panel: a bevelled frame round a dark glass"""
    cv.d.rounded_rectangle([(x0 - 3) * S, (y0 - 3) * S, (x1 + 4) * S - 1, (y1 + 4) * S - 1], radius=4 * S,
                           fill=(54, 66, 98))
    cv.d.rounded_rectangle([(x0 - 2) * S, (y0 - 2) * S, (x1 + 4) * S - 1, (y1 + 4) * S - 1], radius=4 * S,
                           fill=(6, 8, 14))
    cv.d.rounded_rectangle([(x0 - 2) * S, (y0 - 2) * S, (x1 + 3) * S - 1, (y1 + 3) * S - 1], radius=3 * S,
                           fill=(34, 42, 66))
    cv.d.rounded_rectangle([(x0 - 1) * S, (y0 - 1) * S, (x1 + 2) * S - 1, (y1 + 2) * S - 1], radius=2 * S,
                           fill=fill)


def right_panel(cv, lit):
    P = T.PANEL
    frame(cv, *P['score'], C['dmd'])
    for k in ('ball', 'missions', 'scores', 'status'):
        frame(cv, *P[k], C['glass'])
    frame(cv, *P['msg'], (12, 26, 44))
    cv.text(476, 22, 'SCORE', C['gold0'])
    cv.text(476, 158, 'HARBOUR LOG', C['gold0'])
    for i, (k, s) in enumerate([('PBEAM', 'BEAM LANES'), ('PBUOY', 'BUOYS'), ('PFERRY', 'FERRY RUNS')]):
        insert(cv, 484, 180 + 16 * i, 3.4, lit, C['cyan'], C['cyan_d'])
        cv.text(492, 177 + 16 * i, s, C['label'])
    cv.text(476, 238, 'HARBOUR MASTERS', C['gold0'])
    for x in range(476, 628, 4):
        cv.rect(x, 250, x + 2, 251, (70, 80, 110))
    if lit:      # TILT: the status panel's top lines red
        cv.d.rounded_rectangle([472 * S, 416 * S, 632 * S - 1, 432 * S - 1], radius=2 * S, fill=(150, 22, 30))
        cv.d.rectangle([473 * S, 417 * S, 631 * S - 1, 419 * S], fill=(210, 60, 66))


# ------------------------------------------------------------------ the lamps, the conversion
def lamp_cards():
    out = []
    for (name, x, y, w, h, c) in T.lamps:
        out.append([(x + i, y + j) for j in range(h) for i in range(w)])
    return out


def main():
    gen, sdk = sys.argv[1], sys.argv[2]
    os.makedirs(gen, exist_ok=True)
    off = draw(False)
    lit = draw(True)
    # a lamp's light only inside its own cards; everything else the same
    mask = Image.new('L', (W, H), 0)
    md = ImageDraw.Draw(mask)
    cards = lamp_cards()
    seen = set()
    for cs in cards:
        for (x, y) in cs:
            assert (x, y) not in seen, 'two lamps on card %d, %d' % (x, y)
            seen.add((x, y))
            md.rectangle([x * 8, y * 8, x * 8 + 7, y * 8 + 7], fill=255)
    lit = Image.composite(lit, off, mask)
    # the colours brought down to a few (exact 12-bit ones, no dither), so
    # that a card's fit one palette row of 15
    both = Image.new('RGB', (W, 2 * H))
    both.paste(off, (0, 0))
    both.paste(lit, (0, H))
    q = both if not NCOL else both.quantize(colors=NCOL, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
    q = q if not NCOL else Image.eval(q, lambda v: int(round(v / 17.0)) * 17)
    off, lit = q.crop((0, 0, W, H)), q.crop((0, H, W, 2 * H))
    po, pl = os.path.join(gen, 'art-off.png'), os.path.join(gen, 'art-lit.png')
    off.save(po)
    lit.save(pl)
    out = os.path.join(gen, 'pic')
    r = subprocess.run([sys.executable, '-I', '-B', os.path.join(sdk, 'tools', 'mkwall.py'), '--tiles', out,
                        '--rows', '4-13', '--max', os.environ.get('PB_TILES', '928'), '--paper', '%03X' % PAPER,
                        '--dither' if os.environ.get('PB_DITHER') == '1' else '--no-dither', po, pl],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit('mkart: ' + (r.stderr or r.stdout))
    mp = open(out + '.map', 'rb').read()
    nrec = 0
    while os.path.exists('%s.vram.%d' % (out, nrec + 1)):
        nrec += 1
    e0, e1, at = [], [], []
    for cs in cards:
        at.append(len(e0))
        for (x, y) in cs:
            k = 2 * (y * 80 + x)
            e0.append(mp[k] | mp[k + 1] << 8)
            e1.append(mp[9600 + k] | mp[9600 + k + 1] << 8)
    at.append(len(e0))
    h = ['/* Lighthouse Pinball: the table\'s picture (generated by tools/mkart.py; do not edit) */',
         '#ifndef PIC_H', '#define PIC_H', '#include <stdint.h>',
         '#define PIC_RECORDS %d' % nrec,
         '#define PIC_LAMPCARDS %d' % len(e0),
         '/* each lamp\'s cards (row by row): its map entries off and lit */',
         'static const uint8_t lamp_at[%d] = { %s };' % (len(at), ', '.join(map(str, at))),
         'static const uint16_t lamp_e0[%d] = { %s };' % (len(e0), ', '.join('0x%04X' % v for v in e0)),
         'static const uint16_t lamp_e1[%d] = { %s };' % (len(e1), ', '.join('0x%04X' % v for v in e1)),
         '#endif', '']
    open(os.path.join(gen, 'pic.h'), 'w').write('\n'.join(h))
    print(r.stdout.strip().replace('mkwall --tiles: ', '') + '; %d lamp cards' % len(e0))


if __name__ == '__main__':
    main()
