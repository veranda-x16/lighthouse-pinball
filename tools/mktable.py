#!/usr/bin/env python3
"""Lighthouse Pinball's table, defined once: the physics' shapes, the art,
the colour map, the sprites, the lamps.

    mktable.py <outdir>

writes into <outdir>:
    table.h      the geometry for src/phys.c (units: 1/64 pixel), the grid,
                 the sensors, the lamps, the ferry's rail, the flipper and
                 sprite constants, the score digits
    art.1 art.2 ...  the bitmap (rows 16-479, 80 bytes a row) packed in
                 records of at most 8,000 bytes (a word: the length, then
                 runs: c < 128: c + 1 bytes follow; c >= 128: the next byte
                 c - 126 times)
    colours.bin  the colour map, a byte a card, rows 2-59 (80 x 58)
    sprites.1 sprites.2  the sprite pictures (4 bpp) as they go to VRAM
    preview.png  the screen as VERA would show it, sprites at rest
    art.bin      the raw bitmap (for the gate: the screen against it)

Everything is the program's own: an original table (a night harbour, a
lighthouse, a ferry ramp, fog banks), no other game's layout or art.
Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
"""
import math
import os
import struct
import sys

from PIL import Image

U = 64                  # units a pixel
BALL_R = 7.0            # the ball's radius, pixels
FLIP_R = 5.0            # the flippers' radius (physics)
FLIP_L = 40.0           # pivot to tip
TOP = 16                # the first row under the menu bar

# palette entries (sdk/lib/theme.h's names) and the game's colours for them
PAPER, INK, CHROME, SIDEBAR, SELECT, TITLE, YELLOW, RED, GREEN, PURPLE, ORANGE, CYAN, SKY, SKY2, HILL, DUSK = range(16)
GAME_PALETTE = [0x112, 0xEEF, 0x223, 0x334, 0x357, 0x346, 0xFD4, 0xE45,
                0x3C6, 0x748, 0xB52, 0x29B, 0x124, 0x236, 0x254, 0x435]

# sprite colours: the ball (palette offset 2: entries 32-47) and the
# flippers and the plunger (offset 3: 48-63)
BALL_PAL = [0x000, 0xFFF, 0xEEF, 0xCCD, 0xAAB, 0x889, 0x667, 0x445, 0x223, 0x99A, 0xBBC]
PART_PAL = [0x000, 0xFFF, 0xDDE, 0xAAB, 0x223, 0xE44, 0xB22, 0xFD4, 0xC90, 0x667, 0x889]

segs = []               # (x0, y0, x1, y1, kind, id)
circles = []            # (x, y, r, kind, id)
sensors = []            # (x0, x1, y, kind, id): horizontal lines
lamps = []              # (name, cx, cy, w, h, lit colour)

# kinds of segment (src/phys.h)
K_WALL, K_RUBBER, K_SLING, K_ONEWAY, K_DROP, K_STANDUP = range(6)
# kinds of circle
C_BUMPER, C_POST = range(2)
# kinds of sensor
S_LANE, S_SPIN, S_DOCK, S_EXIT, S_INLANE, S_OUTLANE, S_ORBIT = range(7)


def seg(x0, y0, x1, y1, kind=K_WALL, ident=0):
    segs.append((x0, y0, x1, y1, kind, ident))


def chain(pts, kind=K_WALL, ident=0):
    for a, b in zip(pts, pts[1:]):
        seg(a[0], a[1], b[0], b[1], kind, ident)


def mirror(x):
    return 608 - x       # the playfield's centre line is x = 304


# ------------------------------------------------------------------ the table
# the outer walls: left x 172, the shooter lane's outside x 460, the arch
ARCH_C, ARCH_RX, ARCH_RY = (316.0, 140.0), 144.0, 112.0
arch = []
NARCH = 26
for i in range(NARCH + 1):
    t = math.pi * (1 - i / NARCH)
    arch.append((ARCH_C[0] + ARCH_RX * math.cos(t), ARCH_C[1] - ARCH_RY * math.sin(t)))
chain([(172, 474)] + arch + [(460, 474)])

# the shooter lane: its inner wall, and the one-way gate at its top (it
# blocks from the playfield's side: the normal points up and left)
seg(436, 474, 436, 168)
seg(460, 146, 436, 168, K_ONEWAY)

# the harbour loop (an orbit) on the left: its inner guide; the buoys (drop
# targets) on its right face
seg(200, 300, 200, 170)
BUOYS = [(194, 210), (218, 234), (242, 258)]
for i, (a, b) in enumerate(BUOYS):
    seg(206, b, 206, a, K_DROP, i)

# the beam lanes at the top: B E A M
LANE_X = [240, 272, 304, 336, 368]
for x in LANE_X:
    seg(x, 62, x, 86)

# the fog banks (pop bumpers)
BUMPERS = [(272, 138), (336, 138), (304, 182)]
for i, (x, y) in enumerate(BUMPERS):
    circles.append((x, y, 14.0, C_BUMPER, i))

# the lighthouse's island; its lantern target faces down
chain([(374, 132), (378, 94), (394, 94), (398, 132)])
seg(374, 132, 398, 132, K_STANDUP, 0)

# the ferry ramp on the right: two walls and its top
seg(366, 280, 408, 200)
seg(394, 286, 436, 206)
seg(408, 200, 436, 186)

# slingshots, inlane guides (and their mirror images)
for m in (lambda x: x, mirror):
    A, B, C = (m(216), 340), (m(216), 376), (m(238), 390)
    seg(*A, *C, K_SLING, 0 if m(0) == 0 else 1)
    seg(*A, *B, K_RUBBER)
    seg(*B, *C, K_RUBBER)
    chain([(m(190), 340), (m(190), 384), (m(246), 415.5)])

# flippers: pivots, angles in 1/256 of a turn (y down: positive is down)
PIVOTS = [(254, 426), (354, 426)]
FLIP_REST, FLIP_UP = 21, -19
FLIP_FRAMES = 9                 # rest ... up, 5 units apart

