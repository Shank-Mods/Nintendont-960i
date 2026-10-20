/*
Nintendont (Kernel) - Swiss video patch engine (field rendering / 960i)

The signatures, patch logic and PowerPC stubs used here come from Swiss
(https://github.com/emukidid/swiss-gc, cube/swiss/source/patcher.c), which
can force GameCube games to use field rendering and output 960i/1152i.
Patch_Video() is imported mechanically by swissvideo/port_patch_video.py,
this file only provides the environment it expects:

  - Swiss' pattern matching helpers (copied verbatim),
  - address translation for a DOL that is already loaded in place,
  - an installer for the hook stubs that uses Nintendont's patch area,
  - the Swiss settings that select "960i + force field rendering",
  - accessors for the few low memory variables shared with the stubs.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <stdarg.h>
#ifndef SWISSVIDEO_HOST_TEST
#include "global.h"
#include "string.h"
#include "debug.h"
#include "Config.h"
#include "syscalls.h"
#include "vsprintf.h"
#include "ff_utf8.h"
#else
#include "swissvideo/test/host_env.h"
#endif
#include "SwissVideo.h"
#include "swissvideo/reservedarea.h"
#include "swissvideo/stubs.h"

/* From Patch.c */
extern u32 PatchCopy(const u8 *PatchPtr, const u32 PatchSize);

#ifndef SWISSVIDEO_HOST_TEST
static void swv_log(const char *fmt, ...);
#endif

/*
	Where MEM1 (0x80000000) is mapped for the code running this patcher.
	The kernel sees MEM1 at physical address 0, the host test harness
	(swissvideo/test) maps it into a buffer.
*/
#ifndef MEM1_BASE
#define MEM1_BASE 0
#endif

/* ------------------------------------------------------------------------ */
/* Swiss environment                                                        */
/* ------------------------------------------------------------------------ */

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef float f32;

#define countof(a) (sizeof(a) / sizeof((a)[0]))
#define in_range(x, a, b) (((x) >= (a)) && ((x) <= (b)))

/* Swiss' FuncPattern (differs from Nintendont's after the first 6 fields) */
typedef struct SwissFuncPattern
{
	u32 Length;
	u32 Loads;
	u32 Stores;
	u32 FCalls;
	u32 Branch;
	u32 Moves;
	const void *Patch;
	u32 PatchLength;
	const char *Name;
	u32 offsetFoundAt;
	u32 *properAddress;
} FuncPattern;

/* Swiss executable types, the image here is always a flat memory image */
enum {
	PATCH_DOL,
	PATCH_DOL_PRS,
	PATCH_ELF,
	PATCH_APPLOADER,
	PATCH_BS2,
	PATCH_EXEC,
	PATCH_BIN,
	PATCH_OTHER
};

enum aveCompat {
	AVE_N_DOL_COMPAT = 0,
	AVE_P_DOL_COMPAT,
	CMPV_DOL_COMPAT,
	GCDIGITAL_COMPAT,
	GCVIDEO_COMPAT,
	AVE_RVL_COMPAT,
	AVE_COMPAT_MAX
};

#ifndef SWISSVIDEO_AVE_COMPAT
#define SWISSVIDEO_AVE_COMPAT AVE_RVL_COMPAT
#endif

#define SYS_VIDEO_NTSC	0
#define SYS_VIDEO_PAL	1
#define SYS_VIDEO_MPAL	2

#define VI_TVMODE_NTSC_PROG	2	/* (VI_NTSC << 2) | VI_PROGRESSIVE */
#define VI_TVMODE_PAL_PROG	6	/* (VI_PAL << 2)  | VI_PROGRESSIVE */

/* The subset of Swiss' settings that Patch_Video() reads. */
static struct {
	int gameVMode;
	int forceHScale;
	int forceVOffset;
	int forceVFilter;
	int forceVJitter;
	int fixPixelCenter;
	int disableDithering;
	int aveCompat;
	int rt4kOptim;
	int forceDTVStatus;
	int forceVideoActive;
	int wiirdEngine;
	int sramVideo;
	int sram60Hz;
	int sramProgressive;
} swissSettings;

/* Swiss' newmode is the GXRModeObj it initialised the VI with before boot */
typedef struct { u32 viTVMode; } SwissRModeObj;
static SwissRModeObj swiss_newmode;
static SwissRModeObj *const newmode = &swiss_newmode;

/* Only used by the disabled Swiss cheat engine hook */
#define CHEATS_ENGINE_START ((u32 *)NULL)

/* Image currently being patched */
static u32 swv_base;	/* physical address of data[0] */
static u32 swv_length;	/* in bytes */

/* Physical (ARM side) view of a 0x80000000 based address */
#define P2C_PTR(p) ((u32 *)(((u32)(p) & 0x01FFFFFF) + MEM1_BASE))

