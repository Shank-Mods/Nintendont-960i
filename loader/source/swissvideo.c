/*
	Swiss video settings (loader side).

	Stores the "Swiss Video" settings page in /nincfg_swiss.bin and, when a
	game is booted, works out the values the kernel's Swiss video patches use
	(a port of Swiss' config_load_current(), cube/swiss/source/config/config.c).

	Swiss is by emu_kidid, Extrems and contributors:
	https://github.com/emukidid/swiss-gc

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation version 2.
*/
#include <gccore.h>
#include <string.h>
#include "global.h"
#include "exi.h"
#include "ff_utf8.h"
#include "swissvideo.h"

#define SWISS_CFG_FILE	"/nincfg_swiss.bin"

SWISS_CFG *const swcfg = (SWISS_CFG*)SWISS_CFG_ADDR_PPC;
static SWISS_BOOT *const swboot = (SWISS_BOOT*)SWISS_BOOT_ADDR_PPC;

/* False if the file exists but couldn't be read: don't overwrite it with
   defaults unless the user changes a Swiss video setting. */
static bool swcfg_writable = false;

const char *const SwissVModeStr[SWISS_VMODE_PAL_MAX] = {
	"Auto", "480i", "480sf", "240p", "960i", "480p", "1080i60", "540p60",
	"576i", "576sf", "288p", "1152i", "576p", "1080i50", "540p50"
};
const char *const SwissAveCompatStr[SWISS_AVE_COMPAT_MAX] = {
	"AVE N-DOL", "AVE P-DOL", "CMPV-DOL", "GCDigital", "GCVideo", "AVE-RVL"
};
const char *const SwissHScaleStr[9] = {
	"Auto", "1:1", "11:10", "9:8", "640px", "656px", "672px", "704px", "720px"
};
const char *const SwissVFilterStr[4] = { "Auto", "0", "1", "2" };
const char *const SwissVJitterStr[4] = { "Auto", "On", "Off", "TAA" };
const char *const SwissPixelCenterStr[3] = { "No", "1/24", "1/12" };
const char *const SwissWidescreenStr[3] = { "No", "3D", "2D+3D" };
const char *const SwissDTVStatusStr[3] = { "No", "Yes", "Region" };

#define in_range(x, a, b)	((x) >= (a) && (x) <= (b))

void SwissCfgDefaults(void)
{
	memset(swcfg, 0, sizeof(SWISS_CFG));
	swcfg->Magic = SWISS_CFG_MAGIC;
	swcfg->Version = SWISS_CFG_VERSION;
	swcfg->Enabled = 0;
	/* The Wii's video encoder. (Swiss defaults to GCVideo on a GameCube.) */
	swcfg->AveCompat = SWISS_AVE_RVL;
	/* Everything else defaults to Swiss' "Auto" / "No" (0). */
}

/* Clamp values from the file (or memory) to the ranges the menu offers. */
static void SwissCfgSanitize(void)
{
	if (swcfg->AveCompat >= SWISS_AVE_COMPAT_MAX)	swcfg->AveCompat = SWISS_AVE_RVL;
	if (swcfg->ForceDTVStatus > 2)			swcfg->ForceDTVStatus = 0;
	if (swcfg->VideoModeNtsc >= SWISS_VMODE_NTSC_MAX)	swcfg->VideoModeNtsc = 0;
	if (swcfg->VideoModePal >= SWISS_VMODE_PAL_MAX)	swcfg->VideoModePal = 0;
	if (swcfg->ForceHScale > 8)			swcfg->ForceHScale = 0;
	if (swcfg->ForceVOffset < -99 || swcfg->ForceVOffset > 99)	swcfg->ForceVOffset = 0;
	if (swcfg->ForceVFilter > 3)			swcfg->ForceVFilter = 0;
	if (swcfg->ForceVJitter > 3)			swcfg->ForceVJitter = 0;
	if (swcfg->FixPixelCenter > 2)			swcfg->FixPixelCenter = 0;
	if (swcfg->ForceWidescreen > 2)			swcfg->ForceWidescreen = 0;
	swcfg->Enabled = !!swcfg->Enabled;
	swcfg->RT4KOptim = !!swcfg->RT4KOptim;
	swcfg->ForceVideoActive = !!swcfg->ForceVideoActive;
	swcfg->DisableDithering = !!swcfg->DisableDithering;
	swcfg->ForceAnisotropy = !!swcfg->ForceAnisotropy;
	swcfg->InvertFieldOrder = !!swcfg->InvertFieldOrder;
	if (swcfg->HiRes240p > 2)			swcfg->HiRes240p = 0;
}

