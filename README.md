# Lighthouse Pinball

An original pinball table for Veranda, the GEOS-API desktop for the
Commander X16: a harbour at night, with a lighthouse, a ferry ramp and fog
banks. Native Veranda application (C with cc65, assembly with ca65), MIT
licence.

The table, its rules, art and sounds are this program's own. It is not a
port or a remake of any other pinball game.

## Playing

| Key | |
|---|---|
| Z or left Shift | left flipper |
| / or right Shift | right flipper |
| Space or Down (hold, let go) | the plunger: the longer it is held, the harder the launch |
| Left, Right, Up | nudge (too much tilts the table) |
| Enter | a new game |

The mouse runs the menus: **Lighthouse** (about), **Game** (new game,
high scores, quit) and **Options** (sound on or off). Opening a menu
pauses the game.

**The table.** At the top are the four **BEAM** lanes (B E A M). The
flippers move the lit lanes along. On the left is the **harbour loop**, an
orbit with a spinner, and three **buoys**, drop targets in front of its
guide. In the middle are three **fog banks**, the pop bumpers. On the
right is the **ferry ramp**: a shot that reaches its dock rides the
ferry's rail across the table and comes down in the left inlane. The
**lighthouse** island has its lantern target facing down. Slingshots sit
above the flippers, with inlanes and outlanes beside them.

**The missions.**

- Light the BEAM lanes: each completion also raises the bonus multiplier,
  up to 5X.
- Knock down the three buoys.
- Ride the ferry three times. A ride within four seconds of the last one
  is a **combo**: it counts twice and scores 15,000.

With all three missions done, the lighthouse is lit. Its lantern target is
then the **jackpot** (50,000 times the level). The first jackpot of a game
also gives an **extra ball**.

Three balls a game. Ball save covers the first ten seconds after a launch.
The bonus (lanes, buoys, ferries) is multiplied at the drain, unless the
table tilted. The five best scores, with a name typed in at game over, are
kept with the sound setting in `/VERANDA/.SYSTEM/Lighthouse Pinball Scores`
(the SDK's `vrpath.h`, Veranda's DECISIONS V71).

## How it works

- **Physics** (`src/phys.c`) runs one tick a frame, 60 a second, in fixed
  point: 1/64 pixel, Q14 unit vectors, Q8.8 angles.
  - **Collisions.** The ball (radius 7) moves in 1, 2 or 4 substeps of at
    most 6 pixels, below its radius. That is a swept test by conservative
    advancement, so it cannot tunnel through anything. Each substep tests
    the shapes in its 32-pixel grid cell whose bounding box holds the ball:
    segments with round ends, and circles for the fog banks.
  - **Contacts** push the ball out along the normal and reflect the normal
    velocity, relative to the moving surface, with each shape's
    restitution. Slingshots and fog banks kick.
  - **Flippers** are capsules turning about their pivots. The surface
    velocity at the contact point (angular velocity times distance) is what
    kicks the ball.
  - **Sensors** are lines the ball's centre crosses: the lanes, the
    spinner, the ramp's dock, the shooter lane's exit, the in- and outlanes
    and the loop.
- **Rules** are in `src/game.c`: one tick at a time from the keys held,
  with no clock and no randomness, so the same keys always give the same
  game.
- **One source for the table.** `tools/mktable.py` defines it once and
  generates from that:
  - the physics' shapes, grid and sensors (`table.c`);
  - the sprites' and lamps' positions; `tools/mkart.py` then draws the
    table in full colour from the same geometry (a night harbour, chrome
    guides, fog-bank bumpers, the ferry's deck, the lighthouse; every
    lamp off, and every lamp lit) and converts both pictures together
    with the Veranda SDK's `mkwall.py --tiles` into VERA 4 bpp tiles
    (928: tiles 16-479 and 544-991), a map each and palette rows 4-13
    (six records; record 1 the rows);
  - the sprites, 4 bpp: the chrome ball (16x16), nine flipper angles
    (64x32, the right flipper is the left one mirrored by VERA) and the
    plunger;
  - the dot-display digits, the panel's 5x7 font, the app tile and icon.
- **On the X16** (`veranda/`):
  - The table is a picture in VERA's tiles on layer 0, under the bitmap
    (Veranda's `vr_pic`, its DECISIONS V105), loaded once; the bitmap
    holds only the panel's text. The ball, flippers and plunger are VERA
    sprites, their pictures in the back screen's lower rows (VRAM
    `$0F000`; sprite slots 1-7 hold tiles).
  - Lamps are their cards' map entries swapped between the off and the
    lit picture's (2 bytes a card).
  - The score is a dot display blitted into the bitmap. Panel text uses
    VERA's increment-by-80 mode.
  - Keys come from the ROM's key hook, which sees presses and releases.
    The frame interrupt counts frames and plays the PSG sounds (voices
    0-4; Veranda's own sounds use 14 and 15).
  - `fix.s` holds the multiply (quarter squares), square root and
    division. It gives the same bits as `src/fix.c` on the host.
- **The palette.** While it runs, the game uses its own night palette with
  the Look 2 names (`sdk/lib/theme.h`): a light ink on dark papers, and the
  sprites' colours in entries 32-63, the table's in rows 4-13. It puts
  back the palette it found when it quits. Put aside (Alt + Tab) and
  back, it writes its rows again (`vr_pic_back`). The menu bar is Veranda Look 2's (`theme_menus`).

## Building

```
tools/gate.sh                       # everything, checked (see below)
make -C veranda SDK=<an installed Veranda SDK>
```

The gate exports Veranda's `main` (or `VREF`: the full-colour table
needs an SDK with `vrpic.h`, Veranda's V105) into `build/vsrc`, builds it
there and installs its SDK into `build/sdk`. The release is
`build/gate/app/Lighthouse Pinball.cvt` (a long name: its GEOS name is
its first 16 characters, `Lighthouse Pinba`; the class is `Lighthouse`).

## The gate

`tools/gate.sh` runs everything under the X16 ROM R49.

- **Host tests** (`host/tests.c`):
  - no tunnelling: about 117,000 shots at top speed in every direction
    from all over the table, plus about 2,000 aimed at every wall, end and
    fog bank;
  - a ball rests on a raised flipper without jitter;
  - kicks from the flippers, fog banks and slingshots;
  - plunger strength.
- **Two scripted games** played by the host's autopilot (`host/sim.c`).
  The X16 replays each one through its key hook and must show:
  - a launch;
  - flipper hits;
  - a score;
  - drains;
  - game over;
  - the high score, typed into the dialog box, saved and read back;
  - no frame late (60 fps at 8 MHz, from the emulator's cycle counter);
  - its log equal to the host's, line for line (deterministic physics);
  - the palette put back.
- **Screenshots in play.**
- **The real mouse** (`-mousescript`): the Game menu, Options > Sound and
  Game > Quit to the shell, which then shows Look 2's palette.
- **Put aside and back** (the test build asks the switcher at a tick, as
  Alt + Tab does): the shell with the game in its dock, the dock's item
  clicked: the game back in tile mode with its palette rows, played on.