static float truncf(float x)
{
	/* only used with fixPixelCenter, values are small and finite */
	return (float)(s32)x;
}

/* ------------------------------------------------------------------------ */
/* Low memory variables shared with the stubs (see reservedarea.h)           */
/* ------------------------------------------------------------------------ */

#ifndef SWISSVIDEO_HOST_TEST
static inline void var_write8(u32 off, u8 val)
{
	__asm__ volatile ("strb\t%0, [%1]" : : "l" (val), "l" (off));
}

static inline void var_write16(u32 off, u16 val)
{
	__asm__ volatile ("strh\t%0, [%1]" : : "l" (val), "l" (off));
}
#else
static inline void var_write8(u32 off, u8 val)
{
	*(volatile u8 *)(MEM1_BASE + off) = val;
}

static inline void var_write16(u32 off, u16 val)
{
	*(volatile u16 *)(MEM1_BASE + off) = val;
}
#endif

static void var_memcpy(u32 off, const u8 *src, u32 len)
{
	u32 i;
	for (i = 0; i < len; i++)
		var_write8(off + i, src[i]);
}

/* ------------------------------------------------------------------------ */
/* Address translation                                                      */
/* ------------------------------------------------------------------------ */

/*
	Swiss patches executables before they are loaded, so it has to map
	file offsets to load addresses. Nintendont patches the executable after
	it has been loaded, data[] is the image at its final location.
*/
static void *Calc_ProperAddress(void *data, int dataType, u32 offsetFoundAt)
{
	(void)dataType;
	if ((u32)data != swv_base || offsetFoundAt >= swv_length)
		return NULL;
	return (void *)(0x80000000 | (swv_base - MEM1_BASE + offsetFoundAt));
}

static void *Calc_Address(void *data, int dataType, u32 properAddress)
{
	(void)dataType;
	u32 phys = (properAddress & ~0xC0000000) + MEM1_BASE;
	if ((u32)data != swv_base || (properAddress & 0xC0000000) != 0x80000000)
		return NULL;
	if (phys < swv_base || phys >= swv_base + swv_length)
		return NULL;
	return (void *)phys;
}

/* ------------------------------------------------------------------------ */
/* Patch installer                                                          */
/* ------------------------------------------------------------------------ */

enum patchIds {
	GX_COPYDISPHOOK,
	VI_CONFIGURE240P,
	VI_CONFIGURE288P,
	VI_CONFIGURE480I,
	VI_CONFIGURE480P,
	VI_CONFIGURE540P50,
	VI_CONFIGURE540P60,
	VI_CONFIGURE576I,
	VI_CONFIGURE576P,
	VI_CONFIGURE960I,
	VI_CONFIGURE1080I50,
	VI_CONFIGURE1080I60,
	VI_CONFIGURE1152I,
	VI_CONFIGUREAUTOP,
	VI_CONFIGUREFIELDMODE,
	VI_CONFIGUREHOOK1,
	VI_CONFIGUREHOOK1_GCVIDEO,
	VI_CONFIGUREHOOK1_RT4K,
	VI_CONFIGUREHOOK2,
	VI_CONFIGURENOYSCALE,
	VI_CONFIGUREPANHOOK,
	VI_CONFIGUREPANHOOKD,
	VI_GETRETRACECOUNTHOOK,
	VI_RETRACEHANDLERHOOK,
	PATCHES_MAX
};

static const struct {
	const unsigned char *patch;
	u32 size;
} patch_table[PATCHES_MAX] = {
	[GX_COPYDISPHOOK]           = { GXCopyDispHook,          GXCopyDispHook_size },
	[VI_CONFIGURE240P]          = { VIConfigure240p,         VIConfigure240p_size },
	[VI_CONFIGURE288P]          = { VIConfigure288p,         VIConfigure288p_size },
	[VI_CONFIGURE480I]          = { VIConfigure480i,         VIConfigure480i_size },
	[VI_CONFIGURE480P]          = { VIConfigure480p,         VIConfigure480p_size },
	[VI_CONFIGURE540P50]        = { VIConfigure540p50,       VIConfigure540p50_size },
	[VI_CONFIGURE540P60]        = { VIConfigure540p60,       VIConfigure540p60_size },
	[VI_CONFIGURE576I]          = { VIConfigure576i,         VIConfigure576i_size },
	[VI_CONFIGURE576P]          = { VIConfigure576p,         VIConfigure576p_size },
	[VI_CONFIGURE960I]          = { VIConfigure960i,         VIConfigure960i_size },
	[VI_CONFIGURE1080I50]       = { VIConfigure1080i50,      VIConfigure1080i50_size },
	[VI_CONFIGURE1080I60]       = { VIConfigure1080i60,      VIConfigure1080i60_size },
	[VI_CONFIGURE1152I]         = { VIConfigure1152i,        VIConfigure1152i_size },
	[VI_CONFIGUREAUTOP]         = { VIConfigureAutop,        VIConfigureAutop_size },
	[VI_CONFIGUREFIELDMODE]     = { VIConfigureFieldMode,    VIConfigureFieldMode_size },
	[VI_CONFIGUREHOOK1]         = { VIConfigureHook1,        VIConfigureHook1_size },
	[VI_CONFIGUREHOOK1_GCVIDEO] = { VIConfigureHook1GCVideo, VIConfigureHook1GCVideo_size },
	[VI_CONFIGUREHOOK1_RT4K]    = { VIConfigureHook1RT4K,    VIConfigureHook1RT4K_size },
	[VI_CONFIGUREHOOK2]         = { VIConfigureHook2,        VIConfigureHook2_size },
	[VI_CONFIGURENOYSCALE]      = { VIConfigureNoYScale,     VIConfigureNoYScale_size },
	[VI_CONFIGUREPANHOOK]       = { VIConfigurePanHook,      VIConfigurePanHook_size },
	[VI_CONFIGUREPANHOOKD]      = { VIConfigurePanHookD,     VIConfigurePanHookD_size },
	[VI_GETRETRACECOUNTHOOK]    = { VIGetRetraceCountHook,   VIGetRetraceCountHook_size },
	[VI_RETRACEHANDLERHOOK]     = { VIRetraceHandlerHook,    VIRetraceHandlerHook_size },
};

static void *patch_locations[PATCHES_MAX];

static void swv_reset_patch_locations(void)
{
	memset(patch_locations, 0, sizeof(patch_locations));
}

/*
	Like Swiss, every new patch is placed directly below the previous one.
	The VIConfigure mode stubs (VIConfigure960i, VIConfigureFieldMode) have
	no blr and fall through into the patch that was installed right before
	them, which is how Swiss chains them in front of VIConfigureHook1.
*/
static void *getPatchAddr(int patchId)
{
	if (patchId < 0 || patchId >= PATCHES_MAX)
		return NULL;
	if (!patch_locations[patchId]) {
		u32 phys = PatchCopy(patch_table[patchId].patch, patch_table[patchId].size);
		patch_locations[patchId] = (void *)(0x80000000 | (phys - MEM1_BASE));
		swv_log("SwissVideo:Installed patch %i to %08X\r\n", patchId, patch_locations[patchId]);
	}
	return patch_locations[patchId];
}

static void swv_reset_sigs(FuncPattern *sigs, u32 count)
{
	u32 i;
	for (i = 0; i < count; i++) {
		sigs[i].offsetFoundAt = 0;
		sigs[i].properAddress = NULL;
	}
}

/* ------------------------------------------------------------------------ */
/* Swiss pattern helpers (verbatim from Swiss patcher.c)                    */
/* ------------------------------------------------------------------------ */

static void make_pattern(u32 *data, int dataType, u32 offsetFoundAt, u32 length, FuncPattern *functionPattern)
{
	u32 i, j;

	memset(functionPattern, 0, sizeof(FuncPattern));

	for (j = i = offsetFoundAt; j >= offsetFoundAt && i < length / sizeof(u32); i++) {
		u32 word = data[i];

		functionPattern->Length++;

		if ((word == 0x4E800020 && j <= i) || word == 0x4C000064)
			break;
		if ((word & 0xFC000003) == 0x40000000)
			j = i + ((s32)((word & 0x0000FFFC) << 16) >> 18);
		if ((word & 0xFC000003) == 0x48000000)
			j = i + ((s32)((word & 0x03FFFFFC) << 6) >> 8);

		if ((word & 0xFF000000) == 0x38000000 ||
			(word & 0xFF000000) == 0x3C000000 ||
			(word & 0xFC000000) == 0x80000000 ||
			(word & 0xFC000000) == 0xC0000000)
			functionPattern->Loads++;

		if ((word & 0xFC000000) == 0x90000000 ||
			(word & 0xFC000000) == 0x94000000 ||
			(word & 0xFC000000) == 0xD0000000)
			functionPattern->Stores++;

		if ((word & 0xFC000003) == 0x48000001)
			functionPattern->FCalls++;

		if ((word & 0xFFFF0000) == 0x40800000 ||
			(word & 0xFFFF0000) == 0x40810000 ||
			(word & 0xFFFF0000) == 0x41800000 ||
			(word & 0xFFFF0000) == 0x41820000 ||
			(word & 0xFC000003) == 0x48000000)
			functionPattern->Branch++;

		if ((word & 0xFF000000) == 0x7C000000 ||
			(word & 0xFC0007FE) == 0xFC000090)
			functionPattern->Moves++;
	}

	functionPattern->offsetFoundAt = offsetFoundAt;
	functionPattern->properAddress = Calc_ProperAddress(data, dataType, offsetFoundAt * sizeof(u32));
}

