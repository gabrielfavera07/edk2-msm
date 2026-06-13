/* BootShim — DIAGNÓSTICO PSHOLD (descarta a ambiguidade display-off)
 * 1ª instrução: pinta vermelho (se display ligado) + reseta o SoC via PSHOLD.
 *   código RODA  -> BOOTLOOP (Samsung reiniciando sem parar) - independe de display
 *   splash estático -> código NÃO roda (ABL não lança nosso kernel)
 * NÃO segue pro boot normal de propósito (queremos o sinal de reset).
 * Recuperar: segure Power+VolUp no boot -> recovery (ABL lê as teclas antes do kernel).
 */

_Head:
	adr	x1, _Payload
	b	_Start

	/* Fake Linux kernel header (byte-idêntico ao de produção) */
_StackBase0:
	.quad 0x80000
_StackSize0:
	.quad 0x800000
	.quad 0xa
	.quad 0
	.quad 0
	.quad 0
	.ascii "ARM\x64"
	.long 0
	.ascii "EDK2-MSM"

_StackBase:
	.quad UEFI_BASE
_StackSize:
	.quad UEFI_SIZE

_Start:
	/* ----- pinta vermelho cont_splash 0x5c000000 (caso display ON) ----- */
	movz	x10, #0x5c00, lsl #16      /* 0x5c000000 */
	movz	x12, #0x00A0, lsl #16      /* 10 MiB */
	add	x11, x10, x12
	movz	w8, #0
	movk	w8, #0xFFFF, lsl #16       /* 0xFFFF0000 vermelho */
_pl:
	str	w8, [x10], #4
	cmp	x10, x11
	b.lo	_pl

	/* ----- marcador em IMEM 0x0c1250c8 (best effort) ----- */
	movz	x14, #0x0c12, lsl #16
	movk	x14, #0x50c8
	movz	w13, #0xBEEF
	movk	w13, #0xDEAD, lsl #16
	str	w13, [x14]

	/* ----- RESET via PSHOLD 0x0440b000 -> bootloop se o código roda ----- */
	movz	x10, #0x0440, lsl #16
	movk	x10, #0xb000              /* 0x0440b000 */
	str	wzr, [x10]

	/* se não resetou: loop morto (NÃO segue boot) */
_dead:
	b	_dead

.text
.align 4
_Payload:
