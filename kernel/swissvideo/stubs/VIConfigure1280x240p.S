/*
	1280x240p experiment (Nintendont addition, not part of Swiss).

	Used in place of Swiss' VIConfigure480p in the VIConfigure hook chain
	(it falls through into the stub installed before it, VIConfigureHook1).
	r3 = GXRenderModeObj passed to VIConfigure.

	The VI is set up as a 54 MHz progressive mode with Nintendont's
	1280x240p timing (240 lines, 263 line frame, 858 clock half-lines,
	see SwissVideo.c), fetching 1280 pixel wide framebuffer lines:
	  fbWidth   = 1280  (the VI only uses it for the fetch width)
	  viWidth   = 640, viXOrigin = 80 (with the timing's hbe640/hbs640 this
	              gives a 1280 clock wide active window)
	  efbHeight = xfbHeight = viHeight = 240, viYOrigin = 0
	The game's GX calls are adjusted separately by the hi-res payload, so it
	doesn't matter whether the game reads these values back or not.
*/
#include "reservedarea.h"

.globl VIConfigure1280x240p
VIConfigure1280x240p:
	lhz			%r0, 2 (%r3)
	srwi		%r5, %r0, 2
	cmpwi		%r5, 1
	beq			1f
	cmpwi		%r5, 4
	bne			2f
1:	subi		%r5, %r5, 1
	insrwi		%r0, %r5, 14, 16
2:	lis			%r4, VAR_AREA
	stw			%r5, VAR_TVMODE (%r4)
	insrwi		%r0, %r5, 14, 0
	oris		%r0, %r0, 2
	li			%r6, 1280
	sth			%r6, 4 (%r3)
	li			%r6, 240
	sth			%r6, 6 (%r3)
	sth			%r6, 8 (%r3)
	sth			%r6, 16 (%r3)
	li			%r6, 80
	sth			%r6, 10 (%r3)
	li			%r6, 0
	sth			%r6, 12 (%r3)
	li			%r6, 640
	sth			%r6, 14 (%r3)
	li			%r7, 0
	stb			%r7, 22 (%r3)
	li			%r8, 1
	stb			%r8, VAR_VFILTER_ON (%r4)
	stw			%r0, 0 (%r3)
