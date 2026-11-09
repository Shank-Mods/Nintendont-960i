#ifndef _SYSCLOCK_H_
#define _SYSCLOCK_H_

/* Read the PLL registers and set g_SysClockHz / g_TickMul (see global.h).
 * Safe to call more than once; must run before anything uses timers. */
void SysClockInit(void);

#endif
