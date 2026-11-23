/* Lighthouse Pinball on the X16: the program's own declarations.
   Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE). */
#ifndef PBX_H
#define PBX_H
#include <veranda.h>
#include <vr.h>
#include "../src/pb.h"
#include <vrpic.h>

#define RAM_BANK    (*(volatile u8 *)0x0000)
#define VERA_ADDR_L (*(volatile u8 *)0x9F20)
#define VERA_ADDR_M (*(volatile u8 *)0x9F21)
#define VERA_ADDR_H (*(volatile u8 *)0x9F22)
#define VERA_DATA0  (*(volatile u8 *)0x9F23)
#define VERA_CTRL   (*(volatile u8 *)0x9F25)

/* x16.s */
extern volatile u8 frames;
extern u8 hook_on, snd_mute;
void mul_init(void);
void hook_install(void);
void hook_remove(void);
u8 __fastcall__ key_code(u8 code);
void pb_irq(void);
void __fastcall__ snd_start(u8 voice, const u8 *notes);
void snd_stop_all(void);
void __fastcall__ vram_unpack(u8 bank);
void __fastcall__ vram_copy(u8 bank, u16 count);
void __fastcall__ vram_fill(u8 value, u16 count);
void __fastcall__ vram_digit(u8 glyph, u16 addr);
void __fastcall__ vram_text(const char *s, u16 addr);

/* sounds.c */
void sound_play(u8 id);

/* the records (vpack: in this order): the table's picture (pic.h:
   tools/mkart.py; record 1 its palette rows), the sprites, the test
   build's script */
#define REC_PIC     1
#define REC_SPR1    (PIC_RECORDS + 1)
#define REC_SPR2    (PIC_RECORDS + 2)
#define REC_SCRIPT  (PIC_RECORDS + 3)

#endif
