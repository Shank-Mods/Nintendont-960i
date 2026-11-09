/*
SysClock.c for Nintendont (clock-aware fork)

Works out the real system clock so a console running with an overclocked
system PLL (for example WiiOverclock/mini) gets correct timing: Starlet
timers, the time handed to games and the timer constants patched into games
are all derived from it. On a stock console nothing changes.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.
*/
#include "global.h"
#include "debug.h"
#include "SysClock.h"
#include "../common/include/SysClockMath.h"

u32 g_SysClockHz = SYSCLK_STOCK_HZ;
u32 g_TickMul = 0x10000;

void SysClockInit(void)
{
	u32 sys = read32(SYSCLK_REG_PLLSYS);
	u32 ext = read32(SYSCLK_REG_PLLSYSEXT);
	u32 ver = read32(SYSCLK_REG_VERSION);
	u32 hz = SYSCLK_STOCK_HZ;

	/* vWii has its own fixed clock tree, the Wii's PLL registers mean nothing there */
	if (!IsWiiU())
		hz = SysClkFromPLL(sys, ext, ver);

	g_SysClockHz = hz;
	g_TickMul = SysClkTickMul(hz);
	dbgprintf("SysClock:%u Hz (PLLSYS %08x EXT %08x VER %08x) tickmul %05x%s\r\n",
		hz, sys, ext, ver, g_TickMul, hz == SYSCLK_STOCK_HZ ? " [stock]" : " [OVERCLOCKED]");
}
