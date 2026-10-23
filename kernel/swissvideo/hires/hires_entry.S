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
	.long	0

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