static bool compare_pattern(FuncPattern *FPatA, FuncPattern *FPatB)
{
	return memcmp(FPatA, FPatB, sizeof(u32) * 6) == 0;
}

static bool find_pattern(u32 *data, int dataType, u32 offsetFoundAt, u32 length, FuncPattern *functionPattern)
{
	FuncPattern FP;

	make_pattern(data, dataType, offsetFoundAt, length, &FP);

	if (functionPattern && compare_pattern(&FP, functionPattern)) {
		functionPattern->offsetFoundAt = FP.offsetFoundAt;
		functionPattern->properAddress = FP.properAddress;
		return true;
	}

	return false;
}

static bool find_pattern_before(u32 *data, int dataType, u32 length, FuncPattern *FPatA, FuncPattern *FPatB)
{
	u32 offsetFoundAt = FPatA->offsetFoundAt - FPatB->Length;

	if (offsetFoundAt == FPatB->offsetFoundAt)
		return true;

	if (offsetFoundAt >= length / sizeof(u32))
		return false;

	while (offsetFoundAt &&
		data[offsetFoundAt - 1] != 0x4E800020 &&
		data[offsetFoundAt - 1] != 0x4C000064)
		offsetFoundAt--;

	return find_pattern(data, dataType, offsetFoundAt, length, FPatB);
}

static bool find_pattern_after(u32 *data, int dataType, u32 length, FuncPattern *FPatA, FuncPattern *FPatB)
{
	u32 offsetFoundAt = FPatA->offsetFoundAt + FPatA->Length;

	if (offsetFoundAt == FPatB->offsetFoundAt)
		return true;

	if (offsetFoundAt >= length / sizeof(u32))
		return false;

	return find_pattern(data, dataType, offsetFoundAt, length, FPatB);
}

static u32 branch(u32 *dst, u32 *src)
{
	u32 word = (u32)dst - (u32)src;
	word &= 0x03FFFFFC;
	word |= 0x48000000;
	return word;
}

static u32 branchAndLink(u32 *dst, u32 *src)
{
	u32 word = (u32)dst - (u32)src;
	word &= 0x03FFFFFC;
	word |= 0x48000001;
	return word;
}

static u32 branchResolve(u32 *data, int dataType, u32 offsetFoundAt)
{
	u32 *address;
	u32 word = data[offsetFoundAt];

	if ((word & 0xFC000000) != 0x48000000)
		return 0;

	if ((address = Calc_ProperAddress(data, dataType, offsetFoundAt * sizeof(u32))) == NULL)
		return 0;

	address = ((s32)((word & 0x03FFFFFC) << 6) >> 8) + (word & 0x2 ? NULL : address);

	if ((address = Calc_Address(data, dataType, (u32)address)) == NULL)
		return 0;

	return address - data;
}

static u32 _SDA2_BASE_, _SDA_BASE_;

static bool get_immediate(u32 *data, u32 offsetFoundAt, u32 offsetFoundAt2, u32 *immediate)
{
	u32 word = data[offsetFoundAt];

	switch (word >> 26) {
		case 15:
		{
			u32 word2 = data[offsetFoundAt2];

			switch (word2 >> 26) {
				case 10:
				{
					if (((word2 >> 16) & 0x1F) != ((word >> 21) & 0x1F))
						return false;

					*immediate = -(word << 16) + (u16)word2;
					return true;
				}
				case 11:
				{
					if (((word2 >> 16) & 0x1F) != ((word >> 21) & 0x1F))
						return false;

					*immediate = -(word << 16) + (s16)word2;
					return true;
				}
			}

			if (((word >> 16) & 0x1F) != 0)
				return false;

			switch (word2 >> 26) {
				case 14:
				case 32 ... 55:
				{
					if (((word2 >> 16) & 0x1F) != ((word >> 21) & 0x1F))
						return false;

					*immediate = (word << 16) + (s16)word2;
					return true;
				}
				case 24:
				{
					if (((word2 >> 21) & 0x1F) != ((word >> 21) & 0x1F))
						return false;

					*immediate = (word << 16) | (u16)word2;
					return true;
				}
			}
		}
		/* fall through */
		case 14:
		case 32 ... 55:
		{
			switch (offsetFoundAt2) {
				case 0:
				{
					if (((word >> 16) & 0x1F) != 0)
						return false;

					*immediate = (s16)word;
					return true;
				}
				case 2:
				{
					if (((word >> 16) & 0x1F) != 2)
						return false;

					*immediate = _SDA2_BASE_ + (s16)word;
					return true;
				}
				case 13:
				{
					if (((word >> 16) & 0x1F) != 13)
						return false;

					*immediate = _SDA_BASE_ + (s16)word;
					return true;
				}
			}
		}
	}

	return false;
}

