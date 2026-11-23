/*
 * Lighthouse Pinball: the physics' tests on the host (the same src/ C as
 * the X16 runs; built with -DPB_PROBE, so phys_probe() sees the ball
 * after every substep).
 *
 *   T1 no tunnelling: balls at the top speed (15 pixels a tick on each
 *      axis, and every direction on a circle of that speed) from a grid of
 *      places all over the table, the flippers flapping: after every
 *      substep the centre's path since the last one crosses no wall, no
 *      drop target standing, no flipper, the one-way gate only upwards;
 *      the centre stays out of every fog bank and at least 3 pixels from
 *      every wall (the ball overlaps a wall by 4 pixels at most)
 *   T2 aimed shots: at the top speed straight at every wall's middle and
 *      ends, every post and fog bank: the same checks
 *   T3 a ball on a raised flipper comes to rest (no jitter)
 *   T4 a flipper kicks: a ball resting on it leaves fast and upwards
 *   T5 a fog bank kicks; a slingshot kicks
 *   T6 the plunger: a full pull leaves the shooter lane, a short one does
 *      not
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/pb.h"

extern s16 f_cur[2];
static double px0, py0;             /* the centre at the last probe */
static long probes, fails;
static double worst_pen;
static const char *what;
static int quiet;

static int cross(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy)
{
    double d1 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx);
    double d2 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
    double d3 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    double d4 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

static double seg_dist(double x, double y, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy, t;
    t = ((x - ax) * dx + (y - ay) * dy) / L2;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return hypot(x - ax - t * dx, y - ay - t * dy);
}

static long nfail[8];
static void fail(const char *why, double x, double y)
{
    ++fails;
    int c = why[0] == 'd' ? 1 : why[5] == 'a' ? 2 : why[7] == 'f' ? 3 : why[0] == 'o' ? 4 : 0;
    if (++nfail[c] <= 4 && !quiet)
        printf("FAIL %s: %s at (%.2f, %.2f) from (%.2f, %.2f), v (%d, %d)\n", what, why, x, y, px0, py0, b_vx, b_vy);
}

/* the flipper's axis now (as phys.c: the angle's table) */
static void flip_axis(int f, double *ax, double *ay, double *bx, double *by)
{
    int idx = (f_cur[f] >> 8) - FLIP_UP;
    double c, s;
    if (idx < 0)
        idx = 0;
    if (idx > FLIP_REST - FLIP_UP)
        idx = FLIP_REST - FLIP_UP;
    c = flip_cos[idx] / 16384.0;
    s = flip_sin[idx] / 16384.0;
    *ax = (f ? FLIP_RX : FLIP_LX) / 64.0;
    *ay = FLIP_Y / 64.0;
    *bx = *ax + (f ? -c : c) * FLIP_LEN / 64.0;
    *by = *ay + s * FLIP_LEN / 64.0;
}

void phys_probe(void)
{
    double x = b_x / 64.0, y = b_y / 64.0, d;
    int i, f;
    if (quiet)
        return;                     /* (T3-T6 test other things) */
    ++probes;
    for (i = 0; i < NSEG; ++i) {
        double ax = seg_x[i] / 64.0, ay = seg_y[i] / 64.0, bx = seg_x1[i] / 64.0, by = seg_y1[i] / 64.0;
        if (seg_kind[i] == K_DROP && !drop_up[seg_id[i]])
            continue;
        if (seg_kind[i] == K_ONEWAY) {
            /* crossing it downwards (into the lane) is not allowed */
            if (cross(px0, py0, x, y, ax, ay, bx, by) && y > py0)
                fail("through the one-way gate", x, y);
            continue;
        }
        if (cross(px0, py0, x, y, ax, ay, bx, by))
            fail("through a wall", x, y);
        d = seg_dist(x, y, ax, ay, bx, by);
        if (7.0 - d > worst_pen)
            worst_pen = 7.0 - d;
        if (d < 3.0)
            fail("deep in a wall", x, y);
    }
    for (i = 0; i < NCIR; ++i) {
        d = hypot(x - cir_x[i] / 64.0, y - cir_y[i] / 64.0);
        if (d < (cir_rr[i] - BALL_R) / 64.0 + 3.0)
            fail("into a fog bank", x, y);
    }
    for (f = 0; f < 2; ++f) {
        double ax, ay, bx, by;
        flip_axis(f, &ax, &ay, &bx, &by);
        d = seg_dist(x, y, ax, ay, bx, by);
        if (d < 6.0)
            fail("into a flipper", x, y);
    }
    if ((x < 172 || x > 460 || y < 20) && y < 474)
        fail("out of the table", x, y);
    px0 = x;
    py0 = y;
}

