/*
SysClockMath.h for Nintendont (clock-aware fork)

Pure integer helpers that work out the real Wii system clock from the PLL
registers and scale Nintendont's 243 MHz-based constants to match. No
hardware access in here, so it can be unit tested on a PC
(see kernel/test/sysclock_test.c).

Needs u32/u64 to be defined by the including file.

Everything here returns the exact stock values when the system clock is the
stock 243 MHz (within 0.05%), so a stock console behaves exactly like
upstream Nintendont.
*/
#ifndef _SYSCLOCKMATH_H_
#define _SYSCLOCKMATH_H_

#define SYSCLK_STOCK_HZ		243000000u
#define SYSCLK_MIN_HZ		200000000u
#define SYSCLK_MAX_HZ		420000000u

/* Hollywood registers (physical 0x0D8xxxxx; kernel adds nothing, PPC adds 0xC0000000) */
#define SYSCLK_REG_PLLSYS	0x0D8001B0
#define SYSCLK_REG_PLLSYSEXT	0x0D8001B4
#define SYSCLK_REG_VERSION	0x0D800214

/*
 * Work out the system clock in Hz from PLLSYS / PLLSYSEXT / HW_VERSION.
 *
 * PLLSYS:    CLKR bits 0-5, CLKF bits 6-17, CLKO bits 18-26
 * PLLSYSEXT: Bollywood only, 14 bit fraction at bit 9 (1/16384 units)
 *
 * Bollywood (HW_VERSION >= 0x20): measured against the VI clock by the
 * octest sweep, the system clock moves by exactly
 *     54 MHz / ((CLKR+1) * CLKO)  per 1.0 of (CLKF + fraction)
 * and sits at 243 MHz for the firmware's own setting of 109 + 1492/16384
 * (R=2, O=8). So the effective multiplier is (CLKF + fraction - M0) with
 * M0 = 1 + 1492/16384. (Within 0.02% of the octest measurements 252..288.)
 *
 * Vegas (HW_VERSION < 0x20): 27 MHz * (CLKF+1) * 2 / (CLKR+1) / CLKO,
 * integer only.
 *
 * Anything that does not look sane gives the stock clock.
 */
static inline u32 SysClkFromPLL(u32 pllsys, u32 pllsysext, u32 hwver)
{
	u32 r = pllsys & 0x3f;
	u32 f = (pllsys >> 6) & 0xfff;
	u32 o = (pllsys >> 18) & 0x1ff;
	u64 hz;
	u32 diff;

	if (o == 0)
		return SYSCLK_STOCK_HZ;

	if ((hwver & 0xff) >= 0x20)
	{
		u32 mult = (f << 14) | ((pllsysext >> 9) & 0x3fff);
		u32 m0 = (1u << 14) + 1492u;
		if (mult <= m0)
			return SYSCLK_STOCK_HZ;
		hz = ((u64)54000000u * (mult - m0)) / ((u64)(r + 1) * o * 16384u);
	}
	else
	{
		hz = ((u64)54000000u * (f + 1)) / ((u64)(r + 1) * o);
	}

	if (hz < SYSCLK_MIN_HZ || hz > SYSCLK_MAX_HZ)
		return SYSCLK_STOCK_HZ;
	/* within 0.05% of stock counts as stock */
	diff = (hz > SYSCLK_STOCK_HZ) ? (u32)(hz - SYSCLK_STOCK_HZ) : (u32)(SYSCLK_STOCK_HZ - hz);
	if (diff < SYSCLK_STOCK_HZ / 2000)
		return SYSCLK_STOCK_HZ;
	return (u32)hz;
}

/* Starlet HW_TIMER ticks at sysclk/128. Nintendont's constants assume 243 MHz,
 * so raw ticks are multiplied by this (Q16, 0x10000 = stock) to get "stock
 * ticks". */
static inline u32 SysClkTickMul(u32 hz)
{
	return (u32)(((u64)SYSCLK_STOCK_HZ << 16) / hz);
}

/* v * hz / stock: for quantities that grow with the clock (bus Hz, ticks/s) */
static inline u32 SysClkScale(u32 v, u32 hz)
{
	if (hz == SYSCLK_STOCK_HZ)
		return v;
	return (u32)((((u64)v * hz) + SYSCLK_STOCK_HZ / 2) / SYSCLK_STOCK_HZ);
}

/* v * stock / hz: for quantities that shrink with the clock (1/ticks, multipliers) */
static inline u32 SysClkScaleInv(u32 v, u32 hz)
{
	if (hz == SYSCLK_STOCK_HZ)
		return v;
	return (u32)((((u64)v * SYSCLK_STOCK_HZ) + hz / 2) / hz);
}

/* Multiply a positive IEEE single (given as bits) by num/den, round to nearest.
 * Integer only (the Starlet kernel is built soft-float and size limited). */
static inline u32 SysClkScaleFloatBits(u32 bits, u32 num, u32 den)
{
	u32 sign = bits & 0x80000000u;
	int e = (int)((bits >> 23) & 0xff);
	u64 m = (u64)((bits & 0x7fffff) | 0x800000);
	if (num == den || e == 0 || e == 0xff)
		return bits;
	m = (m * num + den / 2) / den;
	while (m >= (1u << 24)) { m = (m + 1) >> 1; e++; }
	while (m < (1u << 23)) { m <<= 1; e--; }
	return sign | ((u32)e << 23) | (u32)(m & 0x7fffff);
}

/* Same for an IEEE double (keeps the top 32 mantissa bits, ~2e-10 relative). */
static inline u64 SysClkScaleDoubleBits(u64 bits, u32 num, u32 den)
{
	u64 sign = bits & 0x8000000000000000ull;
	int e = (int)((bits >> 52) & 0x7ff);
	u64 man = (bits & 0xfffffffffffffull) | (1ull << 52);
	u64 m;
	if (num == den || e == 0 || e == 0x7ff)
		return bits;
	m = man >> 21;				/* 32 bit mantissa, top bit set */
	m = (m * num + den / 2) / den;
	while (m >= (1ull << 32)) { m = (m + 1) >> 1; e++; }
	while (m < (1ull << 31)) { m <<= 1; e--; }
	man = (m << 21) & 0xfffffffffffffull;
	return sign | ((u64)e << 52) | man;
}

#endif
