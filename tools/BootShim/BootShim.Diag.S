/* BootShim DIAGNÓSTICO — SM-T500 / SM6115 Bengal
 *
 * Objetivo: responder de uma vez, SÓ por software (sem serial/jig):
 *   (1) O nosso código realmente executa pós-AVB?
 *   (2) Em qual Exception Level o ABL/QHEE nos entrega? (hipótese: EL1)
 *
 * Como: cont_splash é escaneado continuamente de 0x5c000000 (é de lá que sai o
 * logo Samsung na tela). Se sobrescrevermos essa RAM como PRIMEIRA instrução, a
 * tela muda de cor IFF nosso código roda. A cor codifica o EL atual:
 *
 *   TELA VERDE  -> rodou, handoff em EL2 (esperado pelo PrePi; bug é noutro lugar)
 *   TELA VERMELHA -> rodou, handoff em EL1 (QHEE!) -> CONFIRMA a causa-raiz
 *   TELA BRANCA -> rodou em EL3 (improvável)
 *   TELA AZUL   -> rodou em EL0 (improvável)
 *   SPLASH SAMSUNG INALTERADO -> código não roda OU display já desligado no handoff
 *
 * Também grava marcador 0xDEADBEEF + EL em 0x5ce00000 (fora da área visível) para
 * confirmação cruzada via RAMdump EDL, caso a tela seja inconclusiva.
 *
 * Depois do diagnóstico, segue o boot normal (copia FD -> UEFI_BASE e salta), pra
 * que um boot que FUNCIONE ainda prossiga.
 */

_Head:
	/* Set _Entry address */
	adr	x1, _Payload
	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header (idêntico ao BootShim.S de produção) */
_StackBase0:
	.quad 0x80000          /* Text Offset */
_StackSize0:
	.quad 0x800000         /* Image Size (não-zero senão o ABL Bengal não lança) */
	.quad 0xa              /* Flags: LE + 2MB-aligned (= kernel stock) */
	.quad 0
	.quad 0
	.quad 0
	.ascii "ARM\x64"       /* ARM64 Magic */
	.long 0
	.ascii "EDK2-MSM"

_StackBase:
	.quad UEFI_BASE        /* Text Offset (destino do FD) */
_StackSize:
	.quad UEFI_SIZE        /* Image Size do FD */

_Start:
	/* ===== DIAGNÓSTICO: pinta framebuffer com cor = Exception Level ===== */
	mrs	x9, CurrentEL
	lsr	x9, x9, #2         /* x9 = EL atual (0..3) */

	cmp	x9, #2
	b.eq	_el2
	cmp	x9, #1
	b.eq	_el1
	cmp	x9, #3
	b.eq	_el3
	/* EL0 (inesperado): azul */
	movz	w8, #0x00FF
	movk	w8, #0xFF00, lsl #16   /* 0xFF0000FF */
	b	_paint
_el2:
	movz	w8, #0xFF00
	movk	w8, #0xFF00, lsl #16   /* 0xFF00FF00 VERDE = bom (EL2) */
	b	_paint
_el1:
	movz	w8, #0x0000
	movk	w8, #0xFFFF, lsl #16   /* 0xFFFF0000 VERMELHO = QHEE/EL1 */
	b	_paint
_el3:
	movn	w8, #0                 /* 0xFFFFFFFF BRANCO = EL3 */

_paint:
	ldr	x10, =0x5c000000           /* base do cont_splash framebuffer */
	movz	x12, #0x00A0, lsl #16      /* 0x00A00000 = 10 MiB (cobre 1200x2000x4) */
	add	x11, x10, x12
_pl:
	str	w8, [x10], #4
	cmp	x10, x11
	b.lo	_pl

	/* marcador p/ RAMdump EDL: 0xDEADBEEF + EL em 0x5ce00000 (fora da tela) */
	ldr	x10, =0x5ce00000
	movz	w13, #0xBEEF
	movk	w13, #0xDEAD, lsl #16      /* 0xDEADBEEF */
	str	w13, [x10]
	str	w9,  [x10, #4]

	/* ===== boot normal (se funcionar, prossegue) ===== */
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
