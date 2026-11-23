/*
 * Lighthouse Pinball: the rules. A tick at a time, from the inputs held;
 * the physics' events become points, lamps, missions, messages and
 * sounds. Nothing here depends on time or chance: the same inputs give
 * the same game (the gates compare the X16's with the host's).
 *
 * The harbour at night: light the BEAM lanes (B E A M), knock down the
 * three BUOYS, ride the FERRY ramp three times (again within four seconds:
 * a combo, worth two). The three missions done light the lighthouse: its
 * lantern target is the JACKPOT, the first one a game also an extra ball.
 * Lanes done raise the bonus multiplier (to 5X). Nudge with the arrow
 * keys; too much and it tilts.
 *
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include "pb.h"

struct game G;
u8 snd_n, snd_q[SNDQ];
u8 in_held, in_latch;
u16 trace_hash;

/* X16 key numbers (the KERNAL's, IBM's): 43 Return, 44 left Shift, 46 Z,
   55 /, 57 right Shift, 61 Space, 79 left, 83 up, 84 down, 89 right */
const u8 keymap[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IN_START, IN_LFLIP, 0, IN_LFLIP, 0,
    0, 0, 0, 0, 0, 0, 0, IN_RFLIP, 0, IN_RFLIP, 0, 0, 0, IN_PLUNGE, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IN_NUDGEL,
    0, 0, 0, IN_NUDGEU, IN_PLUNGE, 0, 0, 0, 0, IN_NUDGER, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void key_event(u8 code)
{
    u8 b = keymap[code & 127];
    if (!b)
        return;
    if (code & 128)
        in_held &= (u8)~b;
    else {
        in_held |= b;
        in_latch |= b;
    }
}

#define LAUNCH_MIN  300
#define LAUNCH_K    10
#define CHARGE_MAX  60
#define SAVE_TICKS  600
#define COMBO_TICKS 240

static void snd(u8 s)
{
    if (snd_n < SNDQ)
        snd_q[snd_n++] = s;
}

static void add(u32 v)
{
    if (!G.tilted)
        G.score += v;
}

static void msg(u8 m, u8 arg)
{
    G.msg = m;
    G.msg_arg = arg;
    G.msg_timer = 150;
}

static void new_ball(void)
{
    b_x = PLUNGER_X;
    b_y = PLUNGER_REST - BALL_R;
    b_vx = b_vy = 0;
    b_on = 1;
    p_top = PLUNGER_REST;
    G.ball_state = B_LANE;
    G.charge = 0;
    G.tilt_meter = 0;
    G.tilted = 0;
    G.save_timer = 0;
    G.combo_timer = 0;
    G.bonus = 0;
    G.drops = 0;
    drop_up[0] = drop_up[1] = drop_up[2] = 1;
}

void game_init(void)
{
    u8 i;
    u8 *p = (u8 *)&G;
    for (i = 0; i < sizeof G; ++i)
        p[i] = 0;
    phys_init();
    G.state = G_ATTRACT;
    G.mult = 1;
    msg(M_PRESS, 0);
    G.msg_timer = 0;
    trace_hash = 0;
}

static void game_new(void)
{
    game_init();
    G.state = G_PLAY;
    G.ball = 1;
    G.prev = IN_START;
    snd(SND_START);
    new_ball();
    msg(M_BALL, 1);
}

static void mission(u8 m)
{
    if (G.missions & (1 << m))
        return;
    G.missions |= (u8)(1 << m);
    add(25000);
    snd(SND_MISSION);
    msg(M_BEAM + m, 0);
    if (G.missions == 7 && !G.jackpot_lit) {
        G.jackpot_lit = 1;
        msg(M_JACKPOT_LIT, 0);
    }
}

static void next_ball(void)
{
    if (!G.tilted) {
        G.last_bonus = G.bonus;
        add((u32)G.bonus * 1000 * G.mult);
    }
    if (G.extra) {
        --G.extra;
        new_ball();
        msg(M_AGAIN, 0);
        return;
    }
    if (++G.ball > 3) {
        G.state = G_OVER;
        G.ball_state = B_NONE;
        G.ball = 3;
        b_on = 0;
        snd(SND_OVER);
        msg(M_OVER, 0);
        G.msg_timer = 0;
        return;
    }
    new_ball();
    msg(M_BALL, G.ball);
}

static void events(void)
{
    u8 i, k, id, b;
    for (i = 0; i < ev_n; ++i) {
        k = ev_kind[i];
        id = ev_id[i];
        switch (k) {
        case EV_BUMPER:
            add(500 + 100 * (u16)G.level);
            G.flash[id] = 8;
            snd(SND_BUMPER);
            break;
        case EV_SLING:
            add(50);
            snd(SND_SLING);
            break;
        case EV_DROP:
            add(2000);
            G.drops |= (u8)(1 << id);
            ++G.bonus;
            snd(SND_TARGET);
            if (G.drops == 7) {
                add(10000);
                mission(1);
                G.drop_reset = 90;
            }
            break;
        case EV_STANDUP:
            if (G.jackpot_lit && !G.tilted) {
                add(50000UL * (u32)(G.level + 1));
                G.jackpot_lit = 0;
                G.missions = 0;
                G.ferries = 0;
                G.lanes = 0;
                ++G.level;
                snd(SND_JACKPOT);
                msg(M_JACKPOT, G.level);
                if (!G.extra_given) {
                    G.extra_given = 1;
                    ++G.extra;
                    snd(SND_EXTRA);
                }
            } else {
                add(1000);
                snd(SND_TARGET);
            }
            break;
        case EV_LANE:
            b = (u8)(1 << id);
            if (!(G.lanes & b)) {
                G.lanes |= b;
                add(1000);
                ++G.bonus;
                snd(SND_LANE);
                if (G.lanes == 15) {
                    G.lanes = 0;
                    if (G.mult < 5)
                        ++G.mult;
                    add(10000);
                    mission(0);
                    if (!G.jackpot_lit)
                        msg(M_BEAM, G.mult);
                }
            } else
                add(250);
            break;
        case EV_SPIN:
            add(100 * (u16)id);
            G.spin_timer = 30;
            snd(SND_SPIN);
            break;
        case EV_DOCK:
            if (G.ball_state != B_PLAY)
                break;
            add(5000);
            G.bonus += 2;
            ++G.ferries;
            if (G.combo_timer) {
                add(15000);
                ++G.ferries;
                msg(M_COMBO, 0);
            } else
                msg(M_FERRY, G.ferries > 3 ? 3 : G.ferries);
            snd(SND_FERRY);
            if (G.ferries >= 3)
                mission(2);
            b_on = 0;
            G.ball_state = B_RIDE;
            G.rail_seg = 0;
            G.rail_pos = 0;
            G.combo_timer = 0;
            break;
        case EV_EXIT:
            if (G.ball_state == B_LANE) {
                G.ball_state = B_PLAY;
                G.save_timer = SAVE_TICKS;
            }
            break;
        case EV_INLANE:
            add(500);
            break;
        case EV_OUTLANE:
            add(2000);
            break;
        case EV_ORBIT:
            add(3000);
            snd(SND_LANE);
            msg(M_LOOP, 0);
            break;
        case EV_DRAIN:
            if (G.save_timer && !G.tilted && G.ball_state == B_PLAY) {
                new_ball();
                msg(M_SAVED, 0);
                snd(SND_EXTRA);
            } else {
                G.ball_state = B_DRAINED;
                G.drain_timer = 90;
                snd(SND_DRAIN);
                if (!G.tilted)
                    msg(M_BONUS, G.mult);
            }
            break;
        }
    }
}

static void ride(void)
{
    s16 len;
    u8 s;
    G.rail_pos += RIDE_SPEED;
    for (;;) {
        s = G.rail_seg;
        len = rail_len[s];
        if (G.rail_pos < len || s >= NRAIL - 1)
            break;
        G.rail_pos -= len;
        ++G.rail_seg;
    }
    s = G.rail_seg;
    if (s >= NRAIL - 1) {
        b_x = rail_x[NRAIL - 1];
        b_y = rail_y[NRAIL - 1];
        b_vx = RIDE_VX;
        b_vy = RIDE_VY;
        b_on = 1;
        G.ball_state = B_PLAY;
        G.combo_timer = COMBO_TICKS;
        return;
    }
    b_x = rail_x[s] + mul14(G.rail_pos, rail_ux[s]);
    b_y = rail_y[s] + mul14(G.rail_pos, rail_uy[s]);
}

static void nudge(s16 vx, s16 vy)
{
    if (G.ball_state != B_PLAY || G.tilted || !b_on)
        return;
    b_vx += vx;
    b_vy += vy;
    G.tilt_meter += 60;
    if (G.tilt_meter >= 170) {
        G.tilted = 1;
        snd(SND_TILT);
        msg(M_TILT, 0);
    } else if (G.tilt_meter >= 110) {
        snd(SND_DANGER);
        msg(M_DANGER, 0);
    }
}

void game_tick(u8 in)
{
    u8 press = in & (u8)~G.prev, i;
    ++G.tick;
    if ((press & IN_START) && G.state != G_PLAY)
        game_new();

    /* the flippers, lane change */
    f_hold[0] = (in & IN_LFLIP) && !G.tilted;
    f_hold[1] = (in & IN_RFLIP) && !G.tilted;
    if (!G.tilted) {
        if (press & (IN_LFLIP | IN_RFLIP))
            snd(SND_FLIP);
        if (G.state == G_PLAY) {
            if (press & IN_LFLIP)
                G.lanes = (u8)(((G.lanes << 1) | (G.lanes >> 3)) & 15);
            if (press & IN_RFLIP)
                G.lanes = (u8)(((G.lanes >> 1) | (G.lanes << 3)) & 15);
        }
    }

    /* the plunger */
    if (G.ball_state == B_LANE) {
        if (in & IN_PLUNGE) {
            if (G.charge < CHARGE_MAX)
                ++G.charge;
            p_top = PLUNGER_REST + (s16)G.charge * 17;
        } else if (G.prev & IN_PLUNGE) {
            if (phys_on_plunger()) {
                b_vy = -(LAUNCH_MIN + (s16)G.charge * LAUNCH_K);
                snd(SND_LAUNCH);
            }
            p_top = PLUNGER_REST;
            G.charge = 0;
        }
    } else
        p_top = PLUNGER_REST;

    /* nudges */
    if (press & IN_NUDGEL)
        nudge(-150, -60);
    if (press & IN_NUDGER)
        nudge(150, -60);
    if (press & IN_NUDGEU)
        nudge(0, -200);
    if (G.tilt_meter)
        --G.tilt_meter;
    G.prev = in;

    phys_tick();
    events();

    switch (G.ball_state) {
    case B_PLAY:
        if (G.save_timer)
            --G.save_timer;
        /* a ball search: one resting where no flipper holds it, for three
           seconds, gets a kick */
        if (b_on && !f_hold[0] && !f_hold[1] && b_vx < 24 && b_vx > -24 && b_vy < 24 && b_vy > -24) {
            if (++G.still >= 180) {
                G.still = 0;
                b_vy = -360;
                b_vx = b_x < 304 * UNIT ? 120 : -120;
            }
        } else
            G.still = 0;
        break;
    case B_RIDE:
        ride();
        break;
    case B_DRAINED:
        if (!--G.drain_timer)
            next_ball();
        break;
    }
    if (G.combo_timer && G.ball_state == B_PLAY)
        --G.combo_timer;
    if (G.drop_reset && !--G.drop_reset) {
        G.drops = 0;
        drop_up[0] = drop_up[1] = drop_up[2] = 1;
    }
    for (i = 0; i < 3; ++i)
        if (G.flash[i])
            --G.flash[i];
    if (G.spin_timer)
        --G.spin_timer;
    if (G.msg_timer && !--G.msg_timer)
        G.msg = G.state == G_PLAY ? M_NONE : M_PRESS;
    trace_tick();
}

void trace_tick(void)
{
    u16 h = trace_hash;
    h = (u16)((h << 5) + h) ^ (u16)b_x;
    h = (u16)((h << 5) + h) ^ (u16)b_y;
    h = (u16)((h << 5) + h) ^ (u16)b_vx;
    h = (u16)((h << 5) + h) ^ (u16)b_vy;
    h = (u16)((h << 5) + h) ^ (u16)f_ang[0] ^ (u16)(f_ang[1] << 3);
    h = (u16)((h << 5) + h) ^ (u16)G.score ^ (u16)(G.score >> 16);
    trace_hash = h;
}

void game_lamps(u8 *lit)
{
    u8 i, blink = (u8)((G.tick >> 3) & 1);
    for (i = 0; i < NLAMP; ++i)
        lit[i] = 0;
    if (G.state != G_PLAY) {
        /* the attract show: a light running round */
        for (i = 0; i < NLAMP; ++i)
            lit[i] = (u8)((i + (G.tick >> 3)) % 5 == 0);
        lit[L_TILT] = 0;
        return;
    }
    if (G.tilted) {
        lit[L_TILT] = 1;
        return;
    }
    lit[L_TILT] = G.tilt_meter >= 110 && blink;
    for (i = 0; i < 4; ++i)
        lit[L_LANE0 + i] = (u8)((G.lanes >> i) & 1);
    for (i = 0; i < 3; ++i) {
        lit[L_BUOY0 + i] = (u8)((G.drops >> i) & 1);
        lit[L_FERRY0 + i] = (u8)(G.ferries > i || (G.missions & 4));
        lit[L_BUMP0 + i] = G.flash[i] != 0;
    }
    lit[L_MBEAM] = lit[L_PBEAM] = G.missions & 1;
    lit[L_MBUOY] = lit[L_PBUOY] = (G.missions >> 1) & 1;
    lit[L_MFERRY] = lit[L_PFERRY] = (G.missions >> 2) & 1;
    for (i = 0; i < 4; ++i)
        lit[L_MULT0 + i] = G.mult >= i + 2;
    lit[L_LANTERN] = lit[L_JACKPOT] = G.jackpot_lit && blink;
    lit[L_EXTRA] = G.extra != 0;
    lit[L_AGAIN] = G.extra != 0 || (G.save_timer && (G.save_timer > 120 || blink));
    lit[L_COMBO] = G.combo_timer && blink;
    lit[L_SPIN] = G.spin_timer != 0;
}
