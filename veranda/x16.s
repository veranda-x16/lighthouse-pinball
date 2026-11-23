; Lighthouse Pinball: the X16's own parts in assembly
;   the keyboard hook (keys held and released, for the flippers),
;   the frame interrupt (a counter and the PSG sound player),
;   VRAM: the table's picture unpacked, records copied, the dot display's
;   digits, the sprites' attributes.
; Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).

.include "x16hw.inc"

.export _hook_install, _hook_remove, _hook_on, _key_code
.export _pb_irq, _frames, _snd_start, _snd_stop_all, _snd_mute
.export _vram_unpack, _vram_copy, _vram_digit, _vram_fill
.import _keymap, _in_held, _in_latch

KEYHDL  = $032E

.segment "ZEROPAGE"
src:    .res 2
cnt:    .res 2
isrc:   .res 2                  ; the interrupt's own pointer

.segment "DATA"
_frames:   .byte 0
_hook_on:  .byte 0              ; 1: the game's keys are taken
_snd_mute: .byte 0
old_hook:  .word 0
hooked:    .byte 0

.segment "CODE"

;---------------------------------------------------------------
; the keyboard hook: the ROM calls it (through KEYHDL) with a = the key
; number, bit 7 set when released (x: an extended code's second byte).
; A key of the game (keymap) changes in_held and in_latch and is taken
; (a = 0) while hook_on; any other key, or all of them while a menu or a
; dialog box has the keyboard, goes on to the hook before (Veranda's).
;---------------------------------------------------------------
hook:
	pha
	lda _hook_on
	beq @pass
	pla
	pha
	jsr _key_code
	beq @pass
	pla
	lda #0
	rts
@pass:	pla
	jmp (old_hook)

; key_code(a): the C model's key_event; returns a = the key's bits (0:
; not the game's). Keeps x and y.
_key_code:
	phy
	pha
	and #$7F
	tay
	lda _keymap,y
	beq @no
	tay                     ; y = the bits
	pla
	bmi @up
	tya
	tsb _in_held
	tsb _in_latch
	ply
	ora #0
	rts
@up:	tya
	trb _in_held
	ply
	ora #0
	rts
@no:	pla
	ply
	lda #0
	rts

_hook_install:
	lda hooked
	bne @r
	sei
	lda KEYHDL
	sta old_hook
	lda KEYHDL+1
	sta old_hook+1
	lda #<hook
	sta KEYHDL
	lda #>hook
	sta KEYHDL+1
	cli
	inc hooked
@r:	rts

_hook_remove:
	lda hooked
	beq @r
	sei
	lda old_hook
	sta KEYHDL
	lda old_hook+1
	sta KEYHDL+1
	cli
	stz hooked
@r:	rts

;---------------------------------------------------------------
; sounds: a voice plays a list of notes, 7 bytes each: the frequency
; word, the waveform byte, the volume (0-63), its decay a frame, the
; length in frames (0 ends), a signed slide a frame (x 4 on the
; frequency). PSG voices 0-4 (Veranda's sounds use 14 and 15).
;---------------------------------------------------------------
NV = 5
.segment "DATA"
v_ptr:   .res NV * 2            ; the next note (0: idle)
v_freq:  .res NV * 2
v_wave:  .res NV
v_vol:   .res NV
v_decay: .res NV
v_len:   .res NV
v_slide: .res NV
v_dirty: .res NV               ; registers to write (1), silence (2)

.segment "CODE"
; snd_start(voice, notes): __fastcall__: a/x = notes, the voice on the C stack
.import popa, popax
_snd_start:
	pha
	phx
	jsr popa                ; the voice
	tay
	plx
	pla
	cpy #NV
	bcs @r
	sei
	pha
	tya
	asl
	tay
	pla
	sta v_ptr,y
	txa
	sta v_ptr+1,y
	tya
	lsr
	tay
	lda #0
	sta v_len,y             ; the next frame takes the first note
	cli
@r:	rts

_snd_stop_all:
	sei
	ldy #NV - 1
@l:	lda #0
	sta v_len,y
	lda #2
	sta v_dirty,y
	tya
	asl
	tax
	stz v_ptr,x
	stz v_ptr+1,x
	dey
	bpl @l
	cli
	rts

; a frame of the voices (in the interrupt)
snd_step:
	ldy #NV - 1
@voice:
	sty cv
	tya
	asl
	tax                     ; x = 2 * voice
	lda v_len,y
	beq @next_note
	dec a
	sta v_len,y
	lda v_vol,y             ; the decay
	sec
	sbc v_decay,y
	bcs :+
	lda #0
:	sta v_vol,y
	lda v_slide,y           ; the slide (x 4)
	beq @d
	bmi @neg
	asl
	asl
	clc
	adc v_freq,x
	sta v_freq,x
	bcc @d
	inc v_freq+1,x
	bra @d
@neg:	asl
	asl
	clc
	adc v_freq,x
	sta v_freq,x
	bcs @d
	dec v_freq+1,x
@d:	lda #1
	sta v_dirty,y
	bra @nextv
@next_note:
	lda v_ptr,x
	ora v_ptr+1,x
	beq @nextv
	lda v_ptr,x
	sta isrc
	lda v_ptr+1,x
	sta isrc+1
	ldy #5
	lda (isrc),y            ; the length
	bne @take
	ldy cv                  ; the end: silence
	stz v_ptr,x
	stz v_ptr+1,x
	lda #2
	sta v_dirty,y
	bra @nextv
@take:	ldy cv
	sta v_len,y
	ldy #0
	lda (isrc),y
	sta v_freq,x
	iny
	lda (isrc),y
	sta v_freq+1,x
	iny
	lda (isrc),y
	ldy cv
	sta v_wave,y
	ldy #3
	lda (isrc),y
	ldy cv
	sta v_vol,y
	ldy #4
	lda (isrc),y
	ldy cv
	sta v_decay,y
	ldy #6
	lda (isrc),y
	ldy cv
	sta v_slide,y
	lda #1
	sta v_dirty,y
	clc
	lda isrc
	adc #7
	sta v_ptr,x
	lda isrc+1
	adc #0
	sta v_ptr+1,x
@nextv:	ldy cv
	dey
	bmi @write
	jmp @voice

	; the registers of the voices that changed: $1F9C0 + 4 * voice
@write:
	ldy #NV - 1
@w:	lda v_dirty,y
	beq @wn
	tya
	asl
	asl
	clc
	adc #$C0
	sta VERA_ADDR_L
	lda #$F9
	sta VERA_ADDR_M
	lda #$11                ; increment 1, bank 1
	sta VERA_ADDR_H
	lda v_dirty,y
	cmp #2
	beq @silent
	lda _snd_mute
	bne @silent
	tya
	asl
	tax
	lda v_freq,x
	sta VERA_DATA0
	lda v_freq+1,x
	sta VERA_DATA0
	lda v_vol,y
	ora #$C0
	sta VERA_DATA0
	lda v_wave,y
	sta VERA_DATA0
	bra @wd
@silent:
	stz VERA_DATA0
	stz VERA_DATA0
	stz VERA_DATA0
	stz VERA_DATA0
@wd:	lda #0
	sta v_dirty,y
@wn:	dey
	bpl @w
	rts

.segment "DATA"
tmp:	.res 4
saved:	.res 4
cv:	.res 1

.segment "CODE"
; the frame interrupt (vr_set_irq): the counter, the sounds; VERA's port 0
; and CTRL kept
_pb_irq:
	inc _frames
	lda VERA_CTRL
	sta saved
	stz VERA_CTRL
	lda VERA_ADDR_L
	sta saved+1
	lda VERA_ADDR_M
	sta saved+2
	lda VERA_ADDR_H
	sta saved+3
	jsr snd_step
	lda saved+1
	sta VERA_ADDR_L
	lda saved+2
	sta VERA_ADDR_M
	lda saved+3
	sta VERA_ADDR_H
	lda saved
	sta VERA_CTRL
	rts

;---------------------------------------------------------------
; VRAM (port 0 already pointed, increment 1, by the caller)
;---------------------------------------------------------------
; vram_unpack(bank): the record at $A000 of RAM bank a: a length word, then
; runs (c < 128: c + 1 bytes; c >= 128: the next byte c - 126 times)
_vram_unpack:
	ldx $00
	phx
	sta $00
	lda $A000
	sta cnt
	lda $A001
	sta cnt+1
	lda #<$A002
	sta src
	lda #>$A002
	sta src+1
@tok:	lda cnt
	ora cnt+1
	beq @done
	lda (src)
	jsr @adv
	cmp #128
	bcs @run
	tax                     ; x + 1 literal bytes
	inx
@lit:	lda (src)
	sta VERA_DATA0
	jsr @adv
	dex
	bne @lit
	bra @tok
@run:	sec
	sbc #126
	tax
	lda (src)
	jsr @adv
@rep:	sta VERA_DATA0
	dex
	bne @rep
	bra @tok
@done:	pla
	sta $00
	rts
@adv:	inc src
	bne :+
	inc src+1
:	pha
	lda cnt
	bne :+
	dec cnt+1
:	dec cnt
	pla
	rts

; vram_copy(bank, count): count bytes from $A000 of the bank
_vram_copy:
	sta cnt
	stx cnt+1
	jsr popa
	ldx $00
	phx
	sta $00
	lda #<$A000
	sta src
	lda #>$A000
	sta src+1
	ldy #0
@l:	lda cnt
	ora cnt+1
	beq @d
	lda (src),y
	sta VERA_DATA0
	iny
	bne :+
	inc src+1
:	lda cnt
	bne :+
	dec cnt+1
:	dec cnt
	bra @l
@d:	pla
	sta $00
	rts

; vram_fill(value, count)
_vram_fill:
	sta cnt
	stx cnt+1
	jsr popa
	ldx cnt
	ldy cnt+1
	iny
	cpx #0
	beq @hi
@l:	sta VERA_DATA0
	dex
	bne @l
@hi:	dey
	beq @d
	ldx #0
	bra @l
@d:	rts

; vram_digit(glyph, address): a 16 x 24 glyph (2 bytes a row) of
; digit_font to the bitmap at address (bank 0), rows 80 bytes apart
.import _digit_font
_vram_digit:
	sta cnt                 ; the address
	stx cnt+1
	jsr popa                ; the glyph: 48 bytes each
	sta src
	stz src+1
	; src = digit_font + 48 * glyph
	lda src
	asl
	asl
	asl
	asl                     ; 16 g (g <= 10: fits a byte)
	sta tmp
	lda #0
	sta tmp+1
	lda tmp
	asl
	rol tmp+1
	adc tmp                 ; 48 g = 32 g + 16 g
	sta src
	lda tmp+1
	adc #0
	sta src+1
	clc
	lda src
	adc #<_digit_font
	sta src
	lda src+1
	adc #>_digit_font
	sta src+1
	stz VERA_CTRL
	ldx #24
	ldy #0
@row:	lda cnt
	sta VERA_ADDR_L
	lda cnt+1
	sta VERA_ADDR_M
	lda #$10
	sta VERA_ADDR_H
	lda (src),y
	sta VERA_DATA0
	iny
	lda (src),y
	sta VERA_DATA0
	iny
	clc
	lda cnt
	adc #80
	sta cnt
	bcc :+
	inc cnt+1
:	dex
	bne @row
	rts

; kbd_put(c): a key into the ROM's keyboard queue (the test build types
; the high score's name with it)
.export _kbd_put
_kbd_put:
	jmp k_kbdbuf_put

;---------------------------------------------------------------
; u8 scan(u8 cell): the shapes of the grid's cell whose boxes hold the
; ball's centre (b_x, b_y: unsigned there), into cand; their number.
; (phys.c has the same in C for the host.)
;---------------------------------------------------------------
.export _scan, _cand
.import _grid_off, _grid_ref, _b_x, _b_y
.import _bx0l, _bx0h, _bx1l, _bx1h, _by0l, _by0h, _by1l, _by1h
.segment "BSS"
_cand:	.res 32
.segment "ZEROPAGE"
gp:	.res 2
.segment "CODE"
_scan:
	asl
	tay                     ; 2 * cell (low byte); the carry: the high bit
	lda #<_grid_off
	sta gp
	lda #>_grid_off
	adc #0
	sta gp+1
	lda (gp),y              ; the cell's first ref
	sta src
	iny
	bne :+
	inc gp+1
:	lda (gp),y
	sta src+1
	iny
	bne :+
	inc gp+1
:	lda (gp),y              ; the next cell's first
	sec
	sbc src
	sta cnt                 ; the count (< 256)
	clc
	lda src
	adc #<_grid_ref
	sta src
	lda src+1
	adc #>_grid_ref
	sta src+1
	ldy #0
	stz cnt+1               ; candidates
@l:	cpy cnt
	beq @d
	lda (src),y
	tax
	; x >= bx0
	lda _b_x
	cmp _bx0l,x
	lda _b_x+1
	sbc _bx0h,x
	bcc @n
	; bx1 >= x
	lda _bx1l,x
	cmp _b_x
	lda _bx1h,x
	sbc _b_x+1
	bcc @n
	lda _b_y
	cmp _by0l,x
	lda _b_y+1
	sbc _by0h,x
	bcc @n
	lda _by1l,x
	cmp _b_y
	lda _by1h,x
	sbc _b_y+1
	bcc @n
	txa
	ldx cnt+1
	sta _cand,x
	inc cnt+1
@n:	iny
	bra @l
@d:	lda cnt+1
	ldx #0
	rts

;---------------------------------------------------------------
; vram_text(s, address): the string in the panel's font (8 x 8 cells,
; text_font: characters 32-95; small letters as capitals), a byte a
; character from the bitmap's address (bank 0); 8 rows each, 80 apart
;---------------------------------------------------------------
.export _vram_text
.import _text_font
.segment "ZEROPAGE"
tp:	.res 2
gf:	.res 2
.segment "CODE"
_vram_text:
	sta cnt                 ; the address
	stx cnt+1
	jsr popax
	sta tp
	stx tp+1
	stz VERA_CTRL
@ch:	lda (tp)
	beq @r
	cmp #'a'
	bcc :+
	sbc #32                 ; (carry set)
:	sec
	sbc #32
	cmp #64
	bcc :+
	lda #0
:	stz gf+1
	asl
	asl
	asl
	rol gf+1                ; 8 c (c < 64: one carry)
	clc
	adc #<_text_font
	sta gf
	lda gf+1
	adc #>_text_font
	sta gf+1
	lda cnt
	sta VERA_ADDR_L
	lda cnt+1
	sta VERA_ADDR_M
	lda #$C0                ; increment 80 (code 12), bank 0
	sta VERA_ADDR_H
	ldy #0
@row:	lda (gf),y
	sta VERA_DATA0
	iny
	cpy #8
	bne @row
	inc cnt
	bne :+
	inc cnt+1
:	inc tp
	bne @ch
	inc tp+1
	bra @ch
@r:	rts
