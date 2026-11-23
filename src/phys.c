/*
 * Lighthouse Pinball: the physics.
 *
 * A tick: the flippers turn, gravity, then the ball moves in 1, 2 or 4
 * substeps so that no substep carries it more than 6 pixels (max(|vx|,
 * |vy|) + min / 2, at least the speed, below 384 units a substep): less
 * than the ball's radius (7), nor (with a turning flipper's tip, 5 a
 * substep) the sum of the ball's and the flipper's radii (12), so its
 * centre can never pass a wall or a flipper between two tests (a swept test by conservative advancement: no
 * tunnelling, whatever the speed; host/tests.c checks it). After each
 * substep the ball is tested against the shapes of its grid cell (the
 * table's walls as segments with round ends, the fog banks as circles),
 * the flippers (capsules turning about their pivots: their surface
 * velocity at the contact point is the angular velocity times the
 * distance from the pivot, so a flipper kicks) and the plunger. A contact
 * pushes the ball out along the normal and reflects the normal velocity
 * (relative to the surface) with the shape's restitution; slingshots and
 * fog banks kick. The sensors (lanes, the spinner, the ramp's dock ...)
 * are lines the ball's centre crosses during the tick.
 *
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include "pb.h"

#ifdef PB_PROBE
void phys_probe(void);              /* host/tests.c: after each substep */
s16 f_cur[2];                       /* the flippers' angles in this substep */
#define PROBE() (f_cur[0] = a0[0] + (s16)((f_da[0] * (s16)(i + 1)) >> k), \
                 f_cur[1] = a0[1] + (s16)((f_da[1] * (s16)(i + 1)) >> k), phys_probe())
#else
#define PROBE()
#endif

s16 b_x, b_y, b_vx, b_vy;
u8 b_on;
s16 f_ang[2], f_da[2];
u8 f_hold[2];
s16 p_top;
u8 drop_up[3];
u8 substeps;
u8 ev_n, ev_kind[EV_MAX], ev_id[EV_MAX];

/* restitution (Q8) and kicks */
#define E_WALL      110
#define E_RUBBER    170
#define E_FLIP      90
#define E_TARGET    80
#define REST_V      48              /* below: no bounce, it rests */
#define KICK_SLING  430
#define KICK_BUMPER 520
#define KICK_MIN    80              /* a sling kicks above this approach */

static void event(u8 k, u8 id)
{
    if (ev_n < EV_MAX) {
        ev_kind[ev_n] = k;
        ev_id[ev_n] = id;
        ++ev_n;
    }
}

void phys_init(void)
{
    b_on = 0;
    f_ang[0] = f_ang[1] = FLIP_REST * 256;
    f_da[0] = f_da[1] = 0;
    p_top = PLUNGER_REST;
    drop_up[0] = drop_up[1] = drop_up[2] = 1;
}

/* the contact's state (statics: cc65 reaches them fastest) */
static s16 nx, ny, pen;             /* the normal (Q14), the depth */
static s16 svx, svy;                /* the surface's velocity */
static s16 dx, dy, d, t;
static s32 d2;

/* push the ball out along (nx, ny) by pen; reflect the normal velocity
   relative to the surface; e restitution (Q8), kick a minimum speed out.
   Returns the speed of approach (0: it was leaving) */
static s16 resolve(u8 e, s16 kick)
{
    s16 vn, nv, rx, ry;
    b_x += mul14(pen, nx);
    b_y += mul14(pen, ny);
    rx = b_vx - svx;
    ry = b_vy - svy;
    vn = dot14(rx, nx, ry, ny);
    if (vn >= 0)
        return 0;
    nv = (s16)(mul16(-vn, e) >> 8);
    if (-vn < REST_V)
        nv = 0;
    if (kick && -vn > KICK_MIN && nv < kick)
        nv = kick;
    nv -= vn;                       /* the change of the normal velocity */
    b_vx += mul14(nv, nx);
    b_vy += mul14(nv, ny);
    if (b_vx > VMAX)
        b_vx = VMAX;
    else if (b_vx < -VMAX)
        b_vx = -VMAX;
    if (b_vy > VMAX)
        b_vy = VMAX;
    else if (b_vy < -VMAX)
        b_vy = -VMAX;
    return -vn;
}

/* a round contact: the ball's centre at (dx, dy) from a point, rr the sum
   of the radii; the normal from the point; 1 if touching */
static u8 round_contact(s16 rr)
{
    u16 dist;
    if (dx >= rr || dx <= -rr || dy >= rr || dy <= -rr)
        return 0;
    d2 = mul16(dx, dx) + mul16(dy, dy);
    if (d2 >= mul16(rr, rr))
        return 0;
    dist = isqrt32((u32)d2);
    if (dist == 0)
        return 0;
    nx = divq14(dx, dist);
    ny = divq14(dy, dist);
    pen = rr - (s16)dist;
    return 1;
}