static void *loadResolve(u32 *data, int dataType, u32 offsetFoundAt, u32 offsetFoundAt2)
{
	u32 address;

	if (get_immediate(data, offsetFoundAt, offsetFoundAt2, &address))
		return Calc_Address(data, dataType, address);

	return NULL;
}

static bool findx_pattern(u32 *data, int dataType, u32 offsetFoundAt, u32 length, FuncPattern *functionPattern)
{
	offsetFoundAt = branchResolve(data, dataType, offsetFoundAt);

	if (functionPattern && functionPattern->offsetFoundAt)
		return offsetFoundAt == functionPattern->offsetFoundAt;

	return offsetFoundAt && find_pattern(data, dataType, offsetFoundAt, length, functionPattern);
}

static bool findx_patterns(u32 *data, int dataType, u32 offsetFoundAt, u32 length, ...)
{
	FuncPattern *functionPattern;

	va_list args;
	va_start(args, length);

	while ((functionPattern = va_arg(args, FuncPattern *)))
		if (findx_pattern(data, dataType, offsetFoundAt, length, functionPattern))
			break;

	va_end(args);

	return functionPattern != NULL;
}

/* ------------------------------------------------------------------------ */
/* 960i diagnostics log (written to /nin960i.log)                           */
/* ------------------------------------------------------------------------ */

#ifndef SWISSVIDEO_HOST_TEST
static char swv_logbuf[12 * 1024];
static u32 swv_loglen = 0;
/* Only echo to Nintendont's own log while patching. During gameplay its
   file writes can collide with disc reads from the same device. */
static u32 swv_echo = 0;

static void swv_log(const char *fmt, ...)
{
	char line[0x100];
	va_list args;
	u32 len;

	va_start(args, fmt);
	len = _vsprintf(line, fmt, args);
	va_end(args);

	if (swv_loglen + len < sizeof(swv_logbuf)) {
		memcpy(swv_logbuf + swv_loglen, line, len);
		swv_loglen += len;
	}
	if (swv_echo)
		dbgprintf("%s", line);
}
#else
#define swv_log printf
#endif

/* ------------------------------------------------------------------------ */
/* Imported Swiss code                                                      */
/* ------------------------------------------------------------------------ */

/* Swiss' "Found:[...]" messages also go to the 960i log */
#undef dbgprintf
#define dbgprintf swv_log

#include "swissvideo/video_tables.inc"
#include "swissvideo/Patch_Video.inc"

static void SwissVideoClearSnapshot(void);

/* ------------------------------------------------------------------------ */
/* Nintendont interface                                                     */
/* ------------------------------------------------------------------------ */

/*
	960i experiments: override fields of the 960i/1152i timing table from
	/nin960i_vi.txt on the boot device. One "name=value" (decimal) per line,
	# starts a comment. Field names and offsets follow the SDK/libogc
	timing struct (see video_timing_960i):
*/
static const struct {
	const char *name;
	u8 offset;
	u8 size;
} swv_timing_fields[] = {
	{ "equ",      0, 1 }, { "acv",      2, 2 },
	{ "prbodd",   4, 2 }, { "prbeven",  6, 2 },
	{ "psbodd",   8, 2 }, { "psbeven", 10, 2 },
	{ "bs1",     12, 1 }, { "bs2",     13, 1 }, { "bs3", 14, 1 }, { "bs4", 15, 1 },
	{ "be1",     16, 2 }, { "be2",     18, 2 }, { "be3", 20, 2 }, { "be4", 22, 2 },
	{ "nhlines", 24, 2 }, { "hlw",     26, 2 },
	{ "hsy",     28, 1 }, { "hcs",     29, 1 }, { "hce", 30, 1 }, { "hbe640", 31, 1 },
	{ "hbs640",  32, 2 },
};

static u32 swv_get(const u8 *t, u32 i)
{
	const u8 *p = t + swv_timing_fields[i].offset;
	return swv_timing_fields[i].size == 1 ? p[0] : ((u32)p[0] << 8) | p[1];
}

static void swv_set(u8 *t, u32 i, u32 v)
{
	u8 *p = t + swv_timing_fields[i].offset;
	if (swv_timing_fields[i].size == 1)
		p[0] = v;
	else {
		p[0] = v >> 8;
		p[1] = v;
	}
}

static void swv_log_timing(const char *what, const u8 *t)
{
	u32 equ = t[0], acv = swv_get(t, 1);
	swv_log("SwissVideo:%s equ %u acv %u prb %u/%u psb %u/%u nhlines %u hlw %u\r\n", what,
		equ, acv, swv_get(t, 2), swv_get(t, 3), swv_get(t, 4), swv_get(t, 5),
		swv_get(t, 14), swv_get(t, 15));
	swv_log("SwissVideo:%s frame length odd %u / even %u half-lines\r\n", what,
		3 * equ + swv_get(t, 2) + 2 * acv + swv_get(t, 4),
		3 * equ + swv_get(t, 3) + 2 * acv + swv_get(t, 5));
}

/* Settings read from /nin960i_vi.txt (-1 = not set) */
static s32 swv_ovr_vmode, swv_ovr_vjitter, swv_ovr_vfilter, swv_ovr_avecompat;
static s32 swv_ovr_swiss;	/* 0 = don't apply the Swiss patches (baseline) */
static s32 swv_ovr_timing[countof(swv_timing_fields)];

static void swv_apply_setting(const char *name, u32 n)
{
	u32 j;

	if (strcmp(name, "gamevmode") == 0)        swv_ovr_vmode = n;
	else if (strcmp(name, "swiss") == 0)        swv_ovr_swiss = n;
	else if (strcmp(name, "forcevjitter") == 0) swv_ovr_vjitter = n;
	else if (strcmp(name, "forcevfilter") == 0) swv_ovr_vfilter = n;
	else if (strcmp(name, "avecompat") == 0)    swv_ovr_avecompat = n;
	else {
		for (j = 0; j < countof(swv_timing_fields); j++) {
			if (strcmp(name, swv_timing_fields[j].name) == 0) {
				swv_ovr_timing[j] = n;
				break;
			}
		}
		if (j == countof(swv_timing_fields)) {
			swv_log("SwissVideo:unknown setting %s\r\n", name);
			return;
		}
	}
	swv_log("SwissVideo:setting %s = %u\r\n", name, n);
}

