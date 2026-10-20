/*
	Swiss video settings (loader side). See common/include/SwissVideoConfig.h.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation version 2.
*/
#ifndef __SWISSVIDEO_LOADER_H__
#define __SWISSVIDEO_LOADER_H__

#include <gctypes.h>
#include "Config.h"

/* The settings being edited, in MEM2 next to ncfg. */
extern SWISS_CFG *const swcfg;

/* Value names, in Swiss' order (gui/settings.c) */
extern const char *const SwissVModeStr[SWISS_VMODE_PAL_MAX];
extern const char *const SwissAveCompatStr[SWISS_AVE_COMPAT_MAX];
extern const char *const SwissHScaleStr[9];
extern const char *const SwissVFilterStr[4];
extern const char *const SwissVJitterStr[4];
extern const char *const SwissPixelCenterStr[3];
extern const char *const SwissWidescreenStr[3];
extern const char *const SwissDTVStatusStr[3];

/** Set swcfg to the defaults. */
void SwissCfgDefaults(void);

/**
 * Load /nincfg_swiss.bin from the boot device, or use the defaults.
 * Also moves settings of early 960i test builds out of ncfg.
 * @return True if the file should be (re)written.
 */
bool SwissCfgLoad(void);

/** Call when the user changed a Swiss video setting. */
void SwissCfgChanged(void);

/**
 * Write swcfg to /nincfg_swiss.bin on the boot device. Skipped if the
 * file couldn't be read and nothing was changed, so it isn't replaced
 * with the defaults.
 */
void SwissCfgSave(void);

/** True if this console can use the Swiss video patches at all (not Wii U). */
bool SwissVideoAvailable(void);

/** Swiss' getDTVStatus(): can progressive (EDTV) modes be selected? */
bool SwissDTVStatus(void);

/** True if Swiss would let the user pick this video mode right now. */
bool SwissVModeAllowed(int mode);

/**
 * Work out the settings for the game being booted (Swiss'
 * config_load_current()) and pass them to the kernel.
 * @param BI2region	Game region (bi2.bin).
 * @param GameID	Game ID (first 4 characters).
 * @param allowed	False to boot without the Swiss video patches.
 */
void SwissVideoPrepareBoot(u32 BI2region, u32 GameID, bool allowed);

/** Settings for the game being booted, or NULL if the patches are off. */
const SWISS_BOOT *SwissVideoBootInfo(void);

#endif