bool SwissCfgLoad(void)
{
	bool loaded = false, save = false;
	FIL f;
	UINT read = 0;

	FRESULT res = f_open_char(&f, SWISS_CFG_FILE, FA_READ|FA_OPEN_EXISTING);
	if (res == FR_OK)
	{
		f_read(&f, swcfg, sizeof(SWISS_CFG), &read);
		f_close(&f);
		loaded = (read == sizeof(SWISS_CFG) &&
			  swcfg->Magic == SWISS_CFG_MAGIC &&
			  swcfg->Version == SWISS_CFG_VERSION);
	}
	if (!loaded)
	{
		SwissCfgDefaults();
		// Only (re)create the file if it is missing or unusable, not if
		// the device couldn't be read at all.
		save = (res == FR_OK || res == FR_NO_FILE);
	}
	swcfg_writable = loaded || save;

	/*
		Early 960i test builds kept an on/off bit and an AVE compatibility
		mode in ncfg->Config. Turn that into the equivalent settings here
		(960i for NTSC and PAL games) the first time, then clear the bits so
		nincfg.bin only holds Nintendont's own settings again.
	*/
	if (ncfg->Config & (NIN_CFG_960I_LEGACY | NIN_CFG_960I_COMPAT_LEGACY))
	{
		if (!loaded && (ncfg->Config & NIN_CFG_960I_LEGACY))
		{
			static const u8 legacyCompat[6] = {
				SWISS_AVE_RVL, SWISS_GCVIDEO, SWISS_GCDIGITAL,
				SWISS_CMPV_DOL, SWISS_AVE_N_DOL, SWISS_AVE_P_DOL
			};
			u32 c = (ncfg->Config & NIN_CFG_960I_COMPAT_LEGACY) >> NIN_CFG_BIT_960I_COMPAT_LEGACY;
			swcfg->Enabled = 1;
			swcfg->AveCompat = legacyCompat[c < 6 ? c : 0];
			swcfg->VideoModeNtsc = 4;	/* 960i */
			swcfg->VideoModePal = 4;
		}
		ncfg->Config &= ~(NIN_CFG_960I_LEGACY | NIN_CFG_960I_COMPAT_LEGACY);
		save = swcfg_writable;
	}

	SwissCfgSanitize();
	DCFlushRange(swcfg, sizeof(SWISS_CFG));
	return save;
}

void SwissCfgChanged(void)
{
	swcfg_writable = true;
}

void SwissCfgSave(void)
{
	FIL f;
	UINT wrote;

	if (!swcfg_writable)
		return;
	SwissCfgSanitize();
	if (f_open_char(&f, SWISS_CFG_FILE, FA_WRITE|FA_OPEN_ALWAYS) != FR_OK)
		return;
	f_write(&f, swcfg, sizeof(SWISS_CFG), &wrote);
	f_truncate(&f);
	f_close(&f);
}

bool SwissVideoAvailable(void)
{
	/* The Wii U's video path only scales a 480i/480p image. */
	return !IsWiiU();
}

bool SwissDTVStatus(void)
{
	/* Swiss' getDTVStatus() (video.c) */
	if (in_range(swcfg->AveCompat, SWISS_AVE_N_DOL, SWISS_AVE_P_DOL))
		return false;
	if (in_range(swcfg->AveCompat, SWISS_GCDIGITAL, SWISS_GCVIDEO) && swcfg->RT4KOptim)
		return true;
	if (swcfg->ForceDTVStatus == 1)
		return true;
	if (swcfg->ForceDTVStatus > 1)	/* Region Switch: Japanese consoles */
		return CONF_GetRegion() == CONF_REGION_JP;
	/* On a Wii: component cable connected and progressive scan enabled */
	return (CONF_GetProgressiveScan() > 0) && VIDEO_HaveComponentCable();
}

