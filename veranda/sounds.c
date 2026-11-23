/*
 * Lighthouse Pinball: the sounds, as note lists for the PSG player in
 * x16.s (7 bytes a note: the frequency word, the waveform, the volume,
 * its decay a frame, the length in frames, a slide a frame (x 4); a
 * length of 0 ends). VERA's volume is logarithmic (Veranda's DECISIONS
 * V72): the notes start loud (52-63) and do not fade below about 30
 * before they end.
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include "pbx.h"

/* the frequency word of hz: 2^17 / 48828.125 a hertz */
#define F(hz) (u8)(((hz) * 2684UL / 1000) & 255), (u8)(((hz) * 2684UL / 1000) >> 8)
#define PULSE 0x3F          /* a square */
#define SAW   0x7F
#define TRI   0xBF
#define NOISE 0xFF
#define N(hz, w, vol, dec, len, slide) F(hz), w, vol, dec, len, (u8)(slide)
#define END 0, 0, 0, 0, 0, 0, 0

static const u8 s_flip[] = { N(1800, NOISE, 54, 8, 3, 0), END };
static const u8 s_bumper[] = { N(160, PULSE, 63, 3, 6, -6), N(90, PULSE, 50, 4, 4, -2), END };
static const u8 s_bump2[] = { N(2400, NOISE, 58, 9, 4, 0), END };
static const u8 s_sling[] = { N(330, PULSE, 60, 6, 4, -8), END };
static const u8 s_launch[] = { N(120, SAW, 60, 0, 18, 12), N(520, SAW, 56, 4, 6, 0), END };
static const u8 s_drain[] = { N(440, TRI, 62, 0, 20, -10), N(200, TRI, 60, 1, 24, -4), N(90, TRI, 56, 2, 10, 0), END };
static const u8 s_lane[] = { N(880, TRI, 60, 2, 5, 0), N(1320, TRI, 60, 3, 7, 0), END };
static const u8 s_target[] = { N(660, PULSE, 60, 7, 4, 0), END };
/* the ferry's horn: two long low notes, a fifth apart */
static const u8 s_ferry[] = { N(110, SAW, 63, 0, 26, 0), N(82, SAW, 63, 1, 34, 0), END };
static const u8 s_jackpot[] = {
    N(523, PULSE, 62, 1, 6, 0), N(659, PULSE, 62, 1, 6, 0), N(784, PULSE, 62, 1, 6, 0), N(1047, PULSE, 62, 1, 6, 0),
    N(784, PULSE, 62, 1, 6, 0), N(1047, PULSE, 62, 1, 6, 0), N(1319, PULSE, 63, 1, 20, 0), END };
static const u8 s_extra[] = { N(784, TRI, 62, 0, 6, 0), N(988, TRI, 62, 0, 6, 0), N(1175, TRI, 62, 0, 6, 0),
    N(1568, TRI, 63, 1, 16, 0), END };
static const u8 s_tilt[] = { N(70, PULSE, 63, 0, 40, 0), N(70, NOISE, 50, 1, 10, 0), END };
static const u8 s_spin[] = { N(2200, PULSE, 52, 12, 2, 0), END };
static const u8 s_mission[] = { N(659, TRI, 62, 0, 8, 0), N(880, TRI, 63, 1, 16, 0), END };
static const u8 s_start[] = { N(392, PULSE, 60, 0, 6, 0), N(523, PULSE, 60, 0, 6, 0), N(659, PULSE, 60, 0, 6, 0),
    N(784, PULSE, 62, 1, 14, 0), END };
static const u8 s_over[] = { N(392, TRI, 62, 0, 12, 0), N(330, TRI, 62, 0, 12, 0), N(262, TRI, 62, 0, 12, 0),
    N(196, TRI, 63, 1, 30, -1), END };
static const u8 s_danger[] = { N(300, PULSE, 60, 0, 5, 0), N(0, PULSE, 0, 0, 3, 0), N(300, PULSE, 60, 0, 5, 0), END };

/* the voice each sound takes; a second voice for a layer */
void sound_play(u8 id)
{
    switch (id) {
    case SND_FLIP: snd_start(0, s_flip); break;
    case SND_BUMPER: snd_start(1, s_bumper); snd_start(4, s_bump2); break;
    case SND_SLING: snd_start(1, s_sling); break;
    case SND_TARGET: snd_start(1, s_target); break;
    case SND_SPIN: snd_start(1, s_spin); break;
    case SND_LANE: snd_start(2, s_lane); break;
    case SND_LAUNCH: snd_start(3, s_launch); break;
    case SND_DRAIN: snd_start(3, s_drain); break;
    case SND_FERRY: snd_start(3, s_ferry); break;
    case SND_JACKPOT: snd_start(2, s_jackpot); break;
    case SND_EXTRA: snd_start(4, s_extra); break;
    case SND_TILT: snd_start(3, s_tilt); break;
    case SND_MISSION: snd_start(2, s_mission); break;
    case SND_START: snd_start(2, s_start); break;
    case SND_OVER: snd_start(2, s_over); break;
    case SND_DANGER: snd_start(4, s_danger); break;
    }
}
