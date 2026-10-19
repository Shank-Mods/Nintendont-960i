/*
Nintendont (Kernel) - Swiss video patch engine (field rendering / 960i)

Port of the video patching code of Swiss (https://github.com/emukidid/swiss-gc)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef __SWISSVIDEO_H__
#define __SWISSVIDEO_H__

#ifndef SWISSVIDEO_HOST_TEST
#include "global.h"
#endif

/* Returns true when the 960i mode is enabled and usable for this boot. */
bool SwissVideoEnabled(void);

/* Patches the loaded game executable (DOL/ELF image already in place). */
void SwissVideoPatch(char *Buffer, u32 Length);

/* Logs the VI registers captured by PADRead (diagnostics), call regularly. */
void SwissVideoLogVI(void);

/* Flushes the low memory variables the patches use; call before booting. */
void SwissVideoFlushVars(void);

#endif