/* a place is free: inside, 9 pixels from every shape and flipper, not in
   the island, not in the shooter lane */
static int free_at(double x, double y)
{
    int i;
    double dx, dy;
    if (x < 182 || x > 432 || y < 40 || y > 440)
        return 0;
    dx = (x - 316) / 144.0;
    dy = (y - 140) / 112.0;
    if (y < 140 && dx * dx + dy * dy > 0.85)
        return 0;
    if (x > 372 && x < 400 && y > 92 && y < 134)
        return 0;
    for (i = 0; i < NSEG; ++i)
        if (seg_dist(x, y, seg_x[i] / 64.0, seg_y[i] / 64.0, seg_x1[i] / 64.0, seg_y1[i] / 64.0) < 9)
            return 0;
    for (i = 0; i < NCIR; ++i)
        if (hypot(x - cir_x[i] / 64.0, y - cir_y[i] / 64.0) < cir_rr[i] / 64.0 + 2)
            return 0;
    for (i = 0; i < 2; ++i) {
        double ax, ay, bx, by;
        flip_axis(i, &ax, &ay, &bx, &by);
        if (seg_dist(x, y, ax, ay, bx, by) < 14)
            return 0;
    }
    return 1;
}

static void start(double x, double y, int vx, int vy)
{
    phys_init();
    b_on = 1;
    b_x = (s16)(x * 64);
    b_y = (s16)(y * 64);
    b_vx = (s16)vx;
    b_vy = (s16)vy;
    px0 = x;
    py0 = y;
}

static void run(int ticks, int flap)
{
    int t;
    for (t = 0; t < ticks && b_on; ++t) {
        if (flap) {
            f_hold[0] = (t / flap) & 1;
            f_hold[1] = ((t + flap / 2) / flap) & 1;
        }
        phys_tick();
    }
}

static int check(const char *name, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok)
        ++fails;
    return ok;
}

