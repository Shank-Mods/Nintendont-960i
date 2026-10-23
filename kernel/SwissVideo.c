/*
Nintendont (Kernel) - Swiss video patch engine

The signatures, patch logic and PowerPC stubs used here come from Swiss
(https://github.com/emukidid/swiss-gc, cube/swiss/source/patcher.c), which
can force GameCube games into other video modes (240p, 480p, 960i, ...),
force field rendering, filters, anisotropic filtering and widescreen.
Patch_Video(), Patch_Widescreen() and Patch_TexFilt() are imported
mechanically by swissvideo/port_patch_video.py, this file only provides
the environment they expect:

  - Swiss' pattern matching helpers (copied verbatim),
  - address translation for a DOL that is already loaded in place,
  - an installer for the hook stubs that uses Nintendont's patch area,
  - Swiss' settings, worked out by the loader for the game being booted
    (SWISS_BOOT, see common/include/SwissVideoConfig.h),
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
#include "swissvideo/hires/hires_shared.h"
#include "swissvideo/hires/hires_payload.h"

/* From Patch.c */
extern u32 PatchCopy(const u8 *PatchPtr, const u32 PatchSize);

#ifndef SWISSVIDEO_HOST_TEST
static void swv_log(const char *fmt, ...);
#else
#define swv_log printf
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
	int forceAnisotropy;
	int forceWidescreen;
	int invertFieldOrder;	/* Nintendont addition, see SwissVideoInvertFieldOrder() */
	int hiRes240p;		/* Nintendont experiment, see SwissVideoHiResInstall() */
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

static inline void var_write32(u32 off, u32 val)
{
	__asm__ volatile ("str\t%0, [%1]" : : "l" (val), "l" (off));
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

static inline void var_write32(u32 off, u32 val)
{
	*(volatile u32 *)(MEM1_BASE + off) = val;
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
	GX_INITTEXOBJLODHOOK,
	GX_SETPROJECTIONHOOK,
	GX_SETSCISSORHOOK,
	MTX_FRUSTUMHOOK,
	MTX_LIGHTFRUSTUMHOOK,
	MTX_LIGHTPERSPECTIVEHOOK,
	MTX_ORTHOHOOK,
	MTX_PERSPECTIVEHOOK,
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
	[GX_INITTEXOBJLODHOOK]      = { GXInitTexObjLODHook,     GXInitTexObjLODHook_size },
	[GX_SETPROJECTIONHOOK]      = { GXSetProjectionHook,     GXSetProjectionHook_size },
	[GX_SETSCISSORHOOK]         = { GXSetScissorHook,        GXSetScissorHook_size },
	[MTX_FRUSTUMHOOK]           = { MTXFrustumHook,          MTXFrustumHook_size },
	[MTX_LIGHTFRUSTUMHOOK]      = { MTXLightFrustumHook,     MTXLightFrustumHook_size },
	[MTX_LIGHTPERSPECTIVEHOOK]  = { MTXLightPerspectiveHook, MTXLightPerspectiveHook_size },
	[MTX_ORTHOHOOK]             = { MTXOrthoHook,            MTXOrthoHook_size },
	[MTX_PERSPECTIVEHOOK]       = { MTXPerspectiveHook,      MTXPerspectiveHook_size },
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
		const unsigned char *patch = patch_table[patchId].patch;
		u32 size = patch_table[patchId].size;
		/* 1280x240p experiment: its own mode stub in place of Swiss' 480p one */
		if (patchId == VI_CONFIGURE480P && swissSettings.hiRes240p) {
			patch = VIConfigure1280x240p;
			size = VIConfigure1280x240p_size;
		}
		u32 phys = PatchCopy(patch, size);
		patch_locations[patchId] = (void *)(0x80000000 | (phys - MEM1_BASE));
		swv_log("SwissVideo:Installed patch %i to %08X\r\n", patchId, patch_locations[patchId]);
	}
	return patch_locations[patchId];
}

/* Swiss' installPatch(): a new copy every time (Patch_Widescreen, Patch_TexFilt) */
static void *installPatch(int patchId)
{
	u32 phys;
	void *addr;
	if (patchId < 0 || patchId >= PATCHES_MAX)
		return NULL;
	phys = PatchCopy(patch_table[patchId].patch, patch_table[patchId].size);
	addr = (void *)(0x80000000 | (phys - MEM1_BASE));
	swv_log("SwissVideo:Installed patch %i to %08X\r\n", patchId, addr);
	return addr;
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
/* What Patch_Video() found, for the 1280x240p experiment */
static u32 swv_exp_copydisp, swv_exp_viewport, swv_exp_yscale;
static int swv_exp_copydisp_j = -1, swv_exp_viewport_j = -1, swv_exp_yscale_j = -1;

static int swv_first_found(const FuncPattern *sigs, u32 count, u32 *at)
{
	u32 j;
	for (j = 0; j < count; j++) {
		if (sigs[j].offsetFoundAt) {
			*at = sigs[j].offsetFoundAt;
			return j;
		}
	}
	*at = 0;
	return -1;
}

#define SWV_PATCH_VIDEO_EXPORT() do { \
	swv_exp_copydisp_j = swv_first_found(GXCopyDispSigs, countof(GXCopyDispSigs), &swv_exp_copydisp); \
	swv_exp_viewport_j = swv_first_found(GXSetViewportSigs, countof(GXSetViewportSigs), &swv_exp_viewport); \
	swv_exp_yscale_j = swv_first_found(GXSetDispCopyYScaleSigs, countof(GXSetDispCopyYScaleSigs), &swv_exp_yscale); \
} while (0)

