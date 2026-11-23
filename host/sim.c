/*
 * Lighthouse Pinball on the host: the same physics and rules (src/ C files) as
 * the X16 runs.
 *
 *   sim run <script>            play a script; the log lines (src/trace.c)
 *                               on stdout, as the X16's test build logs them
 *   sim bot <script> [seed]     an autopilot plays a game; its key events
 *                               written as a script (then "run" plays it)
 *   sim path <script> <csv>     the ball's position each tick (for tuning)
 *   sim bin <script> <bin>      the script for the X16 (3 bytes an event:
 *                               the tick, the key number; $FFFF and the
 *                               end tick)
 *
 * A script: lines "<tick> <key number>" (+128: released), "end <tick>".
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/pb.h"
#include "../src/trace.h"

#define MAXEV 20000
static u16 ev_t[MAXEV];
static u8 ev_c[MAXEV];
static int nev, evpos;
static u16 end_tick;
static u16 tick;
static FILE *pathf;
static int counts[16];

static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[128];
    int t, c;
    if (!f) {
        perror(path);
        exit(1);
    }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#')
            continue;
        if (sscanf(line, "end %d", &t) == 1)
            end_tick = (u16)t;
        else if (sscanf(line, "%d %d", &t, &c) == 2) {
            ev_t[nev] = (u16)t;
            ev_c[nev++] = (u8)c;
        }
    }
    fclose(f);
}

/* one tick: the script's keys, the game, the log (veranda/main.c does the
   same, in the same order) */
static void step(int quiet)
{
    u8 in, was = G.state, i;
    while (evpos < nev && ev_t[evpos] == tick)
        key_event(ev_c[evpos++]);
    in = in_held | in_latch;
    in_latch = 0;
    game_tick(in);
    for (i = 0; i < ev_n; ++i) {
        ++counts[ev_kind[i]];
        if (!quiet && trace_event(tick, i))
            puts(trace_buf);
    }
    if (G.state == G_OVER && was != G_OVER && !quiet) {
        trace_line("PB OVER", tick, G.score, 8);
        puts(trace_buf);
    }
    if ((tick & 255) == 255 && !quiet) {
        trace_line("PB T", tick, trace_hash, 4);
        puts(trace_buf);
    }
    if (pathf)
        fprintf(pathf, "%u %d %d %d %d %d\n", tick, b_x, b_y, b_on, G.ball_state, ev_n);
    snd_n = 0;
    ++tick;
}

static void run(void)
{
    game_init();
    while (tick < end_tick)
        step(0);
    trace_line("PB END", tick, trace_hash, 4);
    puts(trace_buf);
    trace_line("PB SCORE", tick, G.score, 8);
    puts(trace_buf);
}

/*---------------------------------------------------------------- the autopilot */
static FILE *out;
static u32 seed = 1;

static u32 rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
}

static void key(u8 code)
{
    fprintf(out, "%u %u\n", tick, code);
    ev_t[nev] = tick;
    ev_c[nev++] = code;
}

static void bot(void)
{
    int hold[2] = { 0, 0 }, charge = -1, nudged = 0, over = -1, skill = 0;
    u16 ball_ticks = 0;
    u8 lastball = 0;
    game_init();
    end_tick = 60000;
    for (;;) {
        /* decide this tick's keys, then play it */
        if (tick == 30)
            key(43);
        if (tick == 33)
            key(43 | 128);
        if (G.state == G_PLAY) {
            if (G.ball != lastball || G.ball_state == B_LANE)
                ball_ticks = 0;
            lastball = G.ball;
            ++ball_ticks;
            /* the plunger: hold for 40-58 ticks */
            if (G.ball_state == B_LANE && charge < 0 && phys_on_plunger() && b_vy == 0) {
                key(61);
                charge = 40 + (int)(rnd() % 19);
            } else if (charge > 0 && --charge == 0) {
                key(61 | 128);
                charge = -1;
            }
            /* the flippers: the ball coming down near one; the skill
               falls as the ball's time goes on, so the game ends */
            skill = ball_ticks < 1500 ? 92 : ball_ticks < 3000 ? 70 : 20;
            if (b_on && b_vy > 0 && b_y > 392 * UNIT && b_y < 440 * UNIT) {
                if (!hold[0] && b_x < 300 * UNIT && b_x > 236 * UNIT && (int)(rnd() % 100) < skill) {
                    key(46);
                    hold[0] = 10 + (int)(rnd() % 14);
                }
                if (!hold[1] && b_x > 308 * UNIT && b_x < 372 * UNIT && (int)(rnd() % 100) < skill) {
                    key(55);
                    hold[1] = 10 + (int)(rnd() % 14);
                }
            }
            if (hold[0] && --hold[0] == 0)
                key(46 | 128);
            if (hold[1] && --hold[1] == 0)
                key(55 | 128);
            /* a nudge now and then (never enough to tilt) */
            if (G.ball_state == B_PLAY && b_on && b_y < 300 * UNIT && nudged < 4 && (tick % 997) == 0) {
                key(nudged & 1 ? 79 : 89);
                ++nudged;
            }
            if ((tick % 997) == 2 && (in_held & (IN_NUDGEL | IN_NUDGER)))
                key(in_held & IN_NUDGEL ? 79 | 128 : 89 | 128);
        }
        if (G.state == G_OVER && over < 0)
            over = tick;
        if (over >= 0 && tick >= over + 120)
            break;
        step(1);
        if (tick > 50000) {
            fprintf(stderr, "bot: no game over by tick %u\n", tick);
            exit(1);
        }
    }
    fprintf(out, "end %u\n", tick);
    fprintf(stderr, "bot: tick %u score %lu; launches %d, flipper hits %d, drains %d, bumpers %d, slings %d, "
            "lanes %d, drops %d, ferries %d, lantern %d, spins %d, loops %d, outlanes %d, missions %d, level %d\n",
            tick, (unsigned long)G.score, counts[EV_EXIT], counts[EV_FLIPHIT], counts[EV_DRAIN], counts[EV_BUMPER],
            counts[EV_SLING], counts[EV_LANE], counts[EV_DROP], counts[EV_DOCK], counts[EV_STANDUP],
            counts[EV_SPIN], counts[EV_ORBIT], counts[EV_OUTLANE], G.missions, G.level);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "run")) {
        load(argv[2]);
        run();
    } else if (argc >= 3 && !strcmp(argv[1], "bot")) {
        out = fopen(argv[2], "w");
        if (argc > 3)
            seed = (u32)atol(argv[3]);
        fprintf(out, "# Lighthouse Pinball: a game by host/sim's autopilot (seed %lu)\n", (unsigned long)seed);
        bot();
        fclose(out);
    } else if (argc >= 4 && !strcmp(argv[1], "path")) {
        load(argv[2]);
        pathf = fopen(argv[3], "w");
        game_init();
        while (tick < end_tick)
            step(1);
        fclose(pathf);
    } else if (argc >= 4 && !strcmp(argv[1], "bin")) {
        FILE *f;
        int i;
        load(argv[2]);
        f = fopen(argv[3], "wb");
        for (i = 0; i < nev; ++i) {
            fputc(ev_t[i] & 255, f);
            fputc(ev_t[i] >> 8, f);
            fputc(ev_c[i], f);
        }
        fputc(255, f);
        fputc(255, f);
        fputc(end_tick & 255, f);
        fputc(end_tick >> 8, f);
        fclose(f);
    } else {
        fprintf(stderr, "usage: sim run|bot|path|bin ...\n");
        return 2;
    }
    return 0;
}