# the plunger: the lane's middle, the knob's top at rest and pulled
PLUNGER_X, PLUNGER_REST, PLUNGER_PULL = 448, 458, 16

# sensors
for i in range(4):
    sensors.append((LANE_X[i] + 2, LANE_X[i + 1] - 2, 80, S_LANE, i))
sensors.append((172, 200, 250, S_SPIN, 0))
sensors.append((400, 436, 216, S_DOCK, 0))
sensors.append((436, 460, 200, S_EXIT, 0))
sensors.append((190, 216, 356, S_INLANE, 0))
sensors.append((392, 418, 356, S_INLANE, 1))
sensors.append((172, 190, 356, S_OUTLANE, 0))
sensors.append((418, 436, 356, S_OUTLANE, 1))
sensors.append((172, 200, 170, S_ORBIT, 0))

DRAIN_Y = 478

# the ferry's rail (the ball rides it from the dock to the left inlane)
RAIL = [(422, 206), (430, 170), (428, 128), (414, 92), (390, 60), (352, 46), (304, 44),
        (256, 48), (222, 62), (198, 88), (186, 124), (184, 170), (186, 230), (190, 280),
        (198, 312), (203, 326)]
RIDE_SPEED = 6.0                # pixels a tick
RIDE_EXIT_V = (0.0, 2.5)        # the velocity it leaves with

