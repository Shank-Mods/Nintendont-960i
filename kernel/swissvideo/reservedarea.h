/*
	Low memory variables shared between the Swiss-derived video patches
	(stubs/ *.S) and the kernel side patcher (SwissVideo.c).

	Offsets are relative to 0x80000000. They mirror the layout used by
	Swiss (cube/reservedarea.h), except VAR_RMODE, which Swiss keeps at
	0x800030F8; Nintendont keeps it at 0x800009DC (Swiss' VAR_EXI2_REGS,
	which Nintendont doesn't need), in the otherwise unused tail of the
	0x900 exception vector, so nothing above 0x80003000 is touched.

	Keep both sections in sync!
*/

#ifndef __SWISSVIDEO_RESERVEDAREA_H__
#define __SWISSVIDEO_RESERVEDAREA_H__

#ifdef _LANGUAGE_ASSEMBLY

.set VAR_AREA,				0x8000

.set VAR_TVMODE,			0x00CC	# TV format (Dolphin OS global)

.set VAR_RMODE,				0x09DC	# render mode (Swiss: 0x30F8)

.set VAR_FLOAT1_6,			0x09E0	# constant 1/6
.set VAR_FLOAT9_16,			0x09E4	# constant 9/16
.set VAR_FLOAT3_4,			0x09E8	# constant 3/4
.set VAR_FLOATM_1,			0x09EC	# constant -1
.set VAR_VFILTER_ON,		0x09F0	# vertical filter on
.set VAR_VFILTER,			0x09F1	# vertical filter
.set VAR_SAR_WIDTH,			0x09F8	# sample aspect ratio width
.set VAR_SAR_HEIGHT,		0x09FA	# sample aspect ratio height
.set VAR_NEXT_FIELD,		0x09FB	# next video field
.set VAR_CURRENT_FIELD,		0x09FC	# current video field

#else

#define VAR_AREA_ADDR		0x80000000

#define VAR_TVMODE			0x00CC
#define VAR_RMODE			0x09DC
#define VAR_FLOAT1_6		0x09E0
#define VAR_FLOAT9_16		0x09E4
#define VAR_FLOAT3_4		0x09E8
#define VAR_FLOATM_1		0x09EC
#define VAR_VFILTER_ON		0x09F0
#define VAR_VFILTER			0x09F1
#define VAR_SAR_WIDTH		0x09F8
#define VAR_SAR_HEIGHT		0x09FA
#define VAR_NEXT_FIELD		0x09FB
#define VAR_CURRENT_FIELD	0x09FC

/* first and last byte of the block owned by these patches */
#define VAR_BLOCK_START		0x09DC
#define VAR_BLOCK_END		0x0A00

#endif

#endif