int main(void)
{
    double x, y, a;
    long shots = 0, f0;
    int k, i, flap;

    /* T1 */
    what = "T1";
    f0 = fails;
    for (y = 44; y <= 436; y += 6)
        for (x = 184; x <= 430; x += 6) {
            if (!free_at(x, y))
                continue;
            for (k = 0; k < 16; ++k) {
                a = k * 2 * M_PI / 16;
                for (flap = 0; flap <= 7; flap += 7) {
                    start(x, y, (int)(VMAX * cos(a)), (int)(VMAX * sin(a)));
                    f_cur[0] = f_ang[0];
                    f_cur[1] = f_ang[1];
                    run(90, flap);
                    ++shots;
                    start(x, y, k & 1 ? VMAX : -VMAX, k & 2 ? VMAX : -VMAX);
                    run(40, flap);
                    ++shots;
                }
            }
        }
    printf("      T1 failures: through %ld, deep %ld, fog %ld, flipper %ld, out %ld\n", nfail[0], nfail[1], nfail[2], nfail[3], nfail[4]);
    printf("      T1: %ld shots at the top speed, %ld substeps checked\n", shots, probes);
    check("T1 no tunnelling: walls, the gate, targets, fog banks, flippers, at the top speed in every direction", fails == f0);

    /* T2: aimed at every shape's middle and ends from 60 pixels away */
    what = "T2";
    f0 = fails;
    shots = 0;
    for (i = 0; i < NSEG; ++i) {
        double ax = seg_x[i] / 64.0, ay = seg_y[i] / 64.0, bx = seg_x1[i] / 64.0, by = seg_y1[i] / 64.0;
        double tx, ty, nx = -(by - ay), ny = bx - ax, L = hypot(nx, ny), s;
        int e, side;
        nx /= L;
        ny /= L;
        for (e = 0; e < 3; ++e) {
            tx = e == 0 ? (ax + bx) / 2 : e == 1 ? ax : bx;
            ty = e == 0 ? (ay + by) / 2 : e == 1 ? ay : by;
            for (side = -1; side <= 1; side += 2)
                for (s = 20; s <= 60; s += 20) {
                    x = tx + side * nx * s;
                    y = ty + side * ny * s;
                    if (!free_at(x, y))
                        continue;
                    for (k = -2; k <= 2; ++k) {
                        double dx = tx - x, dy = ty - y, d = hypot(dx, dy), c = cos(k * 0.2), sn = sin(k * 0.2);
                        double vx = (dx * c - dy * sn) / d, vy = (dx * sn + dy * c) / d;
                        double m = fabs(vx) > fabs(vy) ? fabs(vx) : fabs(vy);
                        start(x, y, (int)(VMAX * vx / m), (int)(VMAX * vy / m));
                        run(30, 0);
                        ++shots;
                    }
                }
        }
    }
    for (i = 0; i < NCIR; ++i)
        for (k = 0; k < 24; ++k) {
            a = k * 2 * M_PI / 24;
            x = cir_x[i] / 64.0 + 40 * cos(a);
            y = cir_y[i] / 64.0 + 40 * sin(a);
            if (!free_at(x, y))
                continue;
            start(x, y, (int)(-VMAX * cos(a)), (int)(-VMAX * sin(a)));
            run(30, 0);
            ++shots;
        }
    printf("      T2: %ld aimed shots\n", shots);
    check("T2 aimed at every wall's middle and ends and every fog bank at the top speed: none passes", fails == f0);
    printf("      the deepest a ball went into a wall: %.2f pixels (its radius 7)\n", worst_pen);

    quiet = 1;
    /* T3: a ball dropped on the raised left flipper rests */
    {
        s16 x0, y0;
        int moved = 0, t;
        start(262, 380, 0, 0);
        f_hold[0] = 1;
        for (t = 0; t < 240; ++t)
            phys_tick();
        x0 = b_x;
        y0 = b_y;
        for (t = 0; t < 60; ++t) {
            phys_tick();
            if (abs(b_x - x0) > 32 || abs(b_y - y0) > 32)
                moved = 1;
        }
        printf("      T3: at rest at (%.1f, %.1f), v (%d, %d)\n", b_x / 64.0, b_y / 64.0, b_vx, b_vy);
        check("T3 a ball on a raised flipper comes to rest: still, no jitter, over a second", b_on && !moved &&
              abs(b_vx) < 24 && abs(b_vy) < 24);

    }
    /* T4: a ball on the lowered left flipper, 28 pixels out from the pivot:
       a flip sends it upwards fast */
    {
        int t, best = 0;
        double c = flip_cos[FLIP_REST - FLIP_UP] / 16384.0, sn = flip_sin[FLIP_REST - FLIP_UP] / 16384.0;
        start(FLIP_LX / 64.0 + 28 * c + 12.5 * sn, FLIP_Y / 64.0 + 28 * sn - 12.5 * c, 0, 0);
        f_hold[0] = 1;
        for (t = 0; t < 8; ++t) {
            phys_tick();
            if (-b_vy > best)
                best = -b_vy;
        }
        printf("      T4: the flipper's kick: %.1f pixels a tick upwards\n", best / 64.0);
        check("T4 a flipper kicks a ball on it upwards at 9 pixels a tick or more", best >= 9 * 64);
    }
    /* T5: a ball falling on a fog bank; on the left slingshot */
    {
        int t, kick = 0;
        start(cir_x[2] / 64.0 + 3, cir_y[2] / 64.0 - 40, 0, 0);
        for (t = 0; t < 40 && !kick; ++t) {
            phys_tick();
            for (i = 0; i < ev_n; ++i)
                if (ev_kind[i] == EV_BUMPER)
                    kick = 1;
        }
        printf("      T5: off the fog bank at %.1f pixels a tick\n", hypot(b_vx, b_vy) / 64.0);
        check("T5 a fog bank kicks the ball away at 7 pixels a tick or more", kick && hypot(b_vx, b_vy) > 7 * 64);
        kick = 0;
        start(250, 330, -300, 300);
        for (t = 0; t < 30; ++t) {
            phys_tick();
            for (i = 0; i < ev_n; ++i)
                if (ev_kind[i] == EV_SLING)
                    kick = 1;
        }
        check("T5 a slingshot kicks", kick);
    }
    /* T6: the plunger */
    {
        int t, out;
        game_init();
        for (k = 0; k < 2; ++k) {
            u8 in;
            game_init();
            game_tick(IN_START);
            game_tick(0);
            for (t = 0; t < 30; ++t)
                game_tick(0);
            in = IN_PLUNGE;
            for (t = 0; t < (k ? 60 : 8); ++t)
                game_tick(in);
            out = 0;
            for (t = 0; t < 200; ++t) {
                game_tick(0);
                for (i = 0; i < ev_n; ++i)
                    if (ev_kind[i] == EV_EXIT)
                        out = 1;
            }
            check(k ? "T6 a full pull of the plunger shoots the ball out of the lane" :
                  "T6 a short pull does not: the ball falls back on the plunger", k ? out : !out && G.ball_state == B_LANE);
        }
    }
    printf("%s  physics: %ld failures\n", fails ? "FAIL" : "ok  ", fails);
    return fails ? 1 : 0;
}
