/* Host test for common/include/SysClockMath.h:  cc -O2 -Wall -o t sysclock_test.c && ./t */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef uint32_t u32; typedef uint64_t u64;
#include "../include/SysClockMath.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static u32 f2b(float f) { u32 b; memcpy(&b, &f, 4); return b; }
static u64 d2b(double d) { u64 b; memcpy(&b, &d, 8); return b; }
static double b2d(u64 b) { double d; memcpy(&d, &b, 8); return d; }

/* the stock constants from kernel/PatchTimers.c */
static const u32 U_BUS = 0x0e7be2c0, U_CPU = 0x2b73a840, U_SECS = 0x039ef8b0, U_60 = 0x000f7314,
	U_MS = 0x0000ed4e, U_RAD = 0x8a15866c;
static const u32 F_BUS = 0x4d67be2c, F_CPU = 0x4e2dcea1, F_SECS = 0x4c67be2c, F_MS = 0x476d4e00,
	F_1S = 0x328d65ea, F_1MS = 0x378a1586, F_1200 = 0x37a5b36e;
static const u64 D_0_7716 = 0x3fe8b0f27bb2fec5ull, D_1_1574 = 0x3ff284b5dcc63f14ull;

int main(void)
{
	u32 stock_sys = 0x00201b42, stock_ext = 0x080ba80c;

	/* 1. Stock registers (from the octest handoff) -> exactly stock */
	CHECK(SysClkFromPLL(stock_sys, stock_ext, 0x21) == 243000000u, "stock Bollywood");
	CHECK(SysClkFromPLL((0x47 << 6) | (0x10 << 18), 0, 0x11) == 243000000u, "stock Vegas");

	/* 2. Octest measurements: rebuild the registers octest wrote (proportional
	 *    scaling of the firmware multiplier) and compare with the measured MHz. */
	{
		double tgt[] = { 252, 261, 270, 279, 288 };
		double meas[] = { 252.090, 261.181, 270.272, 279.362, 288.453 };
		u32 stock_m = (109u << 14) | 1492u;
		for (int i = 0; i < 5; i++) {
			u32 m = (u32)(((u64)stock_m * (u32)(tgt[i] * 1000) + 121500) / 243000);
			u32 sys = 2 | (((m >> 14) & 0xfff) << 6) | (8u << 18);
			u32 ext = (stock_ext & ~0x007ffe00u) | ((m & 0x3fff) << 9);
			double hz = SysClkFromPLL(sys, ext, 0x21) / 1e6;
			printf("octest %.0f MHz step: model %.3f measured %.3f (%.4f%%)\n", tgt[i], hz, meas[i], 100 * (hz - meas[i]) / meas[i]);
			CHECK(hz > meas[i] * 0.9998 && hz < meas[i] * 1.0002, "model off at %.0f", tgt[i]);
		}
	}

	/* 3. What our autoclock build programs for "315" / "297" */
	{
		u32 stock_m = (109u << 14) | 1492u;
		u32 m = (u32)(((u64)stock_m * 315000 + 121500) / 243000);
		u32 sys = 2 | (((m >> 14) & 0xfff) << 6) | (8u << 18);
		u32 ext = (stock_ext & ~0x007ffe00u) | ((m & 0x3fff) << 9);
		printf("autoclock Bollywood 315 -> %u Hz\n", SysClkFromPLL(sys, ext, 0x21));
		CHECK(sys == 0x00202342, "pllsys 315 = %08x", sys);
		u32 v = (0x57u << 6) | (0x10u << 18);
		printf("autoclock Vegas 297 -> %u Hz\n", SysClkFromPLL(v, 0, 0x11));
		CHECK(SysClkFromPLL(v, 0, 0x11) == 297000000u, "vegas 297");
	}

	/* 4. Garbage must give stock */
	CHECK(SysClkFromPLL(0, 0, 0x21) == 243000000u, "zero pll");
	CHECK(SysClkFromPLL(0xffffffff, 0xffffffff, 0x21) == 243000000u, "all ones");

	/* 5. Scaling at stock is the identity */
	CHECK(SysClkScale(U_BUS, 243000000u) == U_BUS && SysClkScaleInv(U_RAD, 243000000u) == U_RAD, "identity int");
	CHECK(SysClkScaleFloatBits(F_BUS, 243000000u, 243000000u) == F_BUS, "identity float");
	CHECK(SysClkTickMul(243000000u) == 0x10000, "tickmul stock");

	/* 6. Scaled constants vs independent double math at several clocks */
	u32 hzs[] = { 252000000u, 297000000u, 315000000u, 315727000u, 324000000u };
	for (int i = 0; i < 5; i++) {
		u32 hz = hzs[i]; double r = hz / 243e6;
		u32 bus = SysClkScale(U_BUS, hz);
		CHECK(llabs((long long)bus - (long long)hz) <= 1, "bus %u vs %u", bus, hz);
		CHECK(llabs((long long)SysClkScale(U_CPU, hz) - 3LL * hz) <= 3, "cpu");
		CHECK(llabs((long long)SysClkScale(U_SECS, hz) - (long long)(hz / 4)) <= 1, "secs");
		CHECK(llabs((long long)SysClkScale(U_60, hz) - (long long)(hz / 240)) <= 1, "60hz");
		CHECK(llabs((long long)SysClkScale(U_MS, hz) - (long long)(hz / 4000)) <= 1, "ms");
		/* floats vs the value computed straight from hz (within 1 ulp) */
		#define NEAR(a, b) (llabs((long long)(a) - (long long)(b)) <= 1)
		CHECK(NEAR(SysClkScaleFloatBits(F_BUS, hz, 243000000u), f2b((float)hz)), "fbus %08x vs %08x", SysClkScaleFloatBits(F_BUS, hz, 243000000u), f2b((float)hz));
		CHECK(NEAR(SysClkScaleFloatBits(F_CPU, hz, 243000000u), f2b((float)(3.0 * hz))), "fcpu");
		CHECK(NEAR(SysClkScaleFloatBits(F_SECS, hz, 243000000u), f2b((float)(hz / 4.0))), "fsecs");
		CHECK(NEAR(SysClkScaleFloatBits(F_MS, hz, 243000000u), f2b((float)(hz / 4000.0))), "fms");
		CHECK(NEAR(SysClkScaleFloatBits(F_1S, 243000000u, hz), f2b((float)(4.0 / hz))), "f1s");
		CHECK(NEAR(SysClkScaleFloatBits(F_1MS, 243000000u, hz), f2b((float)(4000.0 / hz))), "f1ms");
		CHECK(NEAR(SysClkScaleFloatBits(F_1200, 243000000u, hz), f2b((float)(4800.0 / hz))), "f1200");
		/* RAD multiplier and the Majora's Mask double both scale by 1/ratio */
		double rad_exp = (double)U_RAD / r;
		CHECK(fabs((double)SysClkScaleInv(U_RAD, hz) - rad_exp) < 1.0, "rad");
		double dnew = b2d(SysClkScaleDoubleBits(D_0_7716, 243000000u, hz));
		CHECK(fabs(dnew / (b2d(D_0_7716) / r) - 1) < 1e-8, "dbl %.9f", dnew);
	}
	/* stock constants must be self consistent with the formulas used above */
	CHECK(NEAR(F_BUS, f2b(243000000.0f)), "stock fbus formula");
	CHECK(NEAR(F_CPU, f2b(729000000.0f)), "stock fcpu formula");
	CHECK(NEAR(F_SECS, f2b(60750000.0f)), "stock fsecs formula");
	CHECK(NEAR(F_MS, f2b(60750.0f)), "stock fms formula");
	CHECK(NEAR(F_1S, f2b((float)(1.0 / 60750000.0))), "stock f1s formula");
	CHECK(NEAR(F_1MS, f2b((float)(1.0 / 60750.0))), "stock f1ms formula");
	CHECK(NEAR(F_1200, f2b((float)(1.0 / 50625.0))), "stock f1200 formula");
	CHECK(NEAR(D_0_7716, d2b(b2d(D_1_1574) / 1.5)), "stock dbl formula %016llx", (unsigned long long)d2b(b2d(D_1_1574) / 1.5));
	CHECK(U_BUS == 243000000u && U_CPU == 729000000u && U_SECS == 60750000u && U_60 == 1012500u && U_MS == 60750u, "stock ints");

	printf(fails ? "\n%d FAILURES\n" : "\nall ok\n", fails);
	return fails != 0;
}
