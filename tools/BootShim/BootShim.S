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
	/* ---- DEFINITIVE DIAGNOSTIC (framebuffer-independent): force a hardware
	   reboot via the PSHOLD register (0x0440B000) as the very first action.
	   If the device enters a reboot loop, the ABL DID launch our kernel and
	   the silent-boot problem is downstream (UEFI/framebuffer). If it stays
	   frozen on the Samsung splash, our code never runs = a boot.img/ABL
	   handoff problem. MMU is off here, so this is a direct physical write. */
	movz	x10, #0xB000
	movk	x10, #0x0440, lsl #16     /* x10 = 0x0440B000 (PSHOLD) */
	str	wzr, [x10]                /* write 0 -> PMIC reset */
	dsb	sy
	isb
_RebootWait:
	wfi
	b	_RebootWait
	/* ---- end diagnostic (original relocation below, now unreachable) ---- */

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
