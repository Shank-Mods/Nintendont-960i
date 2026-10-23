/*
	1280x240p two-pass experiment: payload entry points (position independent).
	Nintendont addition, not part of Swiss.

	+0x00  GXSetViewport hook (here, floats in f1-f6)
	+0x04  GXSetScissor hook  (hr_scissor in hires.c)
	+0x08  GXCopyDisp hook    (hr_copydisp in hires.c)

	Each hook replaces the first instruction of the SDK function with a
	branch here; the trampolines in struct hr_data run that instruction and
	branch back to the function.
*/
#ifndef _LANGUAGE_ASSEMBLY
#define _LANGUAGE_ASSEMBLY
#endif
#include "hires_shared.h"

	.section .text.entry, "ax"
	.globl _start
_start:
	b		hr_viewport_hook
	b		hr_scissor
	b		hr_copydisp
	b		hr_calldl

/*
	GXSetViewport(left, top, wd, ht, nearz, farz): a full screen 640x480
	viewport becomes 1280x240 (left/wd doubled, top/ht halved). The
	projection is left alone, so the picture covers the same screen area.
*/
hr_viewport_hook:
	lis		%r11, HR_STATS_PPC@h
	stfs	%f3, ((HR_STATS_PPC & 0xFFFF) + 0x50) (%r11)	/* vp_w */
	stfs	%f4, ((HR_STATS_PPC & 0xFFFF) + 0x54) (%r11)	/* vp_h */
	stwu	%r1, -16 (%r1)
	lis		%r0, 0x4420				/* 640.0f */
	stw		%r0, 8 (%r1)
	lfs		%f0, 8 (%r1)
	fcmpu	%cr0, %f3, %f0
	bne		9f
	lis		%r0, 0x43F0				/* 480.0f */
	stw		%r0, 8 (%r1)
	lfs		%f0, 8 (%r1)
	fcmpu	%cr0, %f4, %f0
	bne		9f
	fadds	%f1, %f1, %f1
	fadds	%f3, %f3, %f3
	lis		%r0, 0x3F00				/* 0.5f */
	stw		%r0, 8 (%r1)
	lfs		%f0, 8 (%r1)
	fmuls	%f2, %f2, %f0
	fmuls	%f4, %f4, %f0
9:	addi	%r1, %r1, 16
	lis		%r12, 0x8000
	lwz		%r12, HR_DATA_PTR_VAR (%r12)
	addi	%r12, %r12, 4			/* hr_data.tramp_vp */
	mtctr	%r12
	bctr

/*
	gcc -Os epilogue helper (libgcc crtsavres, 32 bit SVR4): r11 = caller's
	stack pointer; restores r14..r31 from the save area below it, returns.
*/
	.globl _restgpr_14_x
	.globl _restgpr_15_x
	.globl _restgpr_16_x
	.globl _restgpr_17_x
	.globl _restgpr_18_x
	.globl _restgpr_19_x
	.globl _restgpr_20_x
	.globl _restgpr_21_x
	.globl _restgpr_22_x
	.globl _restgpr_23_x
	.globl _restgpr_24_x
	.globl _restgpr_25_x
	.globl _restgpr_26_x
	.globl _restgpr_27_x
	.globl _restgpr_28_x
	.globl _restgpr_29_x
	.globl _restgpr_30_x
	.globl _restgpr_31_x
_restgpr_14_x:
	lwz		14, -72(11)
_restgpr_15_x:
	lwz		15, -68(11)
_restgpr_16_x:
	lwz		16, -64(11)
_restgpr_17_x:
	lwz		17, -60(11)
_restgpr_18_x:
	lwz		18, -56(11)
_restgpr_19_x:
	lwz		19, -52(11)
_restgpr_20_x:
	lwz		20, -48(11)
_restgpr_21_x:
	lwz		21, -44(11)
_restgpr_22_x:
	lwz		22, -40(11)
_restgpr_23_x:
	lwz		23, -36(11)
_restgpr_24_x:
	lwz		24, -32(11)
_restgpr_25_x:
	lwz		25, -28(11)
_restgpr_26_x:
	lwz		26, -24(11)
_restgpr_27_x:
	lwz		27, -20(11)
_restgpr_28_x:
	lwz		28, -16(11)
_restgpr_29_x:
	lwz		29, -12(11)
_restgpr_30_x:
	lwz		30, -8(11)
_restgpr_31_x:
	lwz		0, 4(11)
	lwz		31, -4(11)
	mtlr	0
	mr		1, 11
	blr
