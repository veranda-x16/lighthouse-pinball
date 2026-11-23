/*
 * Lighthouse Pinball for Veranda: the program. The table is a full-colour
 * picture in VERA's 4 bpp tiles under the bitmap (Veranda's vr_pic, its
 * DECISIONS V105: records 1 .. PIC_RECORDS, tools/mkart.py), loaded once;
 * the ball, the flippers and the plunger are VERA sprites (the next two
 * records), the lamps are their cards' map entries swapped (lit or off:
 * pic.h), the score a dot display blitted into the bitmap, which holds
 * nothing but the panel's text. A tick of the physics and the rules (src/) a
 * frame, from the frame counter of the interrupt (x16.s, which also plays
 * the sounds); the keys come from the ROM's key hook (held and released).
 * Menus: Lighthouse (about), Game (new game, high scores, quit), Options
 * (sound). The high scores in /VERANDA/.SYSTEM (vrpath.h).
 *
 * While it runs the game has its own palette (the night table: a light
 * ink on dark papers; the sprites' colours in entries 32-63); the one it
 * found comes back when it quits.
 *
 * Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).
 */
#include <string.h>
#include <theme.h>
#include <vrpath.h>
#include "pbx.h"
#include "../src/trace.h"
#include "pic.h"

/*---------------------------------------------------------------- the test build */
#ifdef PB_TEST
static u8 sbank;
static u16 spos;
static u16 send;
static u32 t_max, t_sum, p_max;
static u16 t_n;
static u8 test_done, skip_timing;
#define CLOCK_RESET() (*(volatile u8 *)0x9FB8 = 0)
static u32 clock_read(void)
{
    u32 c;
    c = *(volatile u8 *)0x9FB8;
    c |= (u16)(*(volatile u8 *)0x9FB9) << 8;
    c |= (u32)(*(volatile u8 *)0x9FBA) << 16;
    c |= (u32)(*(volatile u8 *)0x9FBB) << 24;
    return c;
}
#endif

static u16 tick;                    /* ticks since the start */
static u8 done;                     /* frames handled */
static u16 late;                    /* frames that came while one was being made */
static u8 paused, over_pending;
static u16 saved_blank;
static u8 pal_saved[128];           /* palette entries 0-63 as found */

/*---------------------------------------------------------------- VRAM */
static void vat(u32 a)
{
    VERA_CTRL = 0;
    VERA_ADDR_L = (u8)a;
    VERA_ADDR_M = (u8)(a >> 8);
    VERA_ADDR_H = (u8)((a >> 16) & 1) | 0x10;
}

static u8 rec_bank(u8 n)
{
    return LoadOverlay(n, 0) == 0 ? r1L : 0;
}

static void palette_save(void)
{
    u8 i;
    vat(0x1FA00UL);
    for (i = 0; i < 128; ++i)
        pal_saved[i] = VERA_DATA0;
}

static void palette_restore(void)
{
    u8 i;
    vat(0x1FA00UL);
    for (i = 0; i < 128; ++i)
        VERA_DATA0 = pal_saved[i];
}

static void palette_game(void)
{
    u8 i;
    vat(0x1FA00UL);
    for (i = 0; i < 16; ++i) {
        VERA_DATA0 = (u8)game_palette[i];
        VERA_DATA0 = (u8)(game_palette[i] >> 8);
    }
    vat(0x1FA40UL);
    for (i = 0; i < 32; ++i) {
        VERA_DATA0 = (u8)sprite_palette[i];
        VERA_DATA0 = (u8)(sprite_palette[i] >> 8);
    }
}

/*---------------------------------------------------------------- sprites */
#define SP_BALL 1
#define SP_LFLIP 2
#define SP_RFLIP 3
#define SP_PLUNGER 4
static u8 shown_frame[2] = { 255, 255 };
static s16 shown_p = -1;

static void sprite(u8 n, u32 addr, s16 x, s16 y, u8 b6, u8 b7)
{
    vat(0x1FC00UL + 8 * n);
    VERA_DATA0 = (u8)(addr >> 5);
    VERA_DATA0 = (u8)(addr >> 13);
    VERA_DATA0 = (u8)x;
    VERA_DATA0 = (u8)(x >> 8) & 3;
    VERA_DATA0 = (u8)y;
    VERA_DATA0 = (u8)(y >> 8) & 3;
    VERA_DATA0 = b6;
    VERA_DATA0 = b7;
}

static void sprite_xy(u8 n, s16 x, s16 y)
{
    vat(0x1FC02UL + 8 * n);
    VERA_DATA0 = (u8)x;
    VERA_DATA0 = (u8)(x >> 8) & 3;
    VERA_DATA0 = (u8)y;
    VERA_DATA0 = (u8)(y >> 8) & 3;
}

static void sprite_z(u8 n, u8 b6)
{
    vat(0x1FC06UL + 8 * n);
    VERA_DATA0 = b6;
}

#define Z_FRONT 0x0C
#define FLIP_ATTR 0xB3              /* 64 x 32, palette offset 3 */

static void flipper(u8 f, u8 k)
{
    s16 x = f ? (FLIP_RX >> 6) - 56 : (FLIP_LX >> 6) - 8;
    sprite(SP_LFLIP + f, SPR_FLIP + 1024UL * k, x, (FLIP_Y >> 6) + flip_oy[k],
           (u8)(Z_FRONT | f), FLIP_ATTR);
}

static void sprites_show(u8 on)
{
    u8 z = on ? Z_FRONT : 0;
    sprite_z(SP_LFLIP, z);
    sprite_z(SP_RFLIP, (u8)(z | 1));
    sprite_z(SP_PLUNGER, z);
    sprite_z(SP_BALL, on && (b_on || G.ball_state == B_RIDE) ? Z_FRONT : 0);
}

static void sprites_draw(void)
{
    u8 f, k;
    s16 a;
    if (b_on || G.ball_state == B_RIDE) {
        sprite_xy(SP_BALL, (b_x >> 6) - 8, (b_y >> 6) - 8);
        sprite_z(SP_BALL, Z_FRONT);
    } else
        sprite_z(SP_BALL, 0);
    for (f = 0; f < 2; ++f) {
        a = FLIP_REST * 256 - f_ang[f] + 640;
        k = (u8)(a / 1280);
        if (k >= FLIP_FRAMES)
            k = FLIP_FRAMES - 1;
        if (k != shown_frame[f]) {
            shown_frame[f] = k;
            flipper(f, k);
        }
    }
    if (p_top != shown_p) {
        shown_p = p_top;
        sprite_xy(SP_PLUNGER, (PLUNGER_X >> 6) - 8, p_top >> 6);
    }
}

/*---------------------------------------------------------------- the table */
static void draw_art(void);

/* the picture's tiles, map and palette rows (vr_pic_on first) */
static void draw_table(void)
{
    u8 i;
    draw_art();
    for (i = 0; i < PIC_RECORDS; ++i)
        vr_vram_load(rec_bank(REC_PIC + i));
}

/* the bitmap under the menu bar cleared: the panel's text is all it holds */
static void draw_art(void)
{
    vat(16 * 80UL);
    vram_fill(0, (480 - 16) * 80);
}

static void load_sprites(void)
{
    vat(SPR_BALL);
    vram_copy(rec_bank(REC_SPR1), SPR1_LEN);
    vram_copy(rec_bank(REC_SPR2), SPR2_LEN);
    sprite(SP_BALL, SPR_BALL, 0, 0, 0, 0x52);
    sprite(SP_PLUNGER, SPR_PLUNGER, (PLUNGER_X >> 6) - 8, PLUNGER_REST >> 6, Z_FRONT, 0x93);
    flipper(0, 0);
    flipper(1, 0);
    shown_frame[0] = shown_frame[1] = 0;
}

/*---------------------------------------------------------------- lamps */
static u8 lamp_now[NLAMP], lamp_shown[NLAMP];

/* lamp n's cards lit or off: their map entries (pic.h), straight into the
   map ($1AC00 + 256 y + 2 x; the kernel writes the same way when no menu
   or box covers a card: the game only runs then) */
static void cards(u8 n, u8 lit)
{
    u8 i, x = lamp_x[n], y = lamp_y[n], w = lamp_w[n], h = lamp_h[n];
    const u16 *e = (lit ? lamp_e1 : lamp_e0) + lamp_at[n];
    while (h--) {
        VERA_CTRL = 0;
        VERA_ADDR_L = (u8)(2 * x);
        VERA_ADDR_M = (u8)(0xAC + y);
        VERA_ADDR_H = 0x11;         /* increment 1, bank 1 */
        for (i = 0; i < w; ++i) {
            VERA_DATA0 = (u8)*e;
            VERA_DATA0 = (u8)(*e++ >> 8);
        }
        ++y;
    }
}

static void lamps(u8 all)
{
    u8 i, c;
    game_lamps(lamp_now);
    for (i = 0; i < NLAMP; ++i) {
        c = lamp_now[i];
        if (c != lamp_shown[i] || all) {
            lamp_shown[i] = c;
            cards(i, c);
        }
    }
}

/*---------------------------------------------------------------- the panel */
static u32 shown_score = 0xFFFFFFFFUL;
static u8 shown_digit[9] = { 255, 255, 255, 255, 255, 255, 255, 255, 255 };
static u8 shown_ball = 255, shown_mult, shown_extra, shown_msg = 255, shown_arg, shown_prog = 255;
static u8 shown_status = 255;
static char line[40];

/* a text's width; for a menu item (Veranda Look 3, Sunroom) a leading
   $1F (a group starts: the kernel draws a dotted line above the item, not
   the byte) is a code, 0 wide; "Name\tKey": the TAB counts 24 pixels
   between the name and the key (the kernel puts the key 8 pixels from
   the list's right edge) */
static u16 strw(const char *s)
{
    u16 w = 0;
    for (; *s; ++s)
        w += *s == '\t' ? 24 : GetCharWidth(*s);
    return w;
}

