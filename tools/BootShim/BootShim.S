/* BootShim — VARIANTE DIAGNÓSTICO (paint test pós-AVB)
 * Pinta o cont_splash (0x5c000000) como PRIMEIRA instrução, cor = Exception Level.
 * Tela muda de cor -> nosso código executa (ABL nos lança pós-AVB).
 * Splash Samsung intacto -> código não roda / display desligado no handoff.
 * Depois segue o boot normal (copia FD -> UEFI_BASE e salta).
 * Original de produção preservado em git (master) e em BootShim.Diag.S.
 */

_Head:
	/* Set _Entry address */
	adr	x1, _Payload
	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header (byte-idêntico ao de produção) */
_StackBase0:
	.quad 0x80000          /* Text Offset */
_StackSize0:
	.quad 0x800000         /* Image Size */
	.quad 0xa              /* Flags */
	.quad 0
	.quad 0
	.quad 0
	.ascii "ARM\x64"       /* ARM64 Magic */
	.long 0
	.ascii "EDK2-MSM"

_StackBase:
	.quad UEFI_BASE
_StackSize:
	.quad UEFI_SIZE

_Start:
	/* ===== DIAGNÓSTICO: cor = Exception Level ===== */
	mrs	x9, CurrentEL
	lsr	x9, x9, #2             /* x9 = EL (0..3) */
	cmp	x9, #2
	b.eq	_c_el2
	cmp	x9, #1
	b.eq	_c_el1
	cmp	x9, #3
	b.eq	_c_el3
	/* EL0: azul 0xFF0000FF */
	movz	w8, #0x00FF
	movk	w8, #0xFF00, lsl #16
	b	_paint
_c_el2:
	/* verde 0xFF00FF00 (EL2) */
	movz	w8, #0xFF00
	movk	w8, #0xFF00, lsl #16
	b	_paint
_c_el1:
	/* vermelho 0xFFFF0000 (EL1) */
	movz	w8, #0
	movk	w8, #0xFFFF, lsl #16
	b	_paint
_c_el3:
	/* branco 0xFFFFFFFF (EL3) */
	movn	w8, #0

_paint:
	movz	x10, #0x5c00, lsl #16      /* 0x5c000000 = base cont_splash */
	movz	x12, #0x00A0, lsl #16      /* 0x00A00000 = 10 MiB (cobre 1200x2000x4) */
	add	x11, x10, x12
_paintloop:
	str	w8, [x10], #4
	cmp	x10, x11
	b.lo	_paintloop

	/* marcador 0xDEADBEEF + EL em 0x5ce00000 (fora da área visível) */
	movz	x14, #0x5ce0, lsl #16
	movz	w13, #0xBEEF
	movk	w13, #0xDEAD, lsl #16
	str	w13, [x14]
	str	w9,  [x14, #4]

	/* ===== boot normal ===== */
	mov	x4, x1
	ldr	x5, _StackBase
	cmp	x4, x5
	beq	_Entry
	ldr	x6, _StackSize
_CopyLoop:
	ldp	x2, x3, [x4], #16
	stp	x2, x3, [x5], #16
	subs	x6, x6, #16
	b.ne	_CopyLoop
	ldr	x5, _StackBase
_Entry:
	br	x5
_Dead:
	b	_Dead

.text
.align 4
_Payload:
