/*
 * Lighthouse Pinball: the fixed-point helpers in C (the host; the X16
 * uses veranda/fix.s, which gives the same results bit for bit: the gate
 * compares the two machines' traces).
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include "pb.h"

s32 mul16(s16 a, s16 b)
{
    return (s32)a * (s32)b;
}

s16 mul14(s16 a, s16 b)
{
    return (s16)(((s32)a * (s32)b) >> 14);
}

s16 dot14(s16 a, s16 b, s16 c, s16 d)
{
    return (s16)(((s32)a * (s32)b + (s32)c * (s32)d) >> 14);
}

/* the integer square root (floor) */
u16 isqrt32(u32 v)
{
    u32 r = 0, b = (u32)1 << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return (u16)r;
}

/* (a << 14) / d, truncated toward 0 */
s16 divq14(s16 a, u16 d)
{
    u32 n;
    s16 q;
    if (a < 0) {
        n = ((u32)(u16)(-a)) << 14;
        q = (s16)(n / d);
        return -q;
    }
    n = ((u32)(u16)a) << 14;
    return (s16)(n / d);
}
