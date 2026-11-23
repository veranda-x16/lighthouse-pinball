/*
 * Lighthouse Pinball: the lines both machines log (the X16's test build
 * through LogText, host/sim.c on stdout), formatted by the same code so
 * the gate can compare them as text:
 *   PB EV <tick> <event> <id>     a launch, a flipper's hit, a drain ...
 *   PB T <tick> <hash>            every 256 ticks: the state's checksum
 *   PB OVER <tick> <score>        the game is over
 * (hexadecimal; the tick counts from the script's start)
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include "pb.h"
#include "trace.h"

char trace_buf[40];
static u8 tn;

static void put(const char *s)
{
    while (*s)
        trace_buf[tn++] = *s++;
}

static void hex(u32 v, u8 digits)
{
    static const char hx[] = "0123456789ABCDEF";
    trace_buf[tn++] = ' ';
    while (digits--)
        trace_buf[tn++] = hx[(u8)(v >> (digits * 4)) & 15];
}

static const char *const names[] = {
    0, 0, 0, "DROP", "LANTERN", "FLIPHIT", 0, 0, "FERRY", "LAUNCH", 0, "OUTLANE", "LOOP", "DRAIN", 0
};

u8 trace_event(u16 tick, u8 i)
{
    const char *n = names[ev_kind[i]];
    if (!n)
        return 0;
    tn = 0;
    put("PB EV");
    hex(tick, 4);
    trace_buf[tn++] = ' ';
    put(n);
    hex(ev_id[i], 1);
    trace_buf[tn] = 0;
    return 1;
}

void trace_line(const char *what, u16 tick, u32 v, u8 digits)
{
    tn = 0;
    put(what);
    hex(tick, 4);
    hex(v, digits);
    trace_buf[tn] = 0;
}