static void hit_seg(u8 i)
{
    s16 ux, uy, v;
    u8 k;
    dx = b_x - seg_x[i];
    dy = b_y - seg_y[i];
    ux = seg_ux[i];
    uy = seg_uy[i];
    d = dot14(dy, ux, dx, -uy);                         /* along the normal (-uy, ux) */
    if (d >= BALL_R || d <= -BALL_R)
        return;
    k = seg_kind[i];
    if (k == K_ONEWAY && d < 0)
        return;
    if (k == K_DROP && !drop_up[seg_id[i]])
        return;
    t = dot14(dx, ux, dy, uy);
    if (t < 0) {
        if (!round_contact(BALL_R))
            return;
    } else if (t > seg_len[i]) {
        dx = b_x - seg_x1[i];
        dy = b_y - seg_y1[i];
        if (!round_contact(BALL_R))
            return;
    } else if (d >= 0) {
        nx = -uy;
        ny = ux;
        pen = BALL_R - d;
    } else {
        nx = uy;
        ny = -ux;
        pen = BALL_R + d;
    }
    svx = svy = 0;
    switch (k) {
    case K_SLING:
        if (resolve(E_RUBBER, KICK_SLING) > KICK_MIN)
            event(EV_SLING, seg_id[i]);
        break;
    case K_RUBBER:
        resolve(E_RUBBER, 0);
        break;
    case K_DROP:
        if (resolve(E_TARGET, 0) > REST_V) {
            drop_up[seg_id[i]] = 0;
            event(EV_DROP, seg_id[i]);
        }
        break;
    case K_STANDUP:
        if (resolve(E_TARGET, 0) > REST_V)
            event(EV_STANDUP, seg_id[i]);
        break;
    default:
        v = resolve(E_WALL, 0);
        if (v > 400)
            event(EV_WALL, 0);
        break;
    }
}

static void hit_cir(u8 i)
{
    s16 rr = cir_rr[i];
    dx = b_x - cir_x[i];
    dy = b_y - cir_y[i];
    if (!round_contact(rr))
        return;
    svx = svy = 0;
    if (cir_kind[i] == C_BUMPER) {
        resolve(E_RUBBER, KICK_BUMPER);
        event(EV_BUMPER, cir_id[i]);
    } else
        resolve(E_WALL, 0);
}

/* flipper f at angle a (Q8.8) */
static void hit_flip(u8 f, s16 a)
{
    s16 c, s, ux, uy, dux, duy, cx, cy, w;
    s8 idx = (s8)(a >> 8) - FLIP_UP;
    if (idx < 0)
        idx = 0;
    else if (idx > FLIP_REST - FLIP_UP)
        idx = FLIP_REST - FLIP_UP;
    c = flip_cos[(u8)idx];
    s = flip_sin[(u8)idx];
    if (f) {
        ux = -c;
        dx = b_x - FLIP_RX;
        dux = s;
    } else {
        ux = c;
        dx = b_x - FLIP_LX;
        dux = -s;
    }
    uy = s;
    duy = c;
    dy = b_y - FLIP_Y;
    t = dot14(dx, ux, dy, uy);
    if (t < 0)
        t = 0;
    else if (t > FLIP_LEN)
        t = FLIP_LEN;
    cx = mul14(t, ux);
    cy = mul14(t, uy);
    dx -= cx;
    dy -= cy;
    if (t > 0 && t < FLIP_LEN) {
        d = dot14(dy + cy, ux, dx + cx, -uy);
        if (d >= FLIP_RR || d <= -FLIP_RR)
            return;
        if (d >= 0) {
            nx = -uy;
            ny = ux;
            pen = FLIP_RR - d;
        } else {
            nx = uy;
            ny = -ux;
            pen = FLIP_RR + d;
        }
    } else if (!round_contact(FLIP_RR))
        return;
    /* the surface's velocity: t times the angular velocity, along du/da;
       the angle's units: 2 pi / 65536 a step (Q8.8 of 1/256 turn) */
    w = mul14(mul14(t, f_da[f]), 25736);      /* x pi / 2: t da 2 pi / 65536 */
    svx = mul14(w, dux);
    svy = mul14(w, duy);
    if (resolve(E_FLIP, 0) > 200 && f_da[f])
        event(EV_FLIPHIT, f);
}

#ifndef __CC65__
/* the shapes of cell c whose boxes hold the ball's centre, into cand
   (x16.s on the X16) */
u8 cand[32];

u8 scan(u8 c)
{
    u16 g = grid_off[c], e = grid_off[c + 1];
    u8 n = 0, r;
    u16 x = (u16)b_x, y = (u16)b_y;
    for (; g < e; ++g) {
        r = grid_ref[g];
        if (x < (u16)(bx0l[r] | bx0h[r] << 8) || x > (u16)(bx1l[r] | bx1h[r] << 8) ||
            y < (u16)(by0l[r] | by0h[r] << 8) || y > (u16)(by1l[r] | by1h[r] << 8))
            continue;
        cand[n++] = r;
    }
    return n;
}
#endif

u8 phys_on_plunger(void)
{
    return b_on && b_x > LANE_X0 && b_y + BALL_R >= p_top - 2 * UNIT;
}