bool SwissVModeAllowed(int mode)
{
	/* Swiss' settings menu skips these (gui/settings.c) */
	bool progressive = in_range(mode, 4, 7) || in_range(mode, 11, 14);
	bool hd = in_range(mode, 6, 7) || in_range(mode, 13, 14);
	if (progressive && !SwissDTVStatus())
		return false;
	if (hd && swcfg->AveCompat != SWISS_CMPV_DOL)
		return false;
	return true;
}

/* Games Swiss runs in the 50Hz variant of the selected mode (960i -> 1152i) */
static const char gameVModePalEntries[][4] = {
	"DLSP", "G3FD", "G3FF", "G3FP", "G3FS", "GLRD", "GLRF", "GLRP",
	"GM8P", "GSWD", "GSWF", "GSWI", "GSWP", "GSWS"
};
/* Swiss' per game defaults (config_defaults()), the game ID's 5th byte is the value */
static const char fixPixelCenterEntries[][5] = {
	"00\0E\1", "DNDD\1", "G2BE\1", "G2BP\1", "GD7E\1", "GD7P\1", "GEME\1",
	"GEMJ\1", "GEMP\1", "GNBE\1", "GNBJ\1", "GNBP\1", "GZBJ\1"
};
static const char forceAnisotropyEntries[][5] = {
	"GADE\0", "GAEE\0", "GAEJ\0", "GAFE\0", "GAFJ\0", "GAFP\0", "GAFU\0",
	"GM8E\0", "GM8J\0", "GM8P\0", "PGSE\0", "PKBJ\0", "PZLE\0", "PZLJ\0", "PZLP\0"
};

