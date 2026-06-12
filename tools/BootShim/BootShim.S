_Head:
	/* Set _Entry address */
	adr	x1, _Payload

	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header */

_StackBase0: //has to be 0 on some devices
	/* Text Offset - match the stock/LineageOS kernel (loads at dram_base + 0x80000) */
	.quad 0x80000

_StackSize0:
	/* Image Size - MUST be non-zero or the Samsung Bengal ABL never loads/
	   launches the kernel. 8 MiB comfortably covers BootShim + 7 MiB UEFI FD. */
	.quad 0x800000

	/* Flags - bit0 LE, bit3 2MB-aligned placement (matches stock kernel = 0xa) */
	.quad 0xa

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* ARM64 Magic */
	.ascii "ARM\x64"

	/* Reserved */
	.long 0

	.ascii "EDK2-MSM"

_StackBase:
	/* Text Offset */
	.quad UEFI_BASE

_StackSize:
	/* Image Size */
	.quad UEFI_SIZE

_Start:
	/* ---- DIAGNOSTIC: prove the ABL actually launches us by painting the
	   continuous-splash framebuffer @ 0x5C000000 before doing anything.
	   MMU is off here, so this is a direct physical write. If the screen
	   changes, BootShim ran; if it stays the Samsung splash, the ABL never
	   transferred control to our kernel. x1 (payload ptr) is preserved. ---- */
	movz	x10, #0x5C00, lsl #16     /* x10 = 0x5C000000 framebuffer base */
	movz	x11, #0x07E0
	movk	x11, #0x07E0, lsl #16     /* x11 = 0x07E007E0 (RGB565 green x2) */
	movz	x12, #0x0080, lsl #16     /* x12 = 0x00800000 = 8 MiB to fill */
_PaintLoop:
	str	w11, [x10], #4
	subs	x12, x12, #4
	b.ne	_PaintLoop
	/* ---- end diagnostic ---- */

	mov	x4, x1
	ldr	x5, _StackBase
	cmp	x4, x5
	beq	_Entry
	ldr	x6,	_StackSize

_CopyLoop:
	ldp	x2, x3, [x4], #16
	stp	x2, x3, [x5], #16
	subs	x6, x6, #16
	b.ne	_CopyLoop
	ldr	x5, _StackBase

_Entry:
	br	x5

_Dead:
	/* We should never get here */
	b	_Dead

.text
.align 4

_Payload:
	/* Your code will get ran right after this binary */