#ifndef SWISSVIDEO_HOST_TEST
static void SwissVideoReadSettings(void)
{
	char buf[1024], name[16];
	FIL f;
	UINT read = 0;
	u32 i, n;

	swv_ovr_vmode = swv_ovr_vjitter = swv_ovr_vfilter = swv_ovr_avecompat = swv_ovr_swiss = -1;
	for (i = 0; i < countof(swv_ovr_timing); i++)
		swv_ovr_timing[i] = -1;

	if (f_open_char(&f, "/nin960i_vi.txt", FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return;
	f_read(&f, buf, sizeof(buf) - 1, &read);
	f_close(&f);
	buf[read] = 0;

	for (i = 0; i < read; ) {
		/* one line: name=value (decimal) */
		while (i < read && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n'))
			i++;
		if (i >= read)
			break;
		if (buf[i] == '#') {
			while (i < read && buf[i] != '\n') i++;
			continue;
		}
		n = 0;
		while (i < read && buf[i] != '=' && buf[i] != '\n' && n < sizeof(name) - 1) {
			char c = buf[i++];
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if (c != ' ') name[n++] = c;
		}
		name[n] = 0;
		if (i >= read || buf[i] != '=') {
			while (i < read && buf[i] != '\n') i++;
			continue;
		}
		i++;
		n = 0;
		while (i < read && buf[i] >= '0' && buf[i] <= '9')
			n = n * 10 + (buf[i++] - '0');
		while (i < read && buf[i] != '\n') i++;
		swv_apply_setting(name, n);
	}
}
#else
static void SwissVideoReadSettings(void)
{
	u32 i;
	swv_ovr_vmode = swv_ovr_vjitter = swv_ovr_vfilter = swv_ovr_avecompat = swv_ovr_swiss = -1;
	for (i = 0; i < countof(swv_ovr_timing); i++)
		swv_ovr_timing[i] = -1;
}
#endif

static void SwissVideoTimingOverrides(void)
{
	u8 *t;
	u32 j, any = 0;

	if (swissSettings.gameVMode == 4)
		t = video_timing_960i;
	else if (swissSettings.gameVMode == 11)
		t = video_timing_1152i;
	else
		return;

	swv_log_timing("default", t);
	for (j = 0; j < countof(swv_timing_fields); j++) {
		if (swv_ovr_timing[j] >= 0) {
			swv_set(t, j, swv_ovr_timing[j]);
			any = 1;
		}
	}
	if (any)
		swv_log_timing("override", t);
}

/* Games Swiss forces to the 50Hz variant of the selected mode (960i -> 1152i) */
static const char gameVModePalEntries[][4] = {
	"DLSP", "G3FD", "G3FF", "G3FP", "G3FS", "GLRD", "GLRF", "GLRP",
	"GM8P", "GSWD", "GSWF", "GSWI", "GSWP", "GSWS"
};

bool SwissVideoEnabled(void)
{
	if (!ConfigGetConfig(NIN_CFG_960I))
		return false;
	/* The Wii U only scales the video signal, it can not output 960i. */
	if (IsWiiU())
		return false;
	/*
		960i uses progressive (31kHz) timings, like Swiss this requires
		an EDTV capable output (component cable with progressive enabled).
	*/
	if (!(ConfigGetVideoMode() & NIN_VID_PROG))
		return false;
	return true;
}

/*
	Mirrors Swiss' config_load_current() for "Video Mode: 960i" with all
	other per game video options left on Auto, on an AVE-RVL (Wii) system.
*/
static void SwissVideoSetup(void)
{
	u32 GameID = ConfigGetGameID();
	char game_id[4];
	u32 i;

	game_id[0] = GameID >> 24;
	game_id[1] = GameID >> 16;
	game_id[2] = GameID >> 8;
	game_id[3] = GameID;

	memset(&swissSettings, 0, sizeof(swissSettings));
	/*
		Swiss' "AVE Compatibility" setting. Every mode except AVE-RVL also
		sets the VI's non-interlaced (NIN) bit for 960i. AVE-RVL is the one
		that gives a picture on the Wii (the others give a black screen).
	*/
	swissSettings.aveCompat = SWISSVIDEO_AVE_COMPAT;
	swissSettings.sramVideo = (BI2region == BI2_REGION_PAL) ? SYS_VIDEO_PAL : SYS_VIDEO_NTSC;

	/* Swiss "Video Mode": 4 = 960i by default, see gameVModeStr in Swiss */
	swissSettings.gameVMode = swv_ovr_vmode >= 0 ? swv_ovr_vmode : 4;

	/* From here on this mirrors Swiss' config_load_current() */
	swissSettings.sram60Hz = in_range(swissSettings.gameVMode, 1, 7);
	swissSettings.sramProgressive = in_range(swissSettings.gameVMode, 4, 7) || in_range(swissSettings.gameVMode, 11, 14);

	if (swissSettings.sram60Hz) {
		for (i = 0; i < countof(gameVModePalEntries); i++) {
			if (memcmp(game_id, gameVModePalEntries[i], 4) == 0) {
				swissSettings.gameVMode += 7;	/* e.g. 960i -> 1152i */
				break;
			}
		}
	}

	if (swissSettings.sramVideo == SYS_VIDEO_PAL)
		swissSettings.sramProgressive &= swissSettings.sram60Hz;
	else
		swissSettings.sram60Hz = 0;

	if (swv_ovr_vjitter >= 0) swissSettings.forceVJitter = swv_ovr_vjitter;
	if (swv_ovr_vfilter >= 0) swissSettings.forceVFilter = swv_ovr_vfilter;
	if (swv_ovr_avecompat >= 0) swissSettings.aveCompat = swv_ovr_avecompat;

	/* "Auto" defaults Swiss applies per mode */
	switch (swissSettings.gameVMode) {
		case 2: case 9: case 5: case 12:
			if (!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			break;
		case 3: case 10:
			swissSettings.forceVOffset &= ~1;
			if (!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if (!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 2;
			break;
		case 4: case 11:
			swissSettings.forceVOffset &= ~1;
			if (!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if (!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 1;	/* force field rendering */
			break;
		case 6: case 13:
			if (!swissSettings.forceHScale)
				swissSettings.forceHScale = 1;
			swissSettings.forceVOffset &= ~1;
			if (!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			if (!swissSettings.forceVJitter)
				swissSettings.forceVJitter = 1;
			break;
		case 7: case 14:
			if (!swissSettings.forceHScale)
				swissSettings.forceHScale = 1;
			swissSettings.forceVOffset &= ~1;
			if (!swissSettings.forceVFilter)
				swissSettings.forceVFilter = 1;
			break;
	}

	/* Swiss initialises the VI with this mode before booting the game
	   (ogc_video__reset): progressive for 4-7 / 11-14, else interlaced */
	if (in_range(swissSettings.gameVMode, 4, 7))
		swiss_newmode.viTVMode = VI_TVMODE_NTSC_PROG;
	else if (in_range(swissSettings.gameVMode, 11, 14))
		swiss_newmode.viTVMode = VI_TVMODE_PAL_PROG;
	else if (swissSettings.gameVMode == 3)
		swiss_newmode.viTVMode = 1;	/* NTSC DS (240p) */
	else if (swissSettings.gameVMode == 10)
		swiss_newmode.viTVMode = 5;	/* PAL DS (288p) */
	else if (in_range(swissSettings.gameVMode, 8, 9))
		swiss_newmode.viTVMode = 4;	/* PAL interlaced */
	else
		swiss_newmode.viTVMode = 0;	/* NTSC interlaced */

	swv_log("SwissVideo:gameVMode %i, forceVJitter %i, forceVFilter %i, aveCompat %i, sramVideo %i, 60Hz %i, progressive %i\r\n",
		swissSettings.gameVMode, swissSettings.forceVJitter, swissSettings.forceVFilter, swissSettings.aveCompat,
		swissSettings.sramVideo, swissSettings.sram60Hz, swissSettings.sramProgressive);
}

void SwissVideoPatch(char *Buffer, u32 Length)
{
	Length &= ~3;
	if (!Length)
		return;

#ifndef SWISSVIDEO_HOST_TEST
	swv_echo = 1;
	swv_loglen = 0;
#endif
	SwissVideoReadSettings();
	if (swv_ovr_swiss == 0) {
		/* Baseline: game's own video code, only log the VI registers */
		swv_log("SwissVideo:swiss=0, Swiss video patches NOT applied (baseline)\r\n");
		SwissVideoClearSnapshot();
#ifndef SWISSVIDEO_HOST_TEST
		swv_echo = 0;
#endif
		return;
	}
	SwissVideoSetup();
	SwissVideoTimingOverrides();

	/* clear the variable block the stubs use, Patch_Video() fills it */
	u32 off;
	for (off = VAR_BLOCK_START; off < VAR_BLOCK_END; off++)
		var_write8(off, 0);

	swv_base = (u32)Buffer;
	swv_length = Length;

	Patch_Video((u32 *)Buffer, Length, PATCH_DOL);

	sync_after_write(Buffer, Length);
	SwissVideoFlushVars();
	SwissVideoClearSnapshot();
#ifndef SWISSVIDEO_HOST_TEST
	swv_echo = 0;
#endif
}

#ifndef SWISSVIDEO_HOST_TEST
/*
	960i diagnostics. PADRead (loader/source/ppc/PADReadGC) copies the VI
	registers to VI_SNAPSHOT every frame while the game runs. Every few
	seconds they are added to the 960i log, which is (re)written to
	/nin960i.log on the boot device. This doesn't depend on Nintendont's
	own Log option, which doesn't work on every setup.
*/
#define VI_SNAPSHOT		0x13003200
#define VI_LOG_INTERVAL	(1898437 * 3)	/* ~3 seconds */
#define VI_LOG_MAX		10

extern bool DI_IRQ;

static u32 viLogTimer = 0, viLogCount = 0, viLogPending = 0;

static void SwissVideoWriteLog(void)
{
	FIL f;
	UINT wrote;

	if (f_open_char(&f, "/nin960i.log", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return;
	f_write(&f, swv_logbuf, swv_loglen, &wrote);
	f_close(&f);
}

void SwissVideoLogVI(void)
{
	u32 i, nonzero = 0;

	if (!SwissVideoEnabled())
		return;

	/* only touch the file system while no disc read is in flight */
	if (viLogPending && DI_IRQ == false) {
		SwissVideoWriteLog();
		viLogPending = 0;
	}

	if (viLogCount >= VI_LOG_MAX)
		return;
	if (viLogTimer == 0) {
		viLogTimer = read32(HW_TIMER);
		return;
	}
	if (TimerDiffTicks(viLogTimer) < VI_LOG_INTERVAL)
		return;
	viLogTimer = read32(HW_TIMER);

	sync_before_read((void *)VI_SNAPSHOT, 0x80);
	for (i = 0; i < 0x80; i += 4)
		nonzero |= read32(VI_SNAPSHOT + i);
	if (!nonzero)
		return;	/* game not running yet */

	viLogCount++;
	swv_log("SwissVideo:VI regs #%u\r\n", viLogCount);
	for (i = 0; i < 0x80; i += 0x10) {
		swv_log("VI %02X: %04X %04X %04X %04X %04X %04X %04X %04X\r\n", i,
			read16(VI_SNAPSHOT + i + 0x0), read16(VI_SNAPSHOT + i + 0x2),
			read16(VI_SNAPSHOT + i + 0x4), read16(VI_SNAPSHOT + i + 0x6),
			read16(VI_SNAPSHOT + i + 0x8), read16(VI_SNAPSHOT + i + 0xA),
			read16(VI_SNAPSHOT + i + 0xC), read16(VI_SNAPSHOT + i + 0xE));
	}
	viLogPending = 1;
}

static void SwissVideoClearSnapshot(void)
{
	u32 i;
	for (i = 0; i < 0x80; i += 4)
		write32(VI_SNAPSHOT + i, 0);
	sync_after_write((void *)VI_SNAPSHOT, 0x80);
	viLogTimer = 0;
	viLogCount = 0;
	viLogPending = 1;	/* write the patch log right away */
}
#else
static void SwissVideoClearSnapshot(void) {}
#endif

void SwissVideoFlushVars(void)
{
	sync_after_write((void *)(MEM1_BASE + VAR_BLOCK_START), VAR_BLOCK_END - VAR_BLOCK_START);
}
