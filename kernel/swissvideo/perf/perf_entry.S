/*
	Frame-time profiler payload: entry points (position independent).
	Nintendont addition.

	+0x00  GXCopyDisp hook (pf_frame in perf.c, then the original function)
	+0x04  offset of the performance-monitor interrupt handler
	+0x08  its size; perf.c copies it to 0x80000F00 at the first frame
	+0x0C  __VIRetraceHandler hook (runs the original, then pf_retrace,
	       which draws the overlay into the XFB the VI shows next)
*/
#ifndef _LANGUAGE_ASSEMBLY
#define _LANGUAGE_ASSEMBLY
#endif
#include "perf_shared.h"

	.section .text.entry, "ax"
	.globl _start
_start:
	b		pf_copydisp_hook
	.long	pf_pmi_handler - _start
	.long	pf_pmi_end - pf_pmi_handler
	b		pf_retrace_hook

/* GXCopyDisp(dest, clear): time the frame, then run the original */
pf_copydisp_hook:
	stwu	%r1, -32(%r1)
	mflr	%r0
	stw		%r0, 36(%r1)
	stw		%r3, 8(%r1)
	stw		%r4, 12(%r1)
	bl		pf_frame
	lwz		%r3, 8(%r1)
	lwz		%r4, 12(%r1)
	lwz		%r0, 36(%r1)
	mtlr	%r0
	addi	%r1, %r1, 32
	lis		%r12, PF_BLOCK@h
	ori		%r12, %r12, (PF_BLOCK + PF_TRAMP_COPYDISP)@l	/* first insn, b back */
	mtctr	%r12
	bctr

/* __VIRetraceHandler(irq, context): call it, then draw the overlay */
pf_retrace_hook:
	stwu	%r1, -32(%r1)
	mflr	%r0
	stw		%r0, 36(%r1)
	lis		%r12, PF_BLOCK@h
	ori		%r12, %r12, (PF_BLOCK + PF_TRAMP_RETRACE)@l
	mtctr	%r12
	bctrl
	bl		pf_retrace
	lwz		%r0, 36(%r1)
	mtlr	%r0
	addi	%r1, %r1, 32
	blr

/*
	Performance-monitor interrupt (vector 0x0F00), entered in real mode with
	external interrupts off. Saves r3-r5 and CR in real mode at PF_SCRATCH,
	turns data translation on to reach the counters (MEM2, uncached), the
	current thread (0x800000E4) and the CP status register, then reloads
	PMC1, re-arms MMCR0 and returns.
*/
	.globl pf_pmi_handler
pf_pmi_handler:
	stw		%r3, PF_SCRATCH + 0 (0)
	stw		%r4, PF_SCRATCH + 4 (0)
	stw		%r5, PF_SCRATCH + 8 (0)
	mfcr	%r3
	stw		%r3, PF_SCRATCH + 12 (0)
	mfmsr	%r3
	ori		%r3, %r3, 0x10			/* MSR[DR] */
	mtmsr	%r3
	isync

	lis		%r5, PF_DATA_HI
	lwz		%r3, PF_OFF_S_TOTAL (%r5)
	addi	%r3, %r3, 1
	stw		%r3, PF_OFF_S_TOTAL (%r5)

	lis		%r4, 0x8000
	lwz		%r3, 0xE4 (%r4)			/* __OSCurrentThread */
	cmpwi	%r3, 0
	bne		1f
	lwz		%r4, PF_OFF_S_IDLE (%r5)
	addi	%r4, %r4, 1
	stw		%r4, PF_OFF_S_IDLE (%r5)
	b		2f
1:	lwz		%r4, PF_OFF_MAIN_THREAD (%r5)
	cmpw	%r3, %r4
	bne		2f
	lwz		%r4, PF_OFF_S_MAIN (%r5)
	addi	%r4, %r4, 1
	stw		%r4, PF_OFF_S_MAIN (%r5)

2:	lis		%r4, 0xCC00
	lhz		%r3, 0 (%r4)			/* CP status */
	andi.	%r3, %r3, 0x0C			/* idle for reading | idle for commands */
	cmpwi	%r3, 0x0C
	beq		3f
	lwz		%r4, PF_OFF_S_GPU (%r5)
	addi	%r4, %r4, 1
	stw		%r4, PF_OFF_S_GPU (%r5)

3:	lwz		%r3, PF_OFF_RELOAD (%r5)
	mtspr	953, %r3			/* PMC1 */
	lwz		%r3, PF_OFF_MMCR0 (%r5)
	mtspr	952, %r3			/* MMCR0 (ENINT is cleared on the interrupt) */

	mfmsr	%r3
	rlwinm	%r3, %r3, 0, 28, 26		/* clear MSR[DR] */
	mtmsr	%r3
	isync
	lwz		%r3, PF_SCRATCH + 12 (0)
	mtcr	%r3
	lwz		%r5, PF_SCRATCH + 8 (0)
	lwz		%r4, PF_SCRATCH + 4 (0)
	lwz		%r3, PF_SCRATCH + 0 (0)
	rfi
	.globl pf_pmi_end
pf_pmi_end:
