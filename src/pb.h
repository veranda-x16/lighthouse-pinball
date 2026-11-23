/*
 * Lighthouse Pinball: the physics and the rules, in C that the X16 (cc65)
 * and the host (cc) compile alike. Fixed point throughout: positions and
 * velocities in 1/64 pixel (per tick), unit vectors in Q14, angles in
 * 1/256 of a turn (Q8.8 for the flippers). One tick is one frame (60 Hz).
 *
 * Every product goes through mul16() (16 x 16 to 32 bits, signed), so the
 * two compilers' int widths never matter; shifts of negative numbers are
 * arithmetic on both.
 *
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#ifndef PB_H
#define PB_H
#include <stdint.h>
#include "table.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

/* fix.c (or fix.s on the X16) */
s32 mul16(s16 a, s16 b);
s16 mul14(s16 a, s16 b);            /* (a * b) >> 14 */
s16 dot14(s16 a, s16 b, s16 c, s16 d);  /* (a * b + c * d) >> 14 */
u16 isqrt32(u32 v);
s16 divq14(s16 a, u16 d);          /* (a << 14) / d, |a| <= d */

/*---------------------------------------------------------------- physics */
#define GRAVITY     9               /* units a tick, each tick */
#define VMAX        960             /* 15 pixels a tick, each axis */
#define FLIP_SPEED_UP   (12 * 256)  /* angle units a tick (Q8.8) */
#define FLIP_SPEED_DOWN (8 * 256)

extern s16 b_x, b_y, b_vx, b_vy;    /* the ball */
extern u8 b_on;                     /* the physics moves it */
extern s16 f_ang[2], f_da[2];       /* the flippers: angle (Q8.8), its change this tick */
extern u8 f_hold[2];                /* the flipper buttons, as the physics sees them */
extern s16 p_top;                   /* the plunger's top */
extern u8 drop_up[3];               /* the buoys standing */
extern u8 substeps;                 /* the last tick's (statistics) */

/* events of a tick */
enum {
    EV_BUMPER = 1, EV_SLING, EV_DROP, EV_STANDUP, EV_FLIPHIT, EV_LANE, EV_SPIN, EV_DOCK,
    EV_EXIT, EV_INLANE, EV_OUTLANE, EV_ORBIT, EV_DRAIN, EV_WALL
};
#define EV_MAX 16
extern u8 ev_n, ev_kind[EV_MAX], ev_id[EV_MAX];

extern u8 cand[32];
u8 scan(u8 cell);                   /* phys.c, or x16.s's */
void phys_init(void);
void phys_tick(void);
u8 phys_on_plunger(void);           /* the ball rests on the plunger */

/*---------------------------------------------------------------- the game */
/* the inputs: held keys, a tick's worth */
#define IN_LFLIP    0x01
#define IN_RFLIP    0x02
#define IN_PLUNGE   0x04
#define IN_NUDGEL   0x08
#define IN_NUDGER   0x10
#define IN_NUDGEU   0x20
#define IN_START    0x40

/* the keyboard: X16 key numbers to inputs (the hook in hook.s reads it) */
extern const u8 keymap[128];
extern u8 in_held, in_latch;
void key_event(u8 code);            /* bit 7: released (the C model of hook.s) */

enum { G_ATTRACT, G_PLAY, G_OVER };
enum { B_NONE, B_LANE, B_PLAY, B_RIDE, B_DRAINED };

/* messages for the panel */
enum {
    M_NONE, M_PRESS, M_PLUNGE, M_BEAM, M_BUOYS, M_FERRY, M_COMBO, M_JACKPOT_LIT, M_JACKPOT,
    M_EXTRA, M_SAVED, M_AGAIN, M_DANGER, M_TILT, M_BONUS, M_BALL, M_OVER, M_LOOP, M_LANE
};

/* sounds (veranda/sound.s plays them) */
enum {
    SND_FLIP = 1, SND_BUMPER, SND_SLING, SND_LAUNCH, SND_DRAIN, SND_LANE, SND_TARGET, SND_FERRY,
    SND_JACKPOT, SND_EXTRA, SND_TILT, SND_SPIN, SND_MISSION, SND_START, SND_OVER, SND_DANGER
};

struct game {
    u8 state, ball_state;
    u8 ball, extra, extra_given;
    u32 score;
    u16 tick;                       /* ticks of this game */
    u8 lanes, drops, ferries, missions, jackpot_lit, level, mult;
    u8 bonus;
    u8 tilt_meter, tilted;
    u16 save_timer, combo_timer;
    u8 drop_reset, drain_timer;
    u8 charge;
    u8 msg, msg_arg;
    u16 msg_timer;
    s16 rail_pos;
    u8 rail_seg;
    u8 flash[3];                    /* the fog banks' flashes */
    u8 prev;                        /* the inputs a tick before */
    u8 spin_timer;
    u16 last_bonus;
    u8 still;                       /* ticks the ball has hardly moved */
};
extern struct game G;

#define SNDQ 8
extern u8 snd_n, snd_q[SNDQ];

void game_init(void);
void game_tick(u8 in);
/* the lamps: 0 off, 1 lit (blinking ones already resolved for this tick) */
void game_lamps(u8 *lit);

/* the state's checksum (the gates compare the X16's with the host's) */
extern u16 trace_hash;
void trace_tick(void);

#endif