#include "swissvideo/Patch_Video.inc"
#include "swissvideo/Patch_Extras.inc"

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
	swv_log("SwissVideo:%s hsy %u hcs %u hce %u hbe640 %u hbs640 %u\r\n", what,
		t[28], t[29], t[30], t[31], swv_get(t, 20));
	swv_log("SwissVideo:%s frame length odd %u / even %u half-lines\r\n", what,
		3 * equ + swv_get(t, 2) + 2 * acv + swv_get(t, 4),
		3 * equ + swv_get(t, 3) + 2 * acv + swv_get(t, 5));
}

/* Settings read from /nin960i_vi.txt (-1 = not set) */
static s32 swv_ovr_vmode, swv_ovr_vjitter, swv_ovr_vfilter, swv_ovr_avecompat;
static s32 swv_ovr_swiss;	/* 0 = don't apply the Swiss patches (baseline) */
static s32 swv_ovr_hrtest = -1;	/* 1280x240p test flags (HR_TEST_*) */
static s32 swv_ovr_timing[countof(swv_timing_fields)];
static s32 swv_ovr_hr_timing[countof(swv_timing_fields)];	/* hr_<field>: 1280x240p */

static void swv_apply_setting(const char *name, u32 n)
{
	u32 j;

	if (strcmp(name, "gamevmode") == 0)        swv_ovr_vmode = n;
	else if (strcmp(name, "swiss") == 0)        swv_ovr_swiss = n;
	else if (strcmp(name, "hrtest") == 0)       swv_ovr_hrtest = n;
	else if (strcmp(name, "forcevjitter") == 0) swv_ovr_vjitter = n;
	else if (strcmp(name, "forcevfilter") == 0) swv_ovr_vfilter = n;
	else if (strcmp(name, "avecompat") == 0)    swv_ovr_avecompat = n;
	else {
		s32 *ovr = swv_ovr_timing;
		const char *field = name;
		if (name[0] == 'h' && name[1] == 'r' && name[2] == '_') {
			ovr = swv_ovr_hr_timing;
			field = name + 3;
		}
		for (j = 0; j < countof(swv_timing_fields); j++) {
			if (strcmp(field, swv_timing_fields[j].name) == 0) {
				ovr[j] = n;
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
	/* static: the whole file must fit, templates are a few KB */
	static char buf[8192];
	char name[16];
	FIL f;
	UINT read = 0;
	u32 i, n;

	swv_ovr_vmode = swv_ovr_vjitter = swv_ovr_vfilter = swv_ovr_avecompat = swv_ovr_swiss = -1;
	for (i = 0; i < countof(swv_ovr_timing); i++)
		swv_ovr_timing[i] = swv_ovr_hr_timing[i] = -1;

#ifndef NIN960I_EXPERIMENTS
	/* Release/test builds: fixed Swiss 960i settings, no settings file */
	(void)buf; (void)name; (void)f; (void)read; (void)n;
	return;
#endif
	if (f_open_char(&f, "/nin960i_vi.txt", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
		swv_log("SwissVideo:no /nin960i_vi.txt, using defaults\r\n");
		return;
	}
	f_read(&f, buf, sizeof(buf) - 1, &read);
	if (f.obj.objsize > sizeof(buf) - 1)
		swv_log("SwissVideo:WARNING /nin960i_vi.txt is %u bytes, only %u read\r\n",
			(u32)f.obj.objsize, (u32)read);
	f_close(&f);
	buf[read] = 0;
	swv_log("SwissVideo:read /nin960i_vi.txt (%u bytes)\r\n", (u32)read);

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
		swv_ovr_timing[i] = swv_ovr_hr_timing[i] = -1;
}
#endif

static void SwissVideoTimingOverridesTo(u8 *t, const s32 *ovr)
{
	u32 j, any = 0;

	swv_log_timing("default", t);
	for (j = 0; j < countof(swv_timing_fields); j++) {
		if (ovr[j] >= 0) {
			swv_set(t, j, ovr[j]);
			any = 1;
		}
	}
	if (any)
		swv_log_timing("override", t);
}

static void SwissVideoTimingOverrides(void)
{
	if (swissSettings.gameVMode == 4)
		SwissVideoTimingOverridesTo(video_timing_960i, swv_ovr_timing);
	else if (swissSettings.gameVMode == 11)
		SwissVideoTimingOverridesTo(video_timing_1152i, swv_ovr_timing);
}

/*
	Settings for the game being booted, worked out by the loader from the
	"Swiss Video" settings page (loader/source/swissvideo.c, a port of
	Swiss' config_load_current()). NULL if the patches are off.
*/
#ifndef SWISSVIDEO_HOST_TEST
static const SWISS_BOOT *SwissVideoBoot(void)
{
	const SWISS_BOOT *b = (const SWISS_BOOT *)SWISS_BOOT_ADDR_ARM;
	sync_before_read((void *)b, sizeof(SWISS_BOOT));
	return (b->Magic == SWISS_BOOT_MAGIC) ? b : NULL;
}
#else
extern SWISS_BOOT test_swiss_boot;
static const SWISS_BOOT *SwissVideoBoot(void)
{
	return (test_swiss_boot.Magic == SWISS_BOOT_MAGIC) ? &test_swiss_boot : NULL;
}
#endif

bool SwissVideoEnabled(void)
{
	/* The Wii U only scales the video signal. (The loader checks this too.) */
	if (IsWiiU())
		return false;
	return SwissVideoBoot() != NULL;
}

bool SwissVideoSramFlags(bool *progressive, bool *pal60)
{
	const SWISS_BOOT *b = SwissVideoBoot();
	if (IsWiiU() || !b || b->gameVMode <= 0)
		return false;	/* Auto: Nintendont's own SRAM settings */
	*progressive = b->sramProgressive;
	*pal60 = b->sram60Hz;
	return true;
}

bool SwissVideoWidescreen(void)
{
	const SWISS_BOOT *b = SwissVideoBoot();
	return SwissVideoEnabled() && b->forceWidescreen;
}

/* Copies the loader's settings into Swiss' swissSettings. */
static void SwissVideoSetup(void)
{
	const SWISS_BOOT *b = SwissVideoBoot();

	memset(&swissSettings, 0, sizeof(swissSettings));
	if (!b)
		return;

	swissSettings.gameVMode = b->gameVMode;
	swissSettings.aveCompat = b->aveCompat;
	swissSettings.forceDTVStatus = b->forceDTVStatus;
	swissSettings.rt4kOptim = b->rt4kOptim;
	swissSettings.forceVideoActive = b->forceVideoActive;
	swissSettings.forceHScale = b->forceHScale;
	swissSettings.forceVOffset = b->forceVOffset;
	swissSettings.forceVFilter = b->forceVFilter;
	swissSettings.forceVJitter = b->forceVJitter;
	swissSettings.fixPixelCenter = b->fixPixelCenter;
	swissSettings.disableDithering = b->disableDithering;
	swissSettings.forceAnisotropy = b->forceAnisotropy;
	swissSettings.forceWidescreen = b->forceWidescreen;
	swissSettings.invertFieldOrder = b->invertFieldOrder;
	/* The 1280x240p experiment builds on Swiss' 480p path (54 MHz, progressive) */
	swissSettings.hiRes240p = (b->gameVMode == 5) ? b->hiRes240p : 0;
	swissSettings.sramVideo = b->sramVideo;
	swissSettings.sram60Hz = b->sram60Hz;
	swissSettings.sramProgressive = b->sramProgressive;

	/* Experiments build: /nin960i_vi.txt overrides */
	if (swv_ovr_vmode >= 0) swissSettings.gameVMode = swv_ovr_vmode;
	if (swv_ovr_vjitter >= 0) swissSettings.forceVJitter = swv_ovr_vjitter;
	if (swv_ovr_vfilter >= 0) swissSettings.forceVFilter = swv_ovr_vfilter;
	if (swv_ovr_avecompat >= 0) swissSettings.aveCompat = swv_ovr_avecompat;
#ifdef SWISSVIDEO_AVE_COMPAT
	/* make SWISSVIDEO_AVE=... forces a compatibility mode */
	swissSettings.aveCompat = SWISSVIDEO_AVE_COMPAT;
#endif

	/*
		The mode Swiss initialises the VI with before booting the game
		(ogc_video__reset()); the loader does the same on the Wii side.
	*/
	switch (swissSettings.gameVMode) {
		case 1: case 2:		swiss_newmode.viTVMode = 0; break;	/* NTSC interlaced */
		case 3:			swiss_newmode.viTVMode = 1; break;	/* NTSC DS (240p) */
		case 4 ... 7:		swiss_newmode.viTVMode = VI_TVMODE_NTSC_PROG; break;
		case 8: case 9:		swiss_newmode.viTVMode = 4; break;	/* PAL interlaced */
		case 10:		swiss_newmode.viTVMode = 5; break;	/* PAL DS (288p) */
		case 11 ... 14:		swiss_newmode.viTVMode = VI_TVMODE_PAL_PROG; break;
		default:	/* Auto */
			switch (swissSettings.sramVideo) {
				case SYS_VIDEO_PAL:	swiss_newmode.viTVMode = 4; break;
				case SYS_VIDEO_MPAL:	swiss_newmode.viTVMode = 8; break;
				default:		swiss_newmode.viTVMode = 0; break;
			}
			break;
	}

	swv_log("SwissVideo:gameVMode %i, aveCompat %i, forceHScale %i, forceVOffset %i, forceVFilter %i, forceVJitter %i\r\n",
		swissSettings.gameVMode, swissSettings.aveCompat, swissSettings.forceHScale,
		swissSettings.forceVOffset, swissSettings.forceVFilter, swissSettings.forceVJitter);
	swv_log("SwissVideo:fixPixelCenter %i, disableDithering %i, forceAnisotropy %i, forceWidescreen %i, forceDTVStatus %i, rt4k %i, videoActive %i\r\n",
		swissSettings.fixPixelCenter, swissSettings.disableDithering, swissSettings.forceAnisotropy,
		swissSettings.forceWidescreen, swissSettings.forceDTVStatus, swissSettings.rt4kOptim,
		swissSettings.forceVideoActive);
	swv_log("SwissVideo:invertFieldOrder %i\r\n", swissSettings.invertFieldOrder);
	swv_log("SwissVideo:sramVideo %i, 60Hz %i, progressive %i, viTVMode %i\r\n",
		swissSettings.sramVideo, swissSettings.sram60Hz, swissSettings.sramProgressive,
		swiss_newmode.viTVMode);
}

/*
	"Invert Field Order" (not a Swiss option): Swiss' forced field rendering
	takes the parity it renders each frame for from VAR_NEXT_FIELD, which
	VIRetraceHandlerHook sets to !current field (srwi / lis / xori 1 / stb).
	Some games show their frames one field later than Swiss expects, so
	every field is rendered for the wrong parity. Removing the xori makes
	VAR_NEXT_FIELD the current field instead, inverting the parity used by
	every GXSetViewport / __GXSetViewport path that reads VAR_CURRENT_FIELD.
	Games that pass their own field to GXSetViewportJitter are unaffected.
*/
#define PPC_XORI_R3_R3_1	0x68630001
#define PPC_NOP			0x60000000

static void SwissVideoInvertFieldOrder(void)
{
	u32 *hook = patch_locations[VI_RETRACEHANDLERHOOK];
	if (!hook) {
		swv_log("SwissVideo:invert field order: no forced field rendering in this executable\r\n");
		return;
	}
	if (P2C_PTR(hook)[2] != PPC_XORI_R3_R3_1) {
		swv_log("SwissVideo:invert field order: unexpected VIRetraceHandlerHook %08X, not changed\r\n",
			P2C_PTR(hook)[2]);
		return;
	}
	P2C_PTR(hook)[2] = PPC_NOP;
	sync_after_write(P2C_PTR(hook), 0x20);
	swv_log("SwissVideo:field order inverted (VIRetraceHandlerHook @ %08X)\r\n", hook);
}

/* ------------------------------------------------------------------------ */
/* 1280x240p two-pass experiment (Nintendont addition)                      */
/* ------------------------------------------------------------------------ */

/*
	VI timing for 1280x240p: the vertical timing of NTSC 240p (equ 6,
	acv 240, prb 24, psb 4, 526 half-lines = 263 lines), but at the 54 MHz
	clock with every horizontal value doubled (half-line 858 clocks at
	27 MHz pixels = 15.73 kHz lines). hbe640/hbs640 together with the
	VIConfigure1280x240p stub (viWidth 640, viXOrigin 80) give an active
	window of 1280 clocks, from clock 295 (10.9 us after sync) to 1575.
	Horizontal sync is 127 clocks (4.7 us), the most the register holds.
	Burst timing (hcs/hce) is the 480p one; component/RGB only.
	Used in timing slot 6 (NTSC progressive) instead of 480p.
*/
static const u8 video_timing_1280x240p[38] = {
	0x06,0x00,0x00,0xF0,	/* equ 6, acv 240 */
	0x00,0x18,0x00,0x18,	/* prb 24/24 */
	0x00,0x04,0x00,0x04,	/* psb 4/4 */
	0x0C,0x0C,0x0C,0x0C,	/* bs1-4 */
	0x02,0x08,0x02,0x08,	/* be1-2 520 */
	0x02,0x08,0x02,0x08,	/* be3-4 520 */
	0x02,0x0E,0x03,0x5A,	/* nhlines 526, hlw 858 */
	0x7F,0x47,0x69,0xFF,	/* hsy 127, hcs 71, hce 105, hbe640 255 */
	0x02,0xA5,0xF4,0x00,	/* hbs640 677, hbeCCIR656 244 */
	0x03,0x38		/* hbsCCIR656 824 */
};

/* GXSetScissor signatures, from Swiss' Patch_Widescreen() */
static FuncPattern hr_GXSetScissorSigs[3] = {
	{ 44, 19, 6, 0, 0, 6, NULL, 0, "GXSetScissor" },
	{ 36, 12, 6, 0, 0, 6, NULL, 0, "GXSetScissor" },
	{ 30, 13, 6, 0, 0, 1, NULL, 0, "GXSetScissor" }
};

static u8 hr_data_init[sizeof(struct hr_data)];
static u32 *hr_scissor_func;
static u32 hr_gxdata_bits;	/* rA/d of an lwz that loads __GXData */

/* Is insn "lwz rX, d(r2)" or "lwz rX, d(r13)", i.e. a load of an SDA pointer? */
static bool hr_is_sda_lwz(u32 insn)
{
	u32 ra = (insn >> 16) & 0x1F;
	return (insn & 0xFC000000) == 0x80000000 && (ra == 2 || ra == 13);
}

/* Can func's first instruction be moved into a trampoline? */
static bool hr_hookable(u32 *func, const char *name)
{
	u32 insn;
	if (!func) {
		swv_log("SwissVideo:hires: %s not found\r\n", name);
		return false;
	}
	insn = P2C_PTR(func)[0];
	/* a relative branch can't be moved */
	if ((insn & 0xFC000000) == 0x48000000 || (insn & 0xFC000000) == 0x40000000) {
		swv_log("SwissVideo:hires: %s starts with a branch (%08X)\r\n", name, insn);
		return false;
	}
	return true;
}

/* Entry hook: func's first instruction goes into tramp (+ branch back). */
static void hr_hook(u32 *func, u32 *tramp, u32 *target, const char *name)
{
	u32 insn = P2C_PTR(func)[0];
	P2C_PTR(tramp)[0] = insn;
	P2C_PTR(tramp)[1] = branch(func + 1, tramp + 1);
	P2C_PTR(func)[0] = branch(target, func);
	swv_log("SwissVideo:hires: hooked %s @ %08X (first %08X)\r\n", name, func, insn);
}

/*
	Before Patch_Video(): GXSetScissor is the one function Swiss doesn't find
	in its video pass. Without it the experiment is off (normal 480p).
*/
static bool SwissVideoHiResPrepare(u32 *data, u32 length)
{
	u32 i, j, n = 0;
	FuncPattern fp;

	hr_scissor_func = NULL;
	for (i = 0; i + 1 < length / sizeof(u32); i++) {
		if ((data[i + 1] & 0xFC00FFFF) != 0x38000156)
			continue;
		make_pattern(data, PATCH_DOL, i, length, &fp);
		for (j = 0; j < countof(hr_GXSetScissorSigs); j++) {
			if (compare_pattern(&fp, &hr_GXSetScissorSigs[j])) {
				if (!hr_scissor_func)
					hr_scissor_func = Calc_ProperAddress(data, PATCH_DOL, i * sizeof(u32));
				n++;
				swv_log("Found:[%s$%i] @ %08X\r\n", hr_GXSetScissorSigs[j].Name, j,
					Calc_ProperAddress(data, PATCH_DOL, i * sizeof(u32)));
			}
		}
	}
	if (n != 1 || !hr_hookable(hr_scissor_func, "GXSetScissor")) {
		swv_log("SwissVideo:hires: GXSetScissor found %u times, 1280x240p off (normal 480p)\r\n", n);
		return false;
	}
	/*
		GXSetScissor works on __GXData->suScis0/1 right away, so one of its
		first instructions loads the __GXData pointer from the small data
		area (lwz rX, d(r2) in Kirby Air Ride: 8102F070).
	*/
	hr_gxdata_bits = 0;
	for (i = 0; i < 8; i++) {
		u32 insn = P2C_PTR(hr_scissor_func)[i];
		if (hr_is_sda_lwz(insn)) {
			hr_gxdata_bits = insn & 0x1FFFFF;
			swv_log("SwissVideo:hires: __GXData load in GXSetScissor+%u: %08X\r\n", i * 4, insn);
			break;
		}
	}
	if (!hr_gxdata_bits) {
		swv_log("SwissVideo:hires: no __GXData load in GXSetScissor, 1280x240p off (normal 480p)\r\n");
		return false;
	}
	return true;
}

/*
	GXCallDisplayList (Dolphin SDK): flushes GX's dirty state, then writes
	the CALL_DL command:
		li    rX, 0x40
		lis   rY, 0xCC01
		stb   rX, -0x8000(rY)
		stw   rA, -0x8000(rY)		list
		stw   rB, -0x8000(rY)		nbytes
	The li/lis come in either order. The function start is the closest
	"mflr r0" before it with no blr in between.
*/
static u32 *hr_find_calldl(u32 *data, u32 length)
{
	u32 i, k, n = 0, words = length / sizeof(u32);
	u32 *found = NULL;

	for (i = 2; i + 2 < words; i++) {
		u32 st = data[i], rx, ry, li = 0;
		if ((st & 0xFC00FFFF) != 0x98008000)
			continue;
		rx = (st >> 21) & 0x1F;
		ry = (st >> 16) & 0x1F;
		if ((data[i + 1] & 0xFC1FFFFF) != (0x90008000 | (ry << 16)) ||
		    (data[i + 2] & 0xFC1FFFFF) != (0x90008000 | (ry << 16)))
			continue;
		for (k = 1; k <= 4 && k <= i; k++) {
			if (data[i - k] == (0x38000040 | (rx << 21)))
				li = 1;
		}
		if (!li)
			continue;
		for (k = 1; k <= 48 && k <= i; k++) {
			if (data[i - k] == 0x4E800020)	/* blr: no prologue in this function */
				break;
			if (data[i - k] == 0x7C0802A6) {	/* mflr r0 */
				u32 *f = Calc_ProperAddress(data, PATCH_DOL, (i - k) * sizeof(u32));
				swv_log("Found:[GXCallDisplayList] @ %08X (CALL_DL at +%u)\r\n", f, k * 4);
				if (!found)
					found = f;
				n++;
				break;
			}
		}
	}
	if (n != 1) {
		swv_log("SwissVideo:hires: GXCallDisplayList found %u times, display lists not copied\r\n", n);
		return NULL;
	}
	return found;
}

static void SwissVideoHiResInstall(u32 *data, u32 length)
{
	u32 i, gxbits = 0;
	u32 *viewport, *scissor = hr_scissor_func, *copydisp;
	u32 payload, block;
	struct hr_data *hd;
	u32 *hdv;

	swv_log("SwissVideo:hires: mode %i, GXCopyDisp$%i, GXSetViewport$%i, GXSetDispCopyYScale$%i\r\n",
		swissSettings.hiRes240p, swv_exp_copydisp_j, swv_exp_viewport_j, swv_exp_yscale_j);
	if (swv_exp_copydisp_j < 0 || swv_exp_viewport_j < 0) {
		swv_log("SwissVideo:hires: ERROR SDK functions missing, the picture will be wrong\r\n");
		return;
	}

	/* __GXData pointer load, found in GXSetScissor (SwissVideoHiResPrepare) */
	gxbits = hr_gxdata_bits;
	(void)i;
	viewport = Calc_ProperAddress(data, PATCH_DOL, swv_exp_viewport * sizeof(u32));
	copydisp = Calc_ProperAddress(data, PATCH_DOL, swv_exp_copydisp * sizeof(u32));
	if (!hr_hookable(viewport, "GXSetViewport") || !hr_hookable(copydisp, "GXCopyDisp")) {
		swv_log("SwissVideo:hires: ERROR can't hook, the picture will be wrong\r\n");
		return;
	}

	/* Payload and its state block, in the patch area */
#ifndef SWISSVIDEO_HOST_TEST
	if (hires_payload_size <= HR_PAYLOAD_MEM2_MAX) {
		memcpy((void *)HR_PAYLOAD_MEM2_ARM, hires_payload, hires_payload_size);
		sync_after_write((void *)HR_PAYLOAD_MEM2_ARM, hires_payload_size);
		payload = HR_PAYLOAD_MEM2_PPC;
	} else
#endif
		payload = 0x80000000 | (PatchCopy(hires_payload, hires_payload_size) - MEM1_BASE);
	memset(hr_data_init, 0, sizeof(hr_data_init));
	hd = (struct hr_data *)hr_data_init;
	hd->mode = swissSettings.hiRes240p;
	hd->test = swv_ovr_hrtest > 0 ? swv_ovr_hrtest : 0;
	if (hd->test)
		swv_log("SwissVideo:hires: test flags %u\r\n", hd->test);
	hd->gxdata_fn[0] = 0x80600000 | gxbits;	/* lwz r3, d(rA) */
	hd->gxdata_fn[1] = 0x4E800020;		/* blr */
	hd->cur_l = 0; hd->cur_t = 0; hd->cur_w = 640; hd->cur_h = 480; hd->cur_main = 1;
	/*
		The patch area (0x80001800-0x80003000) is nearly full with the
		payload in it. Without cheats/debugger, 0x80001000-0x800012FF is
		unused (the code handler's place; the next exception vector the
		game installs is 0x1300), so the state block goes there.
	*/
#ifndef SWISSVIDEO_HOST_TEST
	if (sizeof(hr_data_init) <= 0x300 && !ConfigGetConfig(NIN_CFG_CHEATS | NIN_CFG_DEBUGGER)) {
		memcpy((void *)(MEM1_BASE + 0x1000), hr_data_init, sizeof(hr_data_init));
		block = 0x80001000;
	} else
#endif
		block = 0x80000000 | (PatchCopy(hr_data_init, sizeof(hr_data_init)) - MEM1_BASE);
	hdv = (u32 *)block;

	/* absolute jumps to the payload entries (it may be out of b range) */
	{
		static const u32 entry[4] = { HR_ENTRY_VIEWPORT, HR_ENTRY_SCISSOR,
			HR_ENTRY_COPYDISP, HR_ENTRY_CALLDL };
		u32 *j = P2C_PTR(hdv + __builtin_offsetof(struct hr_data, jmp) / 4);
		for (i = 0; i < 4; i++) {
			u32 t = payload + entry[i];
			j[i * 4 + 0] = 0x3D800000 | (t >> 16);		/* lis r12, t@h */
			j[i * 4 + 1] = 0x618C0000 | (t & 0xFFFF);	/* ori r12, r12, t@l */
			j[i * 4 + 2] = 0x7D8903A6;			/* mtctr r12 */
			j[i * 4 + 3] = 0x4E800420;			/* bctr */
		}
	}
#define HR_JMP(n)	(hdv + __builtin_offsetof(struct hr_data, jmp) / 4 + (n) * 4)
	hr_hook(viewport, hdv + 1, HR_JMP(0), "GXSetViewport");
	hr_hook(scissor, hdv + 3, HR_JMP(1), "GXSetScissor");
	hr_hook(copydisp, hdv + 5, HR_JMP(2), "GXCopyDisp");
	if (swissSettings.hiRes240p == HR_MODE_TWOPASS) {
		u32 *calldl = hr_find_calldl(data, length);
		if (calldl && hr_hookable(calldl, "GXCallDisplayList"))
			hr_hook(calldl, hdv + __builtin_offsetof(struct hr_data, tramp_dl) / 4,
				HR_JMP(3), "GXCallDisplayList");
	}
#undef HR_JMP
	sync_after_write(P2C_PTR(block), sizeof(struct hr_data));
	if (payload < 0x90000000)
		sync_after_write(P2C_PTR(payload), hires_payload_size);

	var_write32(HR_DATA_PTR_VAR, block);
	sync_after_write((void *)(MEM1_BASE + (HR_DATA_PTR_VAR & ~0x1F)), 0x20);

#ifndef SWISSVIDEO_HOST_TEST
	for (i = 0; i < sizeof(struct hr_stats); i += 4)
		write32(HR_STATS_ARM + i, 0);
	write32(HR_STATS_ARM, HR_STATS_MAGIC);
	sync_after_write((void *)HR_STATS_ARM, sizeof(struct hr_stats));
	for (i = 0; i < sizeof(struct hr_stats2); i += 4)
		write32(HR_STATS2_ARM + i, 0);
	sync_after_write((void *)HR_STATS2_ARM, sizeof(struct hr_stats2));
	for (i = 0; i < 0x40; i += 4)
		write32(HR_GAMESNAP_ARM + i, 0);
	sync_after_write((void *)HR_GAMESNAP_ARM, 0x40);
#endif
	swv_log("SwissVideo:hires: payload @ %08X (%u bytes), data @ %08X, __GXData lwz %08X\r\n",
		payload, hires_payload_size, block, hd->gxdata_fn[0]);
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

	/* 1280x240p: our timing in the NTSC progressive slot (restored after) */
	u8 saved_timing[38];
	if (swissSettings.hiRes240p && !SwissVideoHiResPrepare((u32 *)Buffer, Length))
		swissSettings.hiRes240p = 0;
	if (swissSettings.hiRes240p) {
		memcpy(saved_timing, video_timing + 6 * 38, 38);
		memcpy(video_timing + 6 * 38, video_timing_1280x240p, 38);
		/* experiments build: hr_<field> lines of /nin960i_vi.txt */
		SwissVideoTimingOverridesTo(video_timing + 6 * 38, swv_ovr_hr_timing);
	}

	Patch_Video((u32 *)Buffer, Length, PATCH_DOL);
	if (swissSettings.hiRes240p) {
		memcpy(video_timing + 6 * 38, saved_timing, 38);
		SwissVideoHiResInstall((u32 *)Buffer, Length);
	}
	/* Same order as Swiss' Patch_ExecutableFile() */
	if (swissSettings.forceWidescreen)
		Patch_Widescreen((u32 *)Buffer, Length, PATCH_DOL);
	if (swissSettings.forceAnisotropy)
		Patch_TexFilt((u32 *)Buffer, Length, PATCH_DOL);
	if (swissSettings.invertFieldOrder)
		SwissVideoInvertFieldOrder();

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

/* 1280x240p experiment counters (written by the payload, see hires_shared.h) */
static void SwissVideoLogHiRes(void)
{
	const struct hr_stats *s = (const struct hr_stats *)HR_STATS_ARM;
	if (!swissSettings.hiRes240p)
		return;
	sync_before_read((void *)HR_STATS_ARM, sizeof(struct hr_stats));
	if (s->magic != HR_STATS_MAGIC) {
		swv_log("SwissVideo:hires: no counters (payload not running)\r\n");
		return;
	}
	swv_log("SwissVideo:hires: copies %u replays %u | skip first %u big %u overwritten %u nocopy %u timeout %u | not replayed %u\r\n",
		s->copies, s->replays, s->skip_first, s->skip_big, s->skip_overwritten,
		s->skip_nocopy, s->skip_timeout1, s->timeout2);
	swv_log("SwissVideo:hires: fifo %08X size %u, frame %u max %u bytes, scissors patched %u lost %u\r\n",
		s->fifo_base, s->fifo_size, s->last_frame, s->max_frame, s->last_patched, s->sc_lost);
	swv_log("SwissVideo:hires: GXSetScissor calls %u full %u last %u,%u %ux%u | viewport %08X x %08X | copy regs ok %u bad %u (size %08X)\r\n",
		s->sc_calls, s->sc_main, s->last_sc_lt >> 16, s->last_sc_lt & 0xFFFF,
		s->last_sc_wh >> 16, s->last_sc_wh & 0xFFFF, s->vp_w, s->vp_h,
		s->gxdata_ok, s->gxdata_bad, s->last_disp_size);
	{
		const struct hr_stats2 *s2 = (const struct hr_stats2 *)HR_STATS2_ARM;
		sync_before_read((void *)HR_STATS2_ARM, sizeof(struct hr_stats2));
		swv_log("SwissVideo:hires: draw done/token interrupts removed %u | viewports moved %u\r\n",
			s->pe_nopped, s2->vp_shifted);
		swv_log("SwissVideo:hires: display lists copied %u (%u bytes, largest %u) called %u (largest %u)\r\n",
			s2->dl_inlined, s2->dl_bytes, s2->dl_max, s2->dl_called, s2->dl_big);
		swv_log("SwissVideo:hires: GPU resets after a hung replay %u (frames mod 32: %08X) | CP distance fixed %u | FIFO end padded %u\r\n",
			s2->recovered, s2->hung_frames, s2->dist_fixed, s2->unused_pad);
		if (s->diag_where)
			swv_log("SwissVideo:hires: first timeout in pass %u: CP status/control %08X distance %u read pos %d token %08X, %u markers\r\n",
				s->diag_where, s->diag_sr, s->diag_cnt, (s32)s->diag_rp, s->diag_tok, s2->diag_nmk);
	}
	{
		const u32 *g = (const u32 *)HR_GAMESNAP_ARM;
		sync_before_read((void *)HR_GAMESNAP_ARM, 0x40);
		swv_log("SwissVideo:hires: PADRead thread %08X | drawing thread %08X state/suspend %08X at %08X lr %08X\r\n",
			g[0], g[1], g[10], g[2], g[3]);
		swv_log("SwissVideo:hires: CP status/control %08X distance %u rp %08X wp %08X\r\n",
			g[6], g[7], g[8], g[9]);
	}
}

static u32 viLogTimer = 0, viLogCount = 0, viLogPending = 0;
static u32 viLogWritten = 0, viLogHold = 0;	/* first write held back ~8 s */

/*
	Each boot gets its own numbered log, /nin960i_01.log ... _99.log (the
	first number not on the drive yet), so several tests can be run before
	copying them off. /nin960i.log is always the latest one.
*/
static char swv_logname[20];

static void SwissVideoWriteLogTo(const char *name)
{
	FIL f;
	UINT wrote;

	if (f_open_char(&f, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return;
	f_write(&f, swv_logbuf, swv_loglen, &wrote);
	f_close(&f);
}

static void SwissVideoWriteLog(void)
{
	if (!swv_logname[0]) {
		FIL f;
		u32 n;
		strcpy(swv_logname, "/nin960i_00.log");
		for (n = 1; n <= 99; n++) {
			swv_logname[9] = '0' + n / 10;
			swv_logname[10] = '0' + n % 10;
			if (f_open_char(&f, swv_logname, FA_READ | FA_OPEN_EXISTING) != FR_OK)
				break;
			f_close(&f);
		}
		if (n > 99)
			swv_logname[9] = swv_logname[10] = '9';	/* full: keep rewriting _99 */
		swv_log("SwissVideo:this log is also saved as %s\r\n", swv_logname);
	}
	SwissVideoWriteLogTo(swv_logname);
	SwissVideoWriteLogTo("/nin960i.log");
}

void SwissVideoLogVI(void)
{
	u32 i, nonzero = 0;

	if (!SwissVideoEnabled())
		return;

	/* only touch the file system while no disc read is in flight */
	/*
		Writing the log while the game is still loading competes with its
		disc reads, so the first write waits ~8 s (a run that dies earlier
		still gets its log then).
	*/
	if (viLogPending && !viLogWritten) {
		if (!viLogHold)
			viLogHold = read32(HW_TIMER);
		if (TimerDiffTicks(viLogHold) >= 1898437 * 8)
			viLogWritten = 1;
	}
	if (viLogPending && viLogWritten && DI_IRQ == false) {
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
	if (!nonzero) {
		/* no PADRead yet; the 1280x240p counters can still tell how far it got */
		const struct hr_stats *s = (const struct hr_stats *)HR_STATS_ARM;
		if (!swissSettings.hiRes240p)
			return;
		sync_before_read((void *)HR_STATS_ARM, sizeof(struct hr_stats));
		if (s->magic != HR_STATS_MAGIC || s->copies == 0)
			return;	/* game not running yet */
		viLogCount++;
		swv_log("SwissVideo:no VI snapshot yet (#%u)\r\n", viLogCount);
		SwissVideoLogHiRes();
		viLogPending = 1;
		return;
	}

	viLogCount++;
	swv_log("SwissVideo:VI regs #%u\r\n", viLogCount);
	for (i = 0; i < 0x80; i += 0x10) {
		swv_log("VI %02X: %04X %04X %04X %04X %04X %04X %04X %04X\r\n", i,
			read16(VI_SNAPSHOT + i + 0x0), read16(VI_SNAPSHOT + i + 0x2),
			read16(VI_SNAPSHOT + i + 0x4), read16(VI_SNAPSHOT + i + 0x6),
			read16(VI_SNAPSHOT + i + 0x8), read16(VI_SNAPSHOT + i + 0xA),
			read16(VI_SNAPSHOT + i + 0xC), read16(VI_SNAPSHOT + i + 0xE));
	}
	SwissVideoLogHiRes();
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

void SwissVideoLogPatchArea(u32 lowest)
{
	swv_log("SwissVideo:patch area used down to %08X%s\r\n", 0x80000000 | lowest,
		lowest < 0x1800 ? " - WARNING: below 80001800, the game's exception handlers overwrite it" : "");
#ifndef SWISSVIDEO_HOST_TEST
	viLogPending = 1;
#endif
}

void SwissVideoFlushVars(void)
{
	sync_after_write((void *)(MEM1_BASE + VAR_BLOCK_START), VAR_BLOCK_END - VAR_BLOCK_START);
}