static void sensors(s16 x0, s16 y0)
{
    u8 i;
    s16 y;
    for (i = 0; i < NSEN; ++i) {
        y = sen_y[i];
        if ((y0 < y) == (b_y < y))
            continue;
        if (b_x < sen_x0[i] || b_x > sen_x1[i])
            continue;
        switch (sen_kind[i]) {
        case S_LANE:
            event(EV_LANE, sen_id[i]);
            break;
        case S_SPIN:
            event(EV_SPIN, (u8)(1 + ((b_vy < 0 ? -b_vy : b_vy) >> 7)));
            break;
        case S_DOCK:
            if (b_y < y)
                event(EV_DOCK, 0);
            break;
        case S_EXIT:
            if (b_y < y)
                event(EV_EXIT, 0);
            break;
        case S_INLANE:
            if (b_y > y)
                event(EV_INLANE, sen_id[i]);
            break;
        case S_OUTLANE:
            if (b_y > y)
                event(EV_OUTLANE, sen_id[i]);
            break;
        case S_ORBIT:
            if (b_y < y)
                event(EV_ORBIT, 0);
            break;
        }
    }
    (void)x0;
}

void phys_tick(void)
{
    s16 a0[2], x0, y0, s, ax, ay;
    u8 f, k, n, i, j, cell, e, mask, rx, ry;

    ev_n = 0;
    /* the flippers */
    for (f = 0; f < 2; ++f) {
        a0[f] = f_ang[f];
        if (f_hold[f]) {
            s = f_ang[f] - FLIP_SPEED_UP;
            if (s < FLIP_UP * 256)
                s = FLIP_UP * 256;
        } else {
            s = f_ang[f] + FLIP_SPEED_DOWN;
            if (s > FLIP_REST * 256)
                s = FLIP_REST * 256;
        }
        f_da[f] = s - f_ang[f];
        f_ang[f] = s;
    }
    if (!b_on) {
        substeps = 0;
        return;
    }
    b_vy += GRAVITY;
    x0 = b_x;
    y0 = b_y;

    /* substeps */
    ax = b_vx < 0 ? -b_vx : b_vx;
    ay = b_vy < 0 ? -b_vy : b_vy;
    /* max + min / 2 is at least the speed (|v|): substeps of 6 pixels at
       most; the top speed (15, 15) takes 4 of 5.6 */
    s = ax > ay ? ax + (ay >> 1) : ay + (ax >> 1);
    k = s < 384 ? 0 : s < 768 ? 1 : s < 1536 ? 2 : 3;
    /* a turning flipper's tip: 10 pixels a tick at most, 5 a substep of
       two: with the ball's 6, less than their radii's sum (12) */
    if (!k && (f_da[0] || f_da[1]) && b_y > FLIP_Y - 40 * UNIT)
        k = 1;
    n = (u8)(1 << k);
    mask = n - 1;
    substeps = n;
    for (i = 0; i < n; ++i) {
        /* the ball: v / n a substep, the remainder spread over the first ones */
        rx = (u8)(b_vx & mask);
        ry = (u8)(b_vy & mask);
        b_x += (b_vx >> k) + (i < rx);
        b_y += (b_vy >> k) + (i < ry);
        /* the shapes of its cell whose boxes hold the ball */
        if (b_x >= 0 && b_y >= 0) {
            cell = (u8)((b_y >> GRID_SHIFT) * GRID_W + (b_x >> GRID_SHIFT));
            if (cell < GRID_W * GRID_H) {
                e = scan(cell);
                for (j = 0; j < e; ++j) {
                    rx = cand[j];
                    if (rx >= NSEG)
                        hit_cir(rx - NSEG);
                    else
                        hit_seg(rx);
                }
            }
        }
        /* the flippers, at this substep's angle */
        /* (each flipper's reach: the pivot's end and the tip's sweep,
           up to 26 degrees up and 30 down, and the radii) */
        if (b_y > FLIP_Y - 32 * UNIT && b_y < FLIP_Y + 34 * UNIT) {
            if (b_x < FLIP_LX + 53 * UNIT && b_x > FLIP_LX - 13 * UNIT)
                hit_flip(0, a0[0] + (s16)((f_da[0] * (s16)(i + 1)) >> k));
            if (b_x > FLIP_RX - 53 * UNIT && b_x < FLIP_RX + 13 * UNIT)
                hit_flip(1, a0[1] + (s16)((f_da[1] * (s16)(i + 1)) >> k));
        }
        /* the plunger */
        if (b_x > LANE_X0 && b_y + BALL_R > p_top && b_y < p_top) {
            b_y = p_top - BALL_R;
            if (b_vy > 0)
                b_vy = b_vy > REST_V * 2 ? -(s16)(mul16(b_vy, E_TARGET) >> 8) : 0;
        }
        PROBE();
    }
    sensors(x0, y0);
    if (b_y > DRAIN_Y) {
        b_on = 0;
        event(EV_DRAIN, 0);
    }
}