void SwissVideoPrepareBoot(u32 BI2region, u32 GameID, bool allowed)
{
	char game_id[4];
	u32 i;
	SWISS_BOOT b;

	memset(swboot, 0, sizeof(SWISS_BOOT));
	if (!allowed || !swcfg->Enabled || !SwissVideoAvailable())
	{
		DCFlushRange(swboot, sizeof(SWISS_BOOT));
		return;
	}

	game_id[0] = GameID >> 24;
	game_id[1] = GameID >> 16;
	game_id[2] = GameID >> 8;
	game_id[3] = GameID;

	memset(&b, 0, sizeof(b));
	b.aveCompat = swcfg->AveCompat;
	b.forceDTVStatus = swcfg->ForceDTVStatus;
	b.rt4kOptim = swcfg->RT4KOptim;
	b.forceVideoActive = swcfg->ForceVideoActive;
	b.forceHScale = swcfg->ForceHScale;
	b.forceVOffset = swcfg->ForceVOffset;
	b.forceVFilter = swcfg->ForceVFilter;
	b.forceVJitter = swcfg->ForceVJitter;
	b.fixPixelCenter = swcfg->FixPixelCenter;
	b.disableDithering = swcfg->DisableDithering;
	b.forceAnisotropy = swcfg->ForceAnisotropy;
	b.forceWidescreen = swcfg->ForceWidescreen;
	b.invertFieldOrder = swcfg->InvertFieldOrder;

	/* config_defaults(): a few games have their own defaults */
	for (i = 0; i < sizeof(fixPixelCenterEntries) / sizeof(fixPixelCenterEntries[0]); i++) {
		if (!memcmp(game_id, fixPixelCenterEntries[i], 4)) {
			b.fixPixelCenter = fixPixelCenterEntries[i][4];
			break;
		}
	}
	for (i = 0; i < sizeof(forceAnisotropyEntries) / sizeof(forceAnisotropyEntries[0]); i++) {
		if (!memcmp(game_id, forceAnisotropyEntries[i], 4)) {
			b.forceAnisotropy = forceAnisotropyEntries[i][4];
			break;
		}
	}

	/* config_load_current() from here on */
	int gameVMode = (BI2region == BI2_REGION_PAL) ? swcfg->VideoModePal : swcfg->VideoModeNtsc;

	/*
		1280x240p experiment (not in Swiss): NTSC games only, built on the
		480p path (54 MHz progressive VI, A/V encoder in 480p mode).
	*/
	int hiRes = (BI2region != BI2_REGION_PAL) ? swcfg->HiRes240p : 0;
	if (hiRes)
		gameVMode = 5;
	b.sramVideo = (BI2region == BI2_REGION_PAL) ? 1 /* SYS_VIDEO_PAL */ : 0 /* SYS_VIDEO_NTSC */;

	if (gameVMode > 0)
	{
		b.sram60Hz = in_range(gameVMode, 1, 7);
		b.sramProgressive = in_range(gameVMode, 4, 7) || in_range(gameVMode, 11, 14);

		if (b.sram60Hz) {
			for (i = 0; i < sizeof(gameVModePalEntries) / sizeof(gameVModePalEntries[0]); i++) {
				if (!memcmp(game_id, gameVModePalEntries[i], 4)) {
					gameVMode += 7;
					break;
				}
			}
		}

		if (b.sramProgressive && !SwissDTVStatus())
			gameVMode = 0;

		if (b.sramVideo == 1)
			b.sramProgressive &= b.sram60Hz;
		else
			b.sram60Hz = 0;
	}
	/*
		Swiss' "Auto (Progressive)" (gameVMode -1/-2) depends on the mode
		Swiss' own menu runs in. Here Auto always means the game's own video
		mode; pick 480p to force progressive scan.
	*/

	if (!memcmp(game_id, "GB3E", 4))	/* BMX XXX */
		b.sramProgressive = 0;

	switch (gameVMode) {
		case 2: case 9: case 5: case 12:
			if (!b.forceVFilter)
				b.forceVFilter = 1;
			break;
		case 3: case 10:
			b.forceVOffset &= ~1;
			if (!b.forceVFilter)
				b.forceVFilter = 1;
			if (!b.forceVJitter)
				b.forceVJitter = 2;
			break;
		case 4: case 11:
			b.forceVOffset &= ~1;
			if (!b.forceVFilter)
				b.forceVFilter = 1;
			if (!b.forceVJitter)
				b.forceVJitter = 1;
			break;
		case 6: case 13:
			if (!b.forceHScale)
				b.forceHScale = 1;
			b.forceVOffset &= ~1;
			if (!b.forceVFilter)
				b.forceVFilter = 1;
			if (!b.forceVJitter)
				b.forceVJitter = 1;
			break;
		case 7: case 14:
			if (!b.forceHScale)
				b.forceHScale = 1;
			b.forceVOffset &= ~1;
			if (!b.forceVFilter)
				b.forceVFilter = 1;
			break;
	}

	if (hiRes && gameVMode == 5) {
		/* options that would fight with the 1280x240p setup */
		b.hiRes240p = hiRes;
		b.forceVJitter = 2;		/* field rendering off */
		b.fixPixelCenter = 0;
		b.forceHScale = 0;
		b.forceVOffset = 0;
		b.forceWidescreen = 0;
		b.invertFieldOrder = 0;
	}

	b.gameVMode = gameVMode;
	b.Magic = SWISS_BOOT_MAGIC;
	memcpy(swboot, &b, sizeof(b));
	DCFlushRange(swboot, sizeof(SWISS_BOOT));
	gprintf("SwissVideo: gameVMode %d (%s), aveCompat %s, 60Hz %d, progressive %d, 1280x240p %d\r\n",
		b.gameVMode, SwissVModeStr[b.gameVMode], SwissAveCompatStr[b.aveCompat],
		b.sram60Hz, b.sramProgressive, b.hiRes240p);
}

const SWISS_BOOT *SwissVideoBootInfo(void)
{
	return (swboot->Magic == SWISS_BOOT_MAGIC) ? swboot : NULL;
}
