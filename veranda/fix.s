; Lighthouse Pinball: the fixed-point helpers for the X16 (src/fix.c is
; the same in C, for the host; they agree bit for bit: the gate compares
; the two machines' traces).
;   s32 mul16(s16 a, s16 b)                   a * b
;   s16 mul14(s16 a, s16 b)                   (a * b) >> 14
;   s16 dot14(s16 a, s16 b, s16 c, s16 d)     (a * b + c * d) >> 14
;   u16 isqrt32(u32 v)                        floor(sqrt(v))
;   s16 divq14(s16 a, u16 d)                  (a << 14) / d, toward 0 (|a| <= d)
; (>> on a negative number: floor, as C's arithmetic shift.) The products:
; quarter squares, a * b = f(a + b) - f(|a - b|), f(n) = n * n / 4 from
; two tables of 512 entries (mul_init makes them), four 8 x 8 products a
; 16 x 16 one (fewer when a high byte is 0).
; cc65's __fastcall__: the last argument in a/x (sreg), the others on the
; C stack; a 32-bit result in a/x/sreg.
; Copyright (c) 2026 Marcin Spoczynski. MIT licence (see LICENSE).

.export _mul16, _mul14, _dot14, _isqrt32, _divq14, _mul_init
.importzp sreg
.import popax

.segment "BSS"
sq_lo:	.res 512
sq_hi:	.res 512

.segment "ZEROPAGE"
m1:	.res 2
m2:	.res 2
pr:	.res 4                  ; a product
ac:	.res 4                  ; the sum
neg:	.res 1
mx:	.res 1
my:	.res 1
ql:	.res 1
qh:	.res 1

.segment "CODE"

; the tables: f(0) = 0, f(n) = f(n - 1) + floor(n / 2)
_mul_init:
	stz sq_lo
	stz sq_hi
	ldx #1
	stz ql
	stz qh
@a:	txa
	lsr
	clc
	adc ql
	sta ql
	sta sq_lo,x
	lda qh
	adc #0
	sta qh
	sta sq_hi,x
	inx
	bne @a
	; 256-511
@b:	txa
	sec                     ; floor((256 + x) / 2) = 128 + x / 2
	ror
	clc
	adc ql
	sta ql
	sta sq_lo+256,x
	lda qh
	adc #0
	sta qh
	sta sq_hi+256,x
	inx
	bne @b
	rts

; mx * my -> ql, qh (unsigned 8 x 8)
.macro MUL8
	.local big, diff, pos
	lda mx
	clc
	adc my
	tax
	bcs big
	lda sq_lo,x
	sta ql
	lda sq_hi,x
	sta qh
	bra diff
big:	lda sq_lo+256,x
	sta ql
	lda sq_hi+256,x
	sta qh
diff:	lda mx
	sec
	sbc my
	bcs pos
	eor #$FF
	adc #1
pos:	tax
	lda ql
	sec
	sbc sq_lo,x
	sta ql
	lda qh
	sbc sq_hi,x
	sta qh
.endmacro

; m1 * m2 (unsigned 16 x 16) -> pr
umul:
	lda m1
	sta mx
	lda m2
	sta my
	MUL8
	lda ql
	sta pr
	lda qh
	sta pr+1
	stz pr+2
	stz pr+3
	lda m1+1
	bne :+
	jmp umul_nob1           ; a1 = 0: no a1 * b0, a1 * b1
:
	sta mx
	lda m2+1
	beq umul_a1b0
	sta my
	MUL8                    ; a1 * b1
	lda ql
	sta pr+2
	lda qh
	sta pr+3
umul_a1b0:	lda m2
	sta my
	MUL8                    ; a1 * b0
	jsr addmid
umul_nob1:	lda m2+1
	beq umul_r
	sta my
	lda m1
	sta mx
	MUL8                    ; a0 * b1
	jsr addmid
umul_r:	rts

addmid:	clc
	lda ql
	adc pr+1
	sta pr+1
	lda qh
	adc pr+2
	sta pr+2
	bcc :+
	inc pr+3
:	rts

; a/x: |a/x| into m2 (neg toggled for a negative)
absb:	cpx #$80
	bcc :+
	jsr negax
	inc neg
:	sta m2
	stx m2+1
	rts

absa:	cpx #$80
	bcc :+
	jsr negax
	inc neg
:	sta m1
	stx m1+1
	rts

negax:	eor #$FF
	clc
	adc #1
	pha
	txa
	eor #$FF
	adc #0
	tax
	pla
	rts

; the signed product of the operands in a/x (b) and on the stack (a) -> pr
smul:	stz neg
	jsr absb
	jsr popax
	jsr absa
	jsr umul
	lda neg
	lsr
	bcc @r
	sec                     ; negate pr
	lda #0
	sbc pr
	sta pr
	lda #0
	sbc pr+1
	sta pr+1
	lda #0
	sbc pr+2
	sta pr+2
	lda #0
	sbc pr+3
	sta pr+3
@r:	rts

_mul16:
	jsr smul
	lda pr+2
	sta sreg
	lda pr+3
	sta sreg+1
	lda pr
	ldx pr+1
	rts

; pr >> 14 in a/x (floor): bits 14-29
shr14:	lda pr+1
	asl
	lda pr+2
	rol
	sta ql
	lda pr+3
	rol
	sta qh
	lda pr+1
	asl
	asl
	lda ql
	rol
	tay
	lda qh
	rol
	tax
	tya
	rts

_mul14:
	jsr smul
	jmp shr14

_dot14:
	jsr smul                ; c * d
	lda pr
	sta ac
	lda pr+1
	sta ac+1
	lda pr+2
	sta ac+2
	lda pr+3
	sta ac+3
	jsr popax               ; b
	jsr smul                ; a * b
	clc
	lda pr
	adc ac
	sta pr
	lda pr+1
	adc ac+1
	sta pr+1
	lda pr+2
	adc ac+2
	sta pr+2
	lda pr+3
	adc ac+3
	sta pr+3
	jmp shr14

; isqrt32: v in a/x/sreg. The digit-by-digit method: r, b (a power of 4)
.segment "ZEROPAGE"
sv:	.res 4
sr:	.res 4
sb:	.res 4
st:	.res 4
.segment "CODE"
_isqrt32:
	sta sv
	stx sv+1
	lda sreg
	sta sv+2
	lda sreg+1
	sta sv+3
	stz sr
	stz sr+1
	stz sr+2
	stz sr+3
	stz sb
	stz sb+1
	stz sb+2
	lda #$40
	sta sb+3                ; b = 1 << 30
	; while b > v: b >>= 2
@w:	lda sb+3
	cmp sv+3
	bne @c
	lda sb+2
	cmp sv+2
	bne @c
	lda sb+1
	cmp sv+1
	bne @c
	lda sb
	cmp sv
@c:	beq @loop
	bcc @loop
	jsr shr2b
	lda sb
	ora sb+1
	ora sb+2
	ora sb+3
	bne @w
	bra @done
@loop:	; t = r + b
	clc
	lda sr
	adc sb
	sta st
	lda sr+1
	adc sb+1
	sta st+1
	lda sr+2
	adc sb+2
	sta st+2
	lda sr+3
	adc sb+3
	sta st+3
	; r >>= 1 (both branches)
	lsr sr+3
	ror sr+2
	ror sr+1
	ror sr
	; v >= t ?
	lda sv+3
	cmp st+3
	bne @c2
	lda sv+2
	cmp st+2
	bne @c2
	lda sv+1
	cmp st+1
	bne @c2
	lda sv
	cmp st
@c2:	bcc @nx
	; v -= t; r += b
	sec
	lda sv
	sbc st
	sta sv
	lda sv+1
	sbc st+1
	sta sv+1
	lda sv+2
	sbc st+2
	sta sv+2
	lda sv+3
	sbc st+3
	sta sv+3
	clc
	lda sr
	adc sb
	sta sr
	lda sr+1
	adc sb+1
	sta sr+1
	lda sr+2
	adc sb+2
	sta sr+2
	lda sr+3
	adc sb+3
	sta sr+3
@nx:	jsr shr2b
	lda sb
	ora sb+1
	ora sb+2
	ora sb+3
	bne @loop
@done:	lda sr
	ldx sr+1
	rts

shr2b:	lsr sb+3
	ror sb+2
	ror sb+1
	ror sb
	lsr sb+3
	ror sb+2
	ror sb+1
	ror sb
	rts

; divq14: d in a/x, a on the C stack; |a| <= d, so the quotient (|a| << 14)
; / d is below 2^15: the remainder starts as |a| >> 1 (< d) and 15 bits
; come down (|a|'s bit 0, then 14 zeros)
.segment "ZEROPAGE"
dr:	.res 2                  ; the remainder
dd:	.res 2
dq:	.res 2
db:	.res 1
.segment "CODE"
_divq14:
	sta dd
	stx dd+1
	jsr popax
	stz neg
	cpx #$80
	bcc @p
	jsr negax
	inc neg
@p:	stx dr+1
	lsr dr+1
	ror
	sta dr                  ; |a| >> 1
	lda #0
	ror
	sta db                  ; |a|'s bit 0 in bit 7: the first to come down
	stz dq
	stz dq+1
	ldy #15
@l:	asl db
	rol dr
	rol dr+1
	bcs @sub
	lda dr+1
	cmp dd+1
	bne @c
	lda dr
	cmp dd
@c:	bcc @n
@sub:	lda dr
	sbc dd
	sta dr
	lda dr+1
	sbc dd+1
	sta dr+1
	sec
	bra @q
@n:	clc
@q:	rol dq
	rol dq+1
	dey
	bne @l
	lda dq
	ldx dq+1
	ldy neg
	beq @r
	jsr negax
@r:	rts