# ------------------------------------------------------------------ lamps
# cards: (name, x card, y card, w, h, lit colour)
for i, x in enumerate(LANE_X[:4]):
    lamps.append(('LANE%d' % i, (x + 16) // 8 - 1, 11, 2, 2, YELLOW))
for i, (a, b) in enumerate(BUOYS):
    lamps.append(('BUOY%d' % i, 27, (a + b) // 16, 1, 1, GREEN))
lamps.append(('FERRY0', 46, 32, 2, 2, YELLOW))
lamps.append(('FERRY1', 48, 29, 2, 2, YELLOW))
lamps.append(('FERRY2', 50, 26, 2, 2, YELLOW))
lamps.append(('LANTERN', 47, 12, 2, 2, YELLOW))
lamps.append(('BUMP0', 32, 15, 4, 4, YELLOW))
lamps.append(('BUMP1', 40, 15, 4, 4, YELLOW))
lamps.append(('BUMP2', 36, 21, 4, 4, YELLOW))
lamps.append(('MBEAM', 33, 28, 1, 1, CYAN))
lamps.append(('MBUOY', 33, 30, 1, 1, CYAN))
lamps.append(('MFERRY', 33, 32, 1, 1, CYAN))
for i in range(4):
    lamps.append(('MULT%d' % i, 34 + 2 * i - 0, 35, 2, 1, ORANGE))
lamps.append(('EXTRA', 34, 38, 8, 1, GREEN))
lamps.append(('AGAIN', 36, 56, 4, 1, RED))
lamps.append(('COMBO', 44, 37, 5, 1, RED))
lamps.append(('SPIN', 22, 32, 3, 1, CYAN))
lamps.append(('JACKPOT', 45, 17, 7, 1, RED))

# zone colours (cards), in order; later ones win
ZONES = [
    (0, 2, 80, 58, PAPER),          # outside the table
    (22, 2, 36, 12, DUSK),          # the night above the lanes
    (22, 14, 36, 22, SKY2),         # the harbour's water
    (22, 36, 36, 24, SKY),          # the deep water by the flippers
    (31, 15, 6, 6, PURPLE), (39, 15, 6, 6, PURPLE), (35, 21, 6, 6, PURPLE),   # fog
    (46, 11, 5, 6, RED),            # the lighthouse
    (45, 24, 9, 12, ORANGE),        # the ferry ramp's planks
    (0, 2, 21, 58, SKY),            # the left panel: the night sky
    (0, 44, 21, 16, SKY2),          # the sea
    (58, 2, 22, 58, CHROME),        # the right panel
]


# ------------------------------------------------------------------ geometry tools
def seg_rect_dist(s, rx0, ry0, rx1, ry1):
    """the distance from segment s to the rectangle (0 inside)"""
    x0, y0, x1, y1 = s

    def inside(x, y):
        return rx0 <= x <= rx1 and ry0 <= y <= ry1

    if inside(x0, y0) or inside(x1, y1):
        return 0.0
    edges = [(rx0, ry0, rx1, ry0), (rx1, ry0, rx1, ry1), (rx1, ry1, rx0, ry1), (rx0, ry1, rx0, ry0)]
    for e in edges:
        if seg_seg_intersect(s, e):
            return 0.0
    d = min(pt_seg_dist(px, py, s) for px, py in [(rx0, ry0), (rx1, ry0), (rx0, ry1), (rx1, ry1)])
    for e in edges:
        d = min(d, pt_seg_dist(x0, y0, e), pt_seg_dist(x1, y1, e))
    return d


def pt_seg_dist(px, py, s):
    x0, y0, x1, y1 = s
    dx, dy = x1 - x0, y1 - y0
    L2 = dx * dx + dy * dy
    t = 0 if L2 == 0 else max(0, min(1, ((px - x0) * dx + (py - y0) * dy) / L2))
    return math.hypot(px - x0 - t * dx, py - y0 - t * dy)


def seg_seg_intersect(a, b):
    def cr(ox, oy, ax, ay, bx, by):
        return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox)
    d1 = cr(*b[:2], *b[2:], *a[:2])
    d2 = cr(*b[:2], *b[2:], *a[2:])
    d3 = cr(*a[:2], *a[2:], *b[:2])
    d4 = cr(*a[:2], *a[2:], *b[2:])
    return (d1 > 0) != (d2 > 0) and (d3 > 0) != (d4 > 0)


# ------------------------------------------------------------------ the 5x7 font
FONT = {
    'A': ".###.#...##...#######...##...##...#", 'B': "####.#...#####.#...##...##...#####.",
    'C': ".###.#...##....#....#....#...#.###.", 'D': "####.#...##...##...##...##...#####.",
    'E': "######....####.#....#....#....#####", 'F': "######....####.#....#....#....#....",
    'G': ".###.#...##....#.####...##...#.####", 'H': "#...##...#######...##...##...##...#",
    'I': ".###...#....#....#....#....#...###.", 'J': "..###...#....#....#....##...#.###..",
    'K': "#...##..#.###..#.#..#..#.#...##...#", 'L': "#....#....#....#....#....#....#####",
    'M': "#...###.###.#.##...##...##...##...#", 'N': "#...###..##.#.##..###...##...##...#",
    'O': ".###.#...##...##...##...##...#.###.", 'P': "####.#...##...#####.#....#....#....",
    'Q': ".###.#...##...##...##.#.##..#..##.#", 'R': "####.#...##...#####.#.#..#..#.#...#",
    'S': ".#####....#.....###.....#....#####.", 'T': "#####..#....#....#....#....#....#..",
    'U': "#...##...##...##...##...##...#.###.", 'V': "#...##...##...##...#.#.#..#.#...#..",
    'W': "#...##...##...##.#.##.#.##.#.#.#.#.", 'X': "#...##...#.#.#...#...#.#.#...##...#",
    'Y': "#...##...#.#.#...#....#....#....#..", 'Z': "#####....#...#...#...#...#....#####",
    '0': ".###.#...##..###.#.###..##...#.###.", '1': "..#...##....#....#....#....#...###.",
    '2': ".###.#...#....#...#...#...#...#####", '3': "#####...#...#.....#.....##...#.###.",
    '4': "...#...##..#.#.#..#.#####...#....#.", '5': "######....####.....#....##...#.###.",
    '6': "..##..#...#....####.#...##...#.###.", '7': "#####....#...#...#...#....#....#...",
    '8': ".###.#...##...#.###.#...##...#.###.", '9': ".###.#...##...#.####....#...#..##..",
    ' ': "." * 35, '-': "...............#####...............", '!': "..#....#....#....#....#.........#..",
    '.': "..............................##...", ':': ".......##....##.........##....##...",
    '/': "....#...#....#...#....#...#....#...", 'x': "..........#...#.#.#...#...#.#.#...#",
    '+': ".......#....#..#####..#....#.......", '=': "..........#####.....#####..........",
}
for k, v in FONT.items():
    assert len(v) == 35, k
    FONT[k] = v[:35]


def text(img, x, y, s, scale=1, colour=1, spacing=1):
    for ch in s:
        g = FONT[ch]
        for r in range(7):
            for c in range(5):
                if g[r * 5 + c] == '#':
                    for a in range(scale):
                        for b in range(scale):
                            img.putpixel((x + c * scale + b, y + r * scale + a), colour)
        x += (5 + spacing) * scale
    return x


def text_w(s, scale=1, spacing=1):
    return len(s) * (5 + spacing) * scale - spacing * scale


# ------------------------------------------------------------------ drawing
W, H = 640, 480
art = Image.new('1', (W, H), 0)


def px(x, y, c=1):
    if 0 <= x < W and TOP <= y < H:
        art.putpixel((int(x), int(y)), c)


def line(x0, y0, x1, y1, width=1, c=1, dotted=0):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(n + 1):
        if dotted and (i // 2) % dotted:
            continue
        x = x0 + (x1 - x0) * i / n
        y = y0 + (y1 - y0) * i / n
        for a in range(width):
            for b in range(width):
                px(round(x - (width - 1) / 2 + a), round(y - (width - 1) / 2 + b), c)


def circle(cx, cy, r, c=1, fill=False, step=0):
    for y in range(int(cy - r - 1), int(cy + r + 2)):
        for x in range(int(cx - r - 1), int(cx + r + 2)):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if (fill and d <= r) or (not fill and abs(d - r) < 0.6):
                if not step or (x + y) % step == 0:
                    px(x, y, c)


def inside_table(x, y):
    """inside the playfield's outer outline (walls and the arch)"""
    if x < 172 or x > 460 or y > 479:
        return False
    if y >= ARCH_C[1]:
        return True
    dx = (x - ARCH_C[0]) / ARCH_RX
    dy = (y - ARCH_C[1]) / ARCH_RY
    return dx * dx + dy * dy <= 1.0


# the rail outside the table: a sparse texture
for y in range(TOP, H):
    for x in range(160, 472):
        if not inside_table(x + 0.5, y + 0.5) and (x + 2 * y) % 6 == 0:
            px(x, y)

# walls: two pixels thick, caps on the ends
for (x0, y0, x1, y1, kind, ident) in segs:
    if kind == K_DROP:
        line(x0, y0, x1, y1, 3)
    elif kind == K_ONEWAY:
        line(x0, y0, x1, y1, 1, dotted=2)
    elif kind in (K_SLING, K_RUBBER):
        line(x0, y0, x1, y1, 2)
    else:
        line(x0, y0, x1, y1, 2)
for (x0, y0, x1, y1, kind, ident) in segs:
    if kind in (K_WALL, K_RUBBER, K_SLING):
        circle(x0, y0, 1.5, fill=True)
        circle(x1, y1, 1.5, fill=True)

# the slingshots' rubber: a second line inside the kicking face
for m in (lambda x: x, mirror):
    A, C = (m(216), 340), (m(238), 390)
    k = 1 if m(0) == 0 else -1
    line(A[0] + 3 * k, A[1] + 7, C[0] - 4 * k, C[1] - 3, 1)
    text(art, m(216) + (2 if k > 0 else -7), 360, 'S' if k > 0 else 'S')

# fog banks: rings, a cloud's texture
for (x, y) in BUMPERS:
    circle(x, y, 14)
    circle(x, y, 13)
    circle(x, y, 9, step=2)
    circle(x, y, 5)
    for a in range(0, 360, 45):
        t = math.radians(a)
        px(x + 11 * math.cos(t), y + 11 * math.sin(t))

# lane letters (on their lamps) and arrows into the lanes
for i, ch in enumerate('BEAM'):
    x = LANE_X[i] + 16
    text(art, x - 3, 92, ch)
    circle(x, 95.5, 7)

# the buoys' lamps: small circles; labels
for i, (a, b) in enumerate(BUOYS):
    circle(219.5, (a + b) / 2, 3)
text(art, 212, 186, 'BUOYS')

# the lighthouse: stripes, a lantern, the gallery
for y in range(100, 132, 8):
    line(377, y, 396, y, 1)
circle(386, 102, 5)
line(370, 92, 402, 92, 1)
for a in range(-60, 61, 20):
    t = math.radians(a - 90)
    line(386 + 9 * math.cos(t), 100 + 9 * math.sin(t), 386 + 18 * math.cos(t), 100 + 18 * math.sin(t), 1, dotted=2)
text(art, 362, 138, 'JACKPOT')

# the ferry ramp: planks across the channel, arrows, its name
for i in range(9):
    f = i / 8
    ax, ay = 366 + (408 - 366) * f, 280 + (200 - 280) * f
    bx, by = 394 + (436 - 394) * f, 286 + (206 - 286) * f
    line(ax + 2, ay, bx - 2, by, 1, dotted=3)
for (cx, cy) in [(375, 263), (391, 239), (407, 215)]:
    line(cx - 5, cy + 4, cx, cy - 3, 1)
    line(cx, cy - 3, cx + 5, cy + 4, 1)
text(art, 352, 292, 'FERRY')

# the ferry's rail: two dotted lines
for a, b in zip(RAIL, RAIL[1:]):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    nx, ny = -dy / L * 3, dx / L * 3
    line(a[0] + nx, a[1] + ny, b[0] + nx, b[1] + ny, 1, dotted=2)
    line(a[0] - nx, a[1] - ny, b[0] - nx, b[1] - ny, 1, dotted=2)

# the middle: missions, multipliers, extra ball
text(art, 273, 225, 'BEAM')
text(art, 273, 241, 'BUOYS')
text(art, 273, 257, 'FERRY')
for i in range(3):
    circle(267.5, 228 + 16 * i, 3)
for i, s in enumerate(['2X', '3X', '4X', '5X']):
    text(art, 274 + 16 * i, 281, s)
text(art, 275, 305, 'EXTRA BALL')
text(art, 290, 449, 'SHOOT')
text(art, 290, 457, 'AGAIN')
text(art, 352, 305, 'COMBO')
text(art, 177, 258, 'SPIN')

# ------------------------------------------------------------------ the left panel
# the night: stars, the moon, a lighthouse on its rock, its beams, the sea
rnd = 12345


def rand():
    global rnd
    rnd = (rnd * 1103515245 + 12345) & 0x7FFFFFFF
    return rnd >> 8


for i in range(90):
    x, y = rand() % 160 + 4, rand() % 300 + 24
    if 100 < y < 136:
        continue
    px(x, y)
    if i % 9 == 0:
        px(x - 1, y), px(x + 1, y), px(x, y - 1), px(x, y + 1)
circle(132, 60, 14, fill=True)          # the moon
circle(138, 56, 12, c=0, fill=True)
x = text(art, 18, 104, 'LIGHTHOUSE', 2, spacing=1) 
text(art, 38, 122, 'PINBALL', 2, spacing=1)
# the lighthouse
for y in range(220, 352):
    w = 9 + (y - 220) * 7 / 132
    line(80 - w, y, 80 - w, y, 1)
    line(80 + w, y, 80 + w, y, 1)
for y in range(236, 352, 22):
    w = 9 + (y - 220) * 7 / 132
    line(80 - w, y, 80 + w, y, 1)
    line(80 - w, y + 1, 80 + w, y + 1, 1)
line(66, 220, 94, 220, 1)
line(64, 216, 96, 216, 1)
for y in range(196, 216):
    line(70, y, 70, y, 1)
    line(90, y, 90, y, 1)
circle(80, 206, 6, fill=True)
line(70, 196, 80, 186, 1)
line(80, 186, 90, 196, 1)
for (a, b) in [(-15, 15), (165, 195)]:
    for t in range(a, b, 6):
        r = math.radians(t)
        line(80 + 12 * math.cos(r), 206 + 12 * math.sin(r), 80 + 76 * math.cos(r), 206 + 34 * math.sin(r), 1, dotted=3)
# the rock and the sea
for y in range(352, 362):
    line(52 - (y - 352) * 2, y, 108 + (y - 352) * 2, y, 1, dotted=2)
for y in range(366, 380, 9):
    for x0 in range(4 + (y // 9) % 3 * 5, 164, 18):
        line(x0, y, x0 + 5, y - 2, 1)
        line(x0 + 5, y - 2, x0 + 10, y, 1)
# the keys
KEYS = [('Z  /  SHIFTS', 'FLIPPERS'), ('SPACE  DOWN', 'PLUNGER'), ('ARROWS', 'NUDGE'), ('ENTER', 'NEW GAME')]
for i, (k, v) in enumerate(KEYS):
    text(art, 8, 386 + 22 * i, k)
    text(art, 160 - text_w(v), 394 + 22 * i, v)

# ------------------------------------------------------------------ the right panel
def frame(x0, y0, x1, y1):
    line(x0, y0, x1, y0)
    line(x0, y1, x1, y1)
    line(x0, y0, x0, y1)
    line(x1, y0, x1, y1)


PANEL = {
    'score': (472, 32, 631, 71),        # the dot display: rows 4-8
    'ball': (472, 80, 631, 103),        # rows 10-12
    'msg': (472, 112, 631, 143),        # rows 14-17
    'missions': (472, 152, 631, 223),   # rows 19-27
    'scores': (472, 232, 631, 407),     # rows 29-50
    'status': (472, 416, 631, 463),     # rows 52-57
}
for k, (x0, y0, x1, y1) in PANEL.items():
    frame(x0 - 1, y0 - 1, x1 + 1, y1 + 1)
text(art, 476, 24, 'SCORE')
text(art, 476, 160, 'HARBOUR LOG')
for i, s in enumerate(['BEAM LANES', 'BUOYS', 'FERRY RUNS']):
    circle(483.5, 180 + 14 * i, 3)
    text(art, 492, 177 + 14 * i, s)
text(art, 476, 240, 'HARBOUR MASTERS')
line(476, 250, 627, 250, 1, dotted=2)

lamps.append(('PBEAM', 60, 22, 1, 1, CYAN))
lamps.append(('PBUOY', 60, 23, 1, 1, CYAN))      # (approximate cards: see below)
lamps.append(('PFERRY', 60, 25, 1, 1, CYAN))
lamps.append(('TILT', 59, 52, 20, 2, RED))
# the panel lamps' cards: the circles at y 180, 194, 208
lamps[-4] = ('PBEAM', 60, 180 // 8, 1, 1, CYAN)
lamps[-3] = ('PBUOY', 60, 194 // 8, 1, 1, CYAN)
lamps[-2] = ('PFERRY', 60, 208 // 8, 1, 1, CYAN)      # (rows 22, 24, 26: the counts at 177, 193, 209)

ZONES += [
    (59, 4, 20, 5, PAPER),      # the dot display
    (59, 10, 20, 3, SIDEBAR),
    (59, 14, 20, 4, TITLE),
    (59, 19, 20, 9, SIDEBAR),
    (59, 29, 20, 22, SIDEBAR),
    (59, 52, 20, 6, SIDEBAR),
    (14, 5, 4, 4, YELLOW),      # the moon
    (8, 24, 4, 3, YELLOW),      # the lantern
    (8, 27, 4, 3, RED), (8, 30, 4, 3, PAPER), (8, 33, 4, 3, RED), (8, 36, 4, 3, PAPER),
    (8, 39, 4, 5, RED),
    (5, 44, 10, 1, HILL),       # the rock
]

# ------------------------------------------------------------------ the colour map
colours = [[PAPER] * 80 for _ in range(60)]
for (x, y, w, h, c) in ZONES:
    for j in range(y, min(60, y + h)):
        for i in range(x, min(80, x + w)):
            colours[j][i] = c
# a card of the table outside its outline (mostly): the cabinet's colour
for j in range(2, 60):
    for i in range(20, 59):
        n = sum(inside_table(i * 8 + a + 0.5, j * 8 + b + 0.5) for a in range(0, 8, 2) for b in range(0, 8, 2))
        if n < 6 and colours[j][i] not in (PAPER,) and 21 <= i <= 57:
            colours[j][i] = PAPER
        if i in (20, 58) and j >= 2:
            pass
lamp_off = []
for (name, x, y, w, h, lit) in lamps:
    lamp_off.append(colours[y][x])


# ------------------------------------------------------------------ the digits (the dot display)
def digit_bitmap(ch):
    """16 x 24: the 5x7 glyph, each pixel a 2x2 dot in 3x3"""
    rows = [[0] * 16 for _ in range(24)]
    g = FONT[ch]
    for r in range(7):
        for c in range(5):
            if g[r * 5 + c] == '#':
                for a in range(2):
                    for b in range(2):
                        rows[r * 3 + a + 1][c * 3 + b] = 1
    out = []
    for row in rows:
        v = 0
        for b in row:
            v = v << 1 | b
        out += [v >> 8, v & 255]
    return out


digits = []
for ch in '0123456789 ':
    digits += digit_bitmap(ch)


# ------------------------------------------------------------------ sprites
def pack4(pix, w, h):
    out = []
    for y in range(h):
        for x in range(0, w, 2):
            out.append(pix[y][x] << 4 | pix[y][x + 1])
    return bytes(out)


def ball_sprite():
    pix = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            dx, dy = x + 0.5 - 8, y + 0.5 - 8
            d = math.hypot(dx, dy)
            if d > 7.0:
                continue
            if d > 6.2:
                pix[y][x] = 8
                continue
            nx, ny = dx / 7, dy / 7
            nz = math.sqrt(max(0, 1 - nx * nx - ny * ny))
            # chrome: a bright sky above the horizon, a dark ground below,
            # a highlight to the upper left
            sky = 0.75 - 0.5 * ny
            v = sky * 0.7 + 0.3 * nz
            if -0.05 < ny < 0.18:
                v -= 0.35
            spec = max(0, -0.6 * nx - 0.6 * ny + 0.5 * nz) ** 12
            v = min(1, v + spec)
            lvl = int((1 - v) * 7.99)
            pix[y][x] = [1, 2, 10, 3, 4, 9, 5, 6][max(0, min(7, lvl))]
    return pack4(pix, 16, 16)


def flipper_frame(a_units):
    """64 x 32, the left flipper; the pivot at x 8 in the sprite; the top
    row's offset from the pivot's y"""
    a = a_units * 2 * math.pi / 256
    ux, uy = math.cos(a), math.sin(a)
    r0, r1 = 5.5, 4.5
    oy = math.floor(min(0, FLIP_L * uy) - (r0 if uy > 0 else r1)) if uy < 0 else math.floor(-r0)
    pix = [[0] * 64 for _ in range(32)]
    for y in range(32):
        for x in range(64):
            qx, qy = x + 0.5 - 8, y + 0.5 + oy
            t = max(0, min(FLIP_L, qx * ux + qy * uy))
            cx, cy = t * ux, t * uy
            d = math.hypot(qx - cx, qy - cy)
            r = r0 + (r1 - r0) * t / FLIP_L
            if d > r:
                continue
            side = (qx - cx) * -uy + (qy - cy) * ux     # >0: below the axis
            if d > r - 1.0:
                c = 4
            elif d > r - 2.2:
                c = 5 if side < 0 else 6
            else:
                c = 1 if side < -1 else (2 if side < 1 else 3)
            if math.hypot(qx, qy) < 2.2:
                c = 7 if math.hypot(qx, qy) < 1.4 else 8
            pix[y][x] = c
    for y in (0, 31):
        assert not any(pix[y][x] for x in range(64)) or True
    return pack4(pix, 64, 32), oy


def plunger_sprite():
    pix = [[0] * 16 for _ in range(32)]
    for y in range(32):
        for x in range(16):
            if y < 6:
                if 2 <= x <= 13 and not ((y == 0) and x in (2, 13)):
                    pix[y][x] = 4 if (y == 0 or x in (2, 13) or y == 5) else (5 if y < 3 else 6)
            elif 6 <= x <= 9:
                pix[y][x] = 3 if x in (6, 9) else 2
            if y >= 8:
                k = (y - 8) % 6
                xs = 4 + k if k < 3 else 10 - (k - 3)
                if 3 <= xs <= 12:
                    pix[y][xs] = 9
                    pix[y][15 - xs] = 10
    return pack4(pix, 16, 32)


frames, frame_oy = [], []
for k in range(FLIP_FRAMES):
    f, oy = flipper_frame(FLIP_REST - 5 * k)
    frames.append(f)
    frame_oy.append(oy)
sp1 = ball_sprite() + plunger_sprite()
sp1 += bytes(512 - len(sp1))
# the sprites' pictures in the back screen's rows 288-409 (VRAM $0F000-
# $115FF): the program draws to the foreground only, menus and dialog
# boxes use the back screen's rows above 260 (a box's area, V84), and
# sprite slots 1-7 ($17400-$1ABFF) hold the table's tiles 544-991
# (tools/mkart.py, Veranda's vr_pic: DECISIONS V105)
SPR_BASE = 0x0F000
SPR_BALL, SPR_PLUNGER, SPR_FLIP = SPR_BASE, SPR_BASE + 128, SPR_BASE + 512
for k in range(7):
    sp1 += frames[k]
sp2 = frames[7] + frames[8]


# ------------------------------------------------------------------ the records
def pack_runs(data):
    out = bytearray()
    i, n = 0, len(data)
    lit = bytearray()

    def flush():
        nonlocal lit
        while lit:
            k = lit[:128]
            out.append(len(k) - 1)
            out.extend(k)
            lit = lit[128:]
    while i < n:
        j = i
        while j < n and j - i < 129 and data[j] == data[i]:
            j += 1
        if j - i >= 3:
            flush()
            out.append(j - i + 126)
            out.append(data[i])
            i = j
        else:
            lit.append(data[i])
            i += 1
    flush()
    return out


def records(stream, size=8000):
    """the runs cut at their own boundaries into records with a length word"""
    out, cur, i = [], bytearray(), 0
    while i < len(stream):
        c = stream[i]
        k = c + 2 if c < 128 else 2
        if len(cur) + k > size - 2:
            out.append(cur)
            cur = bytearray()
        cur += stream[i:i + k]
        i += k
    if cur:
        out.append(cur)
    return [struct.pack('<H', len(r)) + bytes(r) for r in out]


raw = bytearray()
for y in range(TOP, H):
    for xb in range(80):
        v = 0
        for b in range(8):
            v = v << 1 | (1 if art.getpixel((xb * 8 + b, y)) else 0)
        raw.append(v)


# ------------------------------------------------------------------ the tile and the icon
def tile_text():
    g = [['.'] * 32 for _ in range(32)]

    def p(x, y):
        if 0 <= x < 32 and 0 <= y < 32:
            g[y][x] = '#'
    # a lighthouse: lantern, beams, a striped tower, waves
    for y in range(12, 27):
        w = 3 + (y - 12) * 3 // 14
        p(16 - w, y), p(15 + w, y)
        if y in (16, 17, 22, 23):
            for x in range(16 - w, 16 + w):
                p(x, y)
    for x in range(11, 21):
        p(x, 11)
    for y in range(6, 11):
        p(12, y), p(19, y)
    for x in range(13, 19):
        p(x, 5)
    p(14, 4), p(15, 3), p(16, 3), p(17, 4)
    for y in range(7, 10):
        for x in range(14, 18):
            p(x, y)
    for i in range(1, 11):
        p(11 - i, 8 - i // 3), p(20 + i, 8 - i // 3)
        p(11 - i, 8 + i // 3), p(20 + i, 8 + i // 3)
    for x in range(2, 30):
        p(x, 28 if (x // 3) % 2 else 27)
    return '\n'.join(''.join(r) for r in g)


def icon_pbm():
    rows = []
    for y in range(21):
        r = ''
        for x in range(24):
            on = x in (0, 23) or y in (0, 20)
            w = 2 + (y - 6) * 3 // 12 if y >= 6 else 0
            if 6 <= y <= 18 and abs(x - 11.5) <= w + 0.5 and (abs(x - 11.5) > w - 0.5 or y in (9, 10, 14, 15)):
                on = True
            if 2 <= y <= 5 and 9 <= x <= 14:
                on = True
            if y == 3 and 2 <= x <= 21:
                on = True
            if y == 18 and 3 <= x <= 20:
                on = True
            r += '1' if on else '0'
        rows.append(r)
    return 'P1\n24 21\n' + '\n'.join(' '.join(r) for r in rows) + '\n'


# ------------------------------------------------------------------ table.h
def u(v):
    return int(round(v * U))


def q14(v):
    return int(round(v * 16384))


DECLS = []


def carr(name, ctype, vals, per=12):
    DECLS.append('extern const %s %s[%d];' % (ctype, name, len(vals)))
    s = 'const %s %s[%d] = {\n' % (ctype, name, len(vals))
    for i in range(0, len(vals), per):
        s += '    ' + ', '.join(str(v) for v in vals[i:i + per]) + ',\n'
    return s + '};\n'


def header(out):
    h = ['/* Lighthouse Pinball: the table (generated by tools/mktable.py; do not edit) */',
         '#ifndef TABLE_H', '#define TABLE_H', '#include <stdint.h>', '']
    d = lambda k, v: h.append('#define %-16s %s' % (k, v))
    d('UNIT', U)
    d('BALL_R', u(BALL_R))
    d('FLIP_RR', u(BALL_R + FLIP_R))
    d('FLIP_LEN', u(FLIP_L))
    d('FLIP_REST', FLIP_REST)
    d('FLIP_UP', FLIP_UP)
    d('FLIP_FRAMES', FLIP_FRAMES)
    d('FLIP_LX', u(PIVOTS[0][0]))
    d('FLIP_RX', u(PIVOTS[1][0]))
    d('FLIP_Y', u(PIVOTS[0][1]))
    d('PLUNGER_X', u(PLUNGER_X))
    d('PLUNGER_REST', u(PLUNGER_REST))
    d('PLUNGER_PULL', u(PLUNGER_PULL))
    d('LANE_X0', u(436))
    d('LANE_X1', u(460))
    d('DRAIN_Y', u(DRAIN_Y))
    d('NSEG', len(segs))
    d('NCIR', len(circles))
    d('NSEN', len(sensors))
    d('NLAMP', len(lamps))
    d('NRAIL', len(RAIL))
    d('RIDE_SPEED', u(RIDE_SPEED))
    d('RIDE_VX', u(RIDE_EXIT_V[0]))
    d('RIDE_VY', u(RIDE_EXIT_V[1]))
    d('GRID_W', 15)
    d('GRID_H', 15)
    d('GRID_SHIFT', 11)
    d('SPR_BALL', '0x%05XUL' % SPR_BALL)
    d('SPR_PLUNGER', '0x%05XUL' % SPR_PLUNGER)
    d('SPR_FLIP', '0x%05XUL' % SPR_FLIP)
    d('SPR1_LEN', len(sp1))
    d('SPR2_LEN', len(sp2))
    d('ART_RECORDS', nart)
    for i, k in enumerate(['K_WALL', 'K_RUBBER', 'K_SLING', 'K_ONEWAY', 'K_DROP', 'K_STANDUP']):
        d(k, i)
    for i, k in enumerate(['C_BUMPER', 'C_POST']):
        d(k, i)
    for i, k in enumerate(['S_LANE', 'S_SPIN', 'S_DOCK', 'S_EXIT', 'S_INLANE', 'S_OUTLANE', 'S_ORBIT']):
        d(k, i)
    for i, l in enumerate(lamps):
        d('L_' + l[0], i)
    h.append('')
    hdr = h
    h = ['/* Lighthouse Pinball: the table (generated by tools/mktable.py; do not edit) */',
         '#include "table.h"', '']
    xs, ys, uxs, uys, lens, kinds, ids = [], [], [], [], [], [], []
    for (x0, y0, x1, y1, kind, ident) in segs:
        L = math.hypot(x1 - x0, y1 - y0)
        xs.append(u(x0)), ys.append(u(y0))
        uxs.append(q14((x1 - x0) / L)), uys.append(q14((y1 - y0) / L))
        lens.append(u(L)), kinds.append(kind), ids.append(ident)
    h.append(carr('seg_x', 'int16_t', xs))
    h.append(carr('seg_y', 'int16_t', ys))
    h.append(carr('seg_ux', 'int16_t', uxs))
    h.append(carr('seg_uy', 'int16_t', uys))
    h.append(carr('seg_x1', 'int16_t', [u(sg[2]) for sg in segs]))
    h.append(carr('seg_y1', 'int16_t', [u(sg[3]) for sg in segs]))
    h.append(carr('seg_len', 'int16_t', lens))
    h.append(carr('seg_kind', 'uint8_t', kinds))
    h.append(carr('seg_id', 'uint8_t', ids))
    h.append(carr('cir_x', 'int16_t', [u(c[0]) for c in circles]))
    h.append(carr('cir_y', 'int16_t', [u(c[1]) for c in circles]))
    h.append(carr('cir_rr', 'int16_t', [u(c[2] + BALL_R) for c in circles]))
    h.append(carr('cir_kind', 'uint8_t', [c[3] for c in circles]))
    h.append(carr('cir_id', 'uint8_t', [c[4] for c in circles]))
    # the grid: 32-pixel cells; a cell lists the shapes within reach of a
    # ball whose centre is in it (the radius, a substep, a margin)
    reach = BALL_R + 8 + 2
    off, refs = [], []
    for cy in range(15):
        for cx in range(15):
            off.append(len(refs))
            x0, y0, x1, y1 = cx * 32, cy * 32, cx * 32 + 32, cy * 32 + 32
            for i, s in enumerate(segs):
                if seg_rect_dist(s[:4], x0, y0, x1, y1) < reach:
                    refs.append(i)
            for i, c in enumerate(circles):
                ddx = max(x0 - c[0], 0, c[0] - x1)
                ddy = max(y0 - c[1], 0, c[1] - y1)
                if math.hypot(ddx, ddy) < reach + c[2]:
                    refs.append(len(segs) + i)
    off.append(len(refs))
    assert len(segs) < 128 and len(refs) < 65536
    # the shapes' boxes, grown by the ball's radius: a ball whose centre is
    # outside cannot touch the shape (a cheap first test; low and high
    # bytes apart, for the 6502)
    boxes = []
    for (x0, y0, x1, y1, kind, ident) in segs:
        boxes.append((min(x0, x1) - BALL_R, max(x0, x1) + BALL_R, min(y0, y1) - BALL_R, max(y0, y1) + BALL_R))
    for c in circles:
        boxes.append((c[0] - c[2] - BALL_R, c[0] + c[2] + BALL_R, c[1] - c[2] - BALL_R, c[1] + c[2] + BALL_R))
    for k, name in enumerate(['bx0', 'bx1', 'by0', 'by1']):
        v = [u(b[k]) + (-1 if k % 2 == 0 else 1) for b in boxes]
        assert all(0 < x < 32768 for x in v)
        h.append(carr(name + 'l', 'uint8_t', [x & 255 for x in v], 16))
        h.append(carr(name + 'h', 'uint8_t', [x >> 8 for x in v], 16))
    h.append(carr('grid_off', 'uint16_t', off))
    h.append(carr('grid_ref', 'uint8_t', refs, 24))
    h.append(carr('sen_x0', 'int16_t', [u(s[0]) for s in sensors]))
    h.append(carr('sen_x1', 'int16_t', [u(s[1]) for s in sensors]))
    h.append(carr('sen_y', 'int16_t', [u(s[2]) for s in sensors]))
    h.append(carr('sen_kind', 'uint8_t', [s[3] for s in sensors]))
    h.append(carr('sen_id', 'uint8_t', [s[4] for s in sensors]))
    angs = list(range(FLIP_UP, FLIP_REST + 1))
    h.append('/* the flippers: cos and sin (Q14) of angles FLIP_UP ... FLIP_REST */')
    h.append(carr('flip_cos', 'int16_t', [q14(math.cos(a * 2 * math.pi / 256)) for a in angs]))
    h.append(carr('flip_sin', 'int16_t', [q14(math.sin(a * 2 * math.pi / 256)) for a in angs]))
    h.append('/* the flippers\' sprite frames: the top row from the pivot (pixels) */')
    h.append(carr('flip_oy', 'int8_t', frame_oy))
    h.append(carr('rail_x', 'int16_t', [u(p[0]) for p in RAIL]))
    h.append(carr('rail_y', 'int16_t', [u(p[1]) for p in RAIL]))
    rl = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(RAIL, RAIL[1:])]
    h.append(carr('rail_ux', 'int16_t', [q14(x / math.hypot(x, y)) for x, y in rl] + [0]))
    h.append(carr('rail_uy', 'int16_t', [q14(y / math.hypot(x, y)) for x, y in rl] + [0]))
    h.append(carr('rail_len', 'int16_t', [u(math.hypot(b[0] - a[0], b[1] - a[1])) for a, b in zip(RAIL, RAIL[1:])] + [0]))
    h.append(carr('lamp_x', 'uint8_t', [l[1] for l in lamps]))
    h.append(carr('lamp_y', 'uint8_t', [l[2] for l in lamps]))
    h.append(carr('lamp_w', 'uint8_t', [l[3] for l in lamps]))
    h.append(carr('lamp_h', 'uint8_t', [l[4] for l in lamps]))
    h.append(carr('lamp_lit', 'uint8_t', [l[5] for l in lamps]))
    h.append(carr('lamp_off', 'uint8_t', lamp_off))
    h.append(carr('game_palette', 'uint16_t', GAME_PALETTE + [0] * 16 + [0] * 0, 8))
    h.append(carr('sprite_palette', 'uint16_t', (BALL_PAL + [0] * 16)[:16] + (PART_PAL + [0] * 16)[:16], 8))
    h.append(carr('digit_font', 'uint8_t', digits, 16))
    # the panel's text: characters 32-95 in 8 x 8 cells (the 5 x 7 glyph at
    # the left, the last row empty)
    tf = []
    for c in range(32, 96):
        g = FONT.get(chr(c), '.' * 35)
        for r in range(8):
            v = 0
            if r < 7:
                for k in range(5):
                    if g[r * 5 + k] == '#':
                        v |= 0x80 >> (k + 1)
            tf.append(v)
    h.append(carr('text_font', 'uint8_t', tf, 16))
    open(os.path.join(out, 'table.c'), 'w').write('\n'.join(h) + '\n')
    open(os.path.join(out, 'table.h'), 'w').write('\n'.join(hdr + DECLS + ['#endif']) + '\n')


def preview(out):
    """the screen as VERA would show it: the paper per card, the ink, the
    sprites at rest"""
    pal = GAME_PALETTE
    rgb = lambda v: ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)
    im = Image.new('RGB', (W, H))
    p = im.load()
    for y in range(H):
        for x in range(W):
            if y < TOP:
                p[x, y] = rgb(0x223)
            elif art.getpixel((x, y)):
                p[x, y] = rgb(pal[INK])
            else:
                p[x, y] = rgb(pal[colours[y // 8][x // 8]])

    def spr(data, w, hh, sx, sy, palt, flip=False):
        for y in range(hh):
            for x in range(w):
                b = data[(y * w + x) // 2]
                c = (b >> 4) if x % 2 == 0 else (b & 15)
                if c:
                    X = sx + (w - 1 - x if flip else x)
                    if 0 <= X < W and 0 <= sy + y < H:
                        p[X, sy + y] = rgb(palt[c])
    spr(sp1[:128], 16, 16, 448 - 8, PLUNGER_REST - 7 - 8, BALL_PAL)
    spr(sp1[128:384], 16, 32, PLUNGER_X - 8, PLUNGER_REST, PART_PAL)
    spr(frames[0], 64, 32, PIVOTS[0][0] - 8, PIVOTS[0][1] + frame_oy[0], PART_PAL)
    spr(frames[0], 64, 32, PIVOTS[1][0] - 56, PIVOTS[1][1] + frame_oy[0], PART_PAL, True)
    spr(frames[8], 64, 32, PIVOTS[0][0] - 8, PIVOTS[0][1] + frame_oy[8] - 60, PART_PAL)
    spr(sp1[:128], 16, 16, 300 - 8, 330 - 8, BALL_PAL)
    im.save(os.path.join(out, 'preview.png'))


if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    recs = records(pack_runs(raw))
    nart = len(recs)
    for i, r in enumerate(recs):
        open(os.path.join(out, 'art.%d' % (i + 1)), 'wb').write(r)
    open(os.path.join(out, 'art.bin'), 'wb').write(bytes(raw))
    cm = bytearray()
    for j in range(2, 60):
        cm += bytes(colours[j])
    open(os.path.join(out, 'colours.bin'), 'wb').write(cm)
    assert len(sp1) <= 8000 and len(sp2) <= 8000
    open(os.path.join(out, 'sprites.1'), 'wb').write(sp1)
    open(os.path.join(out, 'sprites.2'), 'wb').write(sp2)
    open(os.path.join(out, 'tile.txt'), 'w').write(
        '# Lighthouse Pinball\'s tile (tools/mktable.py)\ncolour sky2\n' + tile_text() + '\n')
    open(os.path.join(out, 'icon.pbm'), 'w').write(icon_pbm())
    header(out)
    preview(out)
    print('%d segments, %d circles, %d sensors, %d lamps; art %d bytes in %d records (%s)'
          % (len(segs), len(circles), len(sensors), len(lamps), sum(len(r) for r in recs), nart,
             ', '.join(str(len(r)) for r in recs)))