static char *dec(char *p, u32 v)
{
    char d[11];
    u8 n = 0;
    do {
        d[n++] = (char)('0' + (u8)(v % 10));
        v /= 10;
    } while (v);
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

/* a line of the panel: 20 characters from card 59 at pixel row y, the
   text centred, spaces round it (x16.s: the panel's own font) */
static char fld[21];

static void field(const char *s, u16 y)
{
    u8 n = (u8)strlen(s), i, k;
    if (n > 20)
        n = 20;
    k = (u8)((20 - n) / 2);
    for (i = 0; i < 20; ++i)
        fld[i] = ' ';
    memcpy(fld + k, s, n);
    fld[20] = 0;
    vram_text(fld, y * 80 + 59);
}

static void draw_score(void)
{
    static const u32 pow10[9] = { 100000000UL, 10000000UL, 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    u32 v = G.score;
    u8 d[9], i, k;
    if (v > 999999999UL)
        v = 999999999UL;
    for (i = 0; i < 9; ++i) {
        for (k = 0; v >= pow10[i]; ++k)
            v -= pow10[i];
        d[i] = k;
    }
    for (i = 0; i < 8 && !d[i]; ++i)
        d[i] = 10;          /* leading blanks */
    for (i = 0; i < 9; ++i)
        if (d[i] != shown_digit[i]) {
            shown_digit[i] = d[i];
            vram_digit(d[i], 40 * 80 + 60 + 2 * i);
        }
    shown_score = G.score;
}

static const char *const msgs[] = {
    "", "Press Enter to play", "Hold Space, let go", "Beam lit! Bonus ", "Buoys cleared!", "Ferry ",
    "Ferry combo!", "Lighthouse lit: Jackpot!", "JACKPOT ", "Extra ball!", "Ball saved", "Shoot again",
    "Danger!", "TILT", "Bonus x", "Ball ", "Game over", "Harbour loop!", ""
};

static void draw_msg(void)
{
    char *p = line;
    strcpy(line, msgs[G.msg]);
    p += strlen(line);
    switch (G.msg) {
    case M_BEAM: case M_BONUS:
        p = dec(p, G.msg_arg);
        if (G.msg == M_BEAM)
            strcpy(p, "X");
        break;
    case M_FERRY:
        p = dec(p, G.msg_arg);
        strcpy(p, " of 3");
        break;
    case M_JACKPOT: case M_BALL:
        dec(p, G.msg_arg);
        break;
    }
    if (G.msg == M_NONE && G.state == G_PLAY && G.ball_state == B_LANE)
        strcpy(line, msgs[M_PLUNGE]);
    field(line, 124);
    shown_msg = G.msg;
    shown_arg = G.msg_arg;
}

static void draw_ball(void)
{
    char *p;
    strcpy(line, "BALL ");
    p = dec(line + 5, G.state == G_PLAY ? G.ball : 0);
    strcpy(p, G.extra ? "/3+1" : "/3  ");
    strcpy(p + 4, "   BONUS ");
    dec(p + 13, G.mult);
    strcat(line, "X");
    field(line, 88);
    shown_ball = G.ball;
    shown_mult = G.mult;
    shown_extra = G.extra;
}

static u8 progress(void)
{
    u8 n = 0, i;
    for (i = 0; i < 4; ++i)
        n += (G.lanes >> i) & 1;
    return (u8)(n | (G.ferries > 3 ? 3 : G.ferries) << 3 | (G.drops == 7 ? 3 : (G.drops & 1) + (G.drops >> 1 & 1) + (G.drops >> 2)) << 5);
}

static void draw_progress(void)
{
    static const u8 rows[3] = { 177, 193, 209 };
    u8 v = progress(), i, n, of;
    for (i = 0; i < 3; ++i) {
        n = i == 0 ? (v & 7) : i == 1 ? (v >> 5) : (v >> 3) & 3;
        of = i == 0 ? 4 : 3;
        if (G.missions & (1 << i))
            n = of;
        line[0] = (char)('0' + n);
        line[1] = '/';
        line[2] = (char)('0' + of);
        line[3] = 0;
        vram_text(line, rows[i] * 80 + 75);
    }
    shown_prog = v;
}

static u8 status(void)
{
    return (u8)(snd_mute | (G.tilted << 1) | (G.state << 2));
}

static void draw_status(void)
{
    field(G.tilted ? "* TILT *" : G.state == G_PLAY ? "GOOD LUCK, CAPTAIN" : "ENTER: A NEW GAME", 424);
    field(snd_mute ? "SOUND OFF" : "SOUND ON", 444);
    shown_status = status();
}

/*---------------------------------------------------------------- high scores */
#define NHI 5
static struct {
    char magic[4];
    u8 version, sound, pad[2];
    struct { char name[12]; u32 score; } e[NHI];
} hi;
static const char hi_name[] = "Lighthouse Pinball Scores";

static void hi_default(void)
{
    static const char *const names[NHI] = { "Keeper", "Pilot", "Ferryman", "Bosun", "Gull" };
    u8 i;
    memset(&hi, 0, sizeof hi);
    memcpy(hi.magic, "LHPB", 4);
    hi.version = 1;
    for (i = 0; i < NHI; ++i) {
        strcpy(hi.e[i].name, names[i]);
        hi.e[i].score = 250000UL - 50000UL * i;
    }
}

static void hi_load(void)
{
    u8 h;
    vr_cd(VR_DATA);
    h = FOpen((void *)hi_name, 'r');
    if (h >= 2) {
        FRead(h, &hi, sizeof hi);
        FClose(h);
    }
    vr_cdback();
    if (memcmp(hi.magic, "LHPB", 4) || hi.version != 1)
        hi_default();
    snd_mute = hi.sound;
}

static void hi_save(void)
{
    u8 h;
    hi.sound = snd_mute;
    vr_cd(VR_DATA);
    h = FOpen((void *)hi_name, 'w');
    if (h >= 2) {
        FWrite(h, &hi, sizeof hi);
        FClose(h);
    }
    vr_cdback();
}

static void draw_hiscores(void)
{
    u8 i, n;
    char *p;
    for (i = 0; i < NHI; ++i) {
        memset(line, ' ', 20);
        line[0] = (char)('1' + i);
        n = (u8)strlen(hi.e[i].name);
        memcpy(line + 2, hi.e[i].name, n > 10 ? 10 : n);
        p = dec(line + 21, hi.e[i].score);
        n = (u8)(p - (line + 21));
        memcpy(line + 20 - n, line + 21, n);
        line[20] = 0;
        vram_text(line, (u16)(262 + 28 * i) * 80 + 59);
    }
}

/* everything in the bitmap again: the table's picture, the panel */
static void redraw(void)
{
    u8 i;
    draw_art();
    for (i = 0; i < 9; ++i)
        shown_digit[i] = 255;
    draw_score();
    draw_msg();
    draw_ball();
    draw_progress();
    draw_status();
    draw_hiscores();
    lamps(1);
}

/*---------------------------------------------------------------- dialog boxes */
static char l1[40], l2[40], l3[40], l4[40], l5[40], name[12];

/* (Sunroom: 8 pixels more under the text, the OK button 8 from the bottom) */
#define BOX 132, 251, 160, 0, 223, 1    /* top, bottom (bytes), left 160, right 479 */
#define SC_NAME 196                     /* the high scores: the names' x, the scores' right edge */
#define SC_RIGHT 404
static const u8 box_info[] = {
    1, BOX,
    DBVARSTR, 16, 18, 0x0C,
    DBVARSTR, 16, 34, 0x0E,
    DBVARSTR, 16, 50, 0x10,
    DBVARSTR, 16, 66, 0x12,
    DBVARSTR, 16, 82, 0x14,
    OK, 15, 96,
    0
};
static const u8 box_name[] = {
    1, BOX,
    DBVARSTR, 16, 20, 0x0C,
    DBVARSTR, 16, 44, 0x10,
    DBGETSTRING, 16, 58, 0x0E, 11,
    OK, 15, 96,
    0
};

static void box_open(void)
{
    sprites_show(0);
    hook_on = 0;
}

static void redraw(void);

/* after a box: the kernel gives back what it covered, but not always the
   shadow's strip (it recovers from the back screen, which the program
   does not use): the table and the panel drawn again */
static void box_close(void)
{
    redraw();
    sprites_show(1);
    hook_on = 1;
    done = frames;
#ifdef PB_TEST
    skip_timing = 1;
#endif
}

static void info(void)
{
    box_open();
    r5 = (u16)l1;
    r6 = (u16)l2;
    r7 = (u16)l3;
    r8 = (u16)l4;
    r9 = (u16)l5;
    DoDlgBox((void *)box_info);
    box_close();
}

#ifdef PB_TEST
void __fastcall__ kbd_put(u8 c);
#endif

static void game_over(void)
{
    u8 i, k;
    for (i = 0; i < NHI && G.score <= hi.e[i].score; ++i)
        ;
    if (i == NHI)
        return;
    strcpy(l1, "\x18" "A new high score: ");   /* (bold: the box's question) */
    strcpy(dec(l1 + 19, G.score), "\x1b");   /* (PLAINTEXT: the next lines plain) */
    strcpy(l3, "Your name, harbour master:");
    memset(name, 0, sizeof name);
    box_open();
#ifdef PB_TEST
    {
        static const char typed[] = "GATE\r";
        for (k = 0; typed[k]; ++k)
            kbd_put(typed[k]);
    }
#endif
    r5 = (u16)l1;
    r6 = (u16)name;
    r7 = (u16)l3;
    DoDlgBox((void *)box_name);
    box_close();
    if (!name[0])
        strcpy(name, "Anonymous");
    for (k = NHI - 1; k > i; --k)
        hi.e[k] = hi.e[k - 1];
    memset(hi.e[i].name, 0, 12);
    strncpy(hi.e[i].name, name, 11);
    hi.e[i].score = G.score;
    hi_save();
    draw_hiscores();
    done = frames;                  /* (the box and the file: not the game's frames) */
#ifdef PB_TEST
    LogText("PB HISCORE SAVED");
#endif
}

/*---------------------------------------------------------------- menus */
struct item { const char *text; u8 type; void *p; };
#define VMENU(n) struct { u8 top, bottom; u16 left, right; u8 flags; struct item i[n]; }

static void m_about(void), m_new(void), m_scores(void), m_quit(void), m_sound(void);
static const char t_son[] = "Sound: on", t_soff[] = "Sound: off";

static VMENU(1) mlight = { 16, 0, 0, 0, VERTICAL | 1, { { "About Lighthouse Pinball", MENU_ACTION, m_about } } };
static VMENU(3) mgame = { 16, 0, 0, 0, VERTICAL | 3, {
    { "New game\tEnter", MENU_ACTION, m_new }, { "High scores", MENU_ACTION, m_scores },
    { "\x1fQuit", MENU_ACTION, m_quit } } };
static VMENU(1) mopts = { 16, 0, 0, 0, VERTICAL | 1, { { t_son, MENU_ACTION, m_sound } } };
static VMENU(3) mbar = { 0, 14, 0, 0, HORIZONTAL | UN_CONSTRAINED | 3, {
    { "Lighthouse", SUB_MENU, &mlight }, { "Game", SUB_MENU, &mgame }, { "Options", SUB_MENU, &mopts } } };

static void size_menu(void *m, u16 left)
{
    VMENU(3) *v = m;
    u8 i, n = v->flags & 31;
    u16 w, wmax = 0;
    for (i = 0; i < n; ++i) {
        w = strw(v->i[i].text);
        if (w > wmax)
            wmax = w;
    }
    v->left = left;
    v->right = left + 8 + wmax + 8;     /* (the text 8 pixels in, 8 after the longest) */
    v->bottom = (u8)(v->top + n * 16);
}

static void menus(void)
{
    static void *const subs[3] = { &mlight, &mgame, &mopts };
    u8 i;
    u16 b = 0;
    mopts.i[0].text = snd_mute ? t_soff : t_son;
    for (i = 0; i < 3; ++i) {
        size_menu(subs[i], b);
        b += 8 + strw(mbar.i[i].text);
    }
    mbar.right = b;
    DoMenu(&mbar, 0);
}

static void m_about(void)
{
    GotoFirstMenu();
    strcpy(l1, "\x18" "Lighthouse Pinball 1.0\x1b");   /* (bold: the box's title) */
    strcpy(l2, "A night harbour table for Veranda.");
    strcpy(l3, "Z or Shift: flippers. Space: plunger.");
    strcpy(l4, "Arrows: nudge. Enter: a new game.");
    strcpy(l5, "(c) 2026 Marcin Spoczynski, MIT.");
    info();
}

static void m_new(void)
{
    GotoFirstMenu();
    in_latch |= IN_START;
    G.state = G_ATTRACT;
}

static void m_scores(void)
{
    char *b[5], *p;
    u8 i;
    GotoFirstMenu();
    b[0] = l1, b[1] = l2, b[2] = l3, b[3] = l4, b[4] = l5;
    UseFont(FONT_UI);                   /* (the boxes' font: measured) */
    for (i = 0; i < NHI; ++i) {
        /* "1.", the name at SC_NAME, the score right-aligned at SC_RIGHT
           (GOTOX: the kernel's text code $14, a word x; odd, never a 0) */
        p = b[i];
        p[0] = (char)('1' + i);
        p[1] = '.';
        p[2] = 0x14;
        p[3] = (char)SC_NAME;
        p[4] = SC_NAME >> 8;
        p = stpcpy(p + 5, hi.e[i].name);
        dec(p + 3, hi.e[i].score);
        *(u16 *)(p + 1) = (SC_RIGHT - strw(p + 3)) | 1;
        *p = 0x14;
    }
    info();
}

static void leave(void)
{
    vr_set_irq(0);
    hook_remove();
    snd_stop_all();
    sprites_show(0);
    sprite_z(SP_BALL, 0);
    vr_pic_off();                   /* (palette rows 2-15 as vr_pic_on found them) */
    palette_restore();
    blankDelay = saved_blank;
}

static void m_quit(void)
{
    GotoFirstMenu();
    leave();
    EnterDeskTop();
}

static void m_sound(void)
{
    snd_mute ^= 1;
    if (snd_mute)
        snd_stop_all();
    mopts.i[0].text = snd_mute ? t_soff : t_son;
    GotoFirstMenu();
    hi_save();
}

/*---------------------------------------------------------------- the main loop */
#ifdef PB_TEST
static void test_end(void)
{
    char *p;
    u8 i, ok = 1;
    test_done = 1;
    strcpy(line, "PB TIME ");
    p = dec(line + 8, t_n);
    *p++ = ' ';
    p = dec(p, t_max);
    *p++ = ' ';
    p = dec(p, t_n ? t_sum / t_n : 0);
    *p++ = ' ';
    p = dec(p, p_max);
    *p++ = ' ';
    dec(p, late);
    LogText(line);
    leave();
    vat(0x1FA00UL);
    for (i = 0; i < 128; ++i)
        if (VERA_DATA0 != pal_saved[i])
            ok = 0;
    LogText(ok ? "PB PALETTE RESTORED" : "PB PALETTE CHANGED");
    *(volatile u8 *)0x9FB4 = 1;         /* the emulator: a dump at the end */
    __asm__("jmp $FFFF");
}
#endif

static void one_tick(void)
{
    u8 in, was = G.state, i;
#ifdef PB_TEST
    RAM_BANK = sbank;
    while (spos < 0x1FF0 && *(u16 *)(0xA000 + spos) == tick) {
        key_code(*(u8 *)(0xA002 + spos));
        spos += 3;
    }
    RAM_BANK = appBank;
#endif
    __asm__("sei");
    in = in_held | in_latch;
    in_latch = 0;
    __asm__("cli");
    game_tick(in);
    for (i = 0; i < snd_n; ++i)
        if (!snd_mute)
            sound_play(snd_q[i]);
    snd_n = 0;
#ifdef PB_TEST
    for (i = 0; i < ev_n; ++i)
        if (trace_event(tick, i))
            LogText(trace_buf);
    if (G.state == G_OVER && was != G_OVER) {
        trace_line("PB OVER", tick, G.score, 8);
        LogText(trace_buf);
    }
    if ((tick & 255) == 255) {
        trace_line("PB T", tick, trace_hash, 4);
        LogText(trace_buf);
    }
#endif
    if (G.state == G_OVER && was != G_OVER)
        over_pending = 1;
    ++tick;
}

static void pause(u8 on)
{
    paused = on;
    sprites_show(!on);
}

static void poll(void)
{
    u8 f = frames, n, what;
#ifdef PB_TEST
    u32 c;
    if (test_done)
        return;
#endif
    if (vr_pic_back()) {            /* back from the switcher: palette rows 2-15 again */
        vr_vram_load(rec_bank(REC_PIC));
        palette_game();
    }
    if (menuNumber) {               /* a menu is open: the game waits */
        if (!paused)
            pause(1);
        done = f;
        return;
    }
    if (paused)
        pause(0);
    n = (u8)(f - done);
    if (!n)
        return;
    done = f;
    if (n > 1) {
#ifdef PB_TEST
        trace_line("PB LATE", tick, n, 2);
        LogText(trace_buf);
#endif
        late += n - 1;
        if (n > 4)
            n = 4;
    }
#ifdef PB_TEST
    CLOCK_RESET();
#endif
    while (n--) {
        one_tick();
#ifdef PB_TEST
        if (tick >= send) {
            test_end();
            return;
        }
#ifdef PB_ASIDE
        if (tick == PB_ASIDE)       /* (the gate's A: put aside as by Alt + Tab) */
            TaskCtl(TASK_ASIDE, 0, 0);
#endif
#ifdef PB_SHOT
        if (tick == PB_SHOT) {
            *(volatile u8 *)0x9FB4 = 1;
            __asm__("jmp $FFFF");
        }
#endif
#endif
    }
#ifdef PB_TEST
    c = clock_read();
    if (c > p_max)
        p_max = c;
#endif
    if (over_pending) {
        over_pending = 0;
        game_over();
    }
    sprites_draw();
    lamps(0);
    what = 0;
    if (G.score != shown_score)
        draw_score(), what = 1;
    else if (G.msg != shown_msg || G.msg_arg != shown_arg)
        draw_msg(), what = 2;
    else if (G.ball != shown_ball || G.mult != shown_mult || G.extra != shown_extra)
        draw_ball(), what = 3;
    else if (progress() != shown_prog)
        draw_progress(), what = 4;
    else if (status() != shown_status)
        draw_status(), what = 5;
#ifdef PB_TEST
    c = clock_read();
    if (skip_timing) {
        skip_timing = 0;
        c = 0;
    }
    if (c > t_max)
        t_max = c;
    t_sum += c;
    ++t_n;
    if (c > 100000) {
        char *p;
        strcpy(line, "PB SLOW ");
        p = dec(line + 8, tick);
        *p++ = ' ';
        p = dec(p, c);
        *p++ = ' ';
        *p++ = (char)('0' + what);
        *p++ = ' ';
        dec(p, substeps);
        LogText(line);
    }
#endif
}

int main(void)
{
    mul_init();
    saved_blank = blankDelay;
    blankDelay = 0;
    palette_save();
    /* the program draws to the foreground only (VRAM straight): menus and
       boxes then keep what they cover on the save-under stack */
    dispBufferOn = ST_WR_FORE;
    theme_load();
    uiFont = FONT_UI;
    UseFont(FONT_UI);
    theme_menus();
    palette_game();
    vr_pic_on();                    /* (after the paper, entry 0: the covered cards' row 15) */
    load_sprites();                 /* (after vr_pic_on: it clears the back screen) */
    draw_table();
    hi_load();
    game_init();
    draw_hiscores();
    draw_score();
    draw_msg();
    draw_ball();
    draw_progress();
    draw_status();
    lamps(1);
    menus();
#ifdef PB_TEST
    sbank = rec_bank(REC_SCRIPT);
    RAM_BANK = sbank;
    for (spos = 0; *(u16 *)(0xA000 + spos) != 0xFFFF; spos += 3)
        ;
    send = *(u16 *)(0xA002 + spos);
    spos = 0;
    RAM_BANK = appBank;
#endif
#ifdef PB_TEST
    {
        /* the palette found: entries 0-63 as $0RGB, 16 a line (the gate
           compares the one after the end with it) */
        static const char hx[] = "0123456789ABCDEF";
        static char pl[8 + 16 * 4];
        u8 i, j, k;
        u16 v;
        for (j = 0; j < 4; ++j) {
            memcpy(pl, "PB PAL0", 7);
            pl[6] = (char)('0' + j);
            k = 7;
            for (i = 16 * j; i < 16 * j + 16; ++i) {
                v = pal_saved[2 * i] | (u16)(pal_saved[2 * i + 1] & 15) << 8;
                pl[k++] = ' ';
                pl[k++] = hx[v >> 8];
                pl[k++] = hx[(v >> 4) & 15];
                pl[k++] = hx[v & 15];
            }
            pl[k] = 0;
            LogText(pl);
        }
    }
#endif
    hook_install();
    hook_on = 1;
    vr_set_irq(pb_irq);
    done = frames;
    appMain = VR_FN(poll);
    LogText("PINBALL RUNNING");
    return 0;
}
