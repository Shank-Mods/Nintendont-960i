# Clock-aware Nintendont (for overclocked Wiis)

Upstream Nintendont assumes the stock 243 MHz Wii system clock everywhere. On a
console running with an overclocked system PLL (WiiOverclock / mini: ~315 MHz
Bollywood, 297 MHz Vegas) that makes every timer run fast. This branch reads the
real clock from the PLL registers and scales everything that depends on it.
**On a stock console (or Wii U / vWii) every value is bit-identical to before.**

## What is scaled

| Where | What | How |
|---|---|---|
| `kernel/global.h` | `TimerDiffTicks`, `TimerDiffSeconds`, `TicksToSecs` | raw `HW_TIMER` ticks (sysclk/128) are converted to "stock ticks" (`g_TickMul`), so every constant written for 1,898,437 ticks/s keeps its meaning: EXI IRQ rate, HID/PAD polling, fake disc seek/read speed, Bluetooth and USB timers, profiler logs |
| `kernel/common.c` | 32 bit timer wrap | the wrap period (2262 s at stock) follows the clock, so the RTC time handed to games stays right |
| `kernel/PatchTimers.c` | bus / CPU clock in low memory (`0xF8`/`0xFC`) and every timer constant patched into games (ints, floats, doubles, RADTimerRead, Majora's Mask, Panel de Pon) | stock 243 MHz values scaled by hz/243 MHz (or its inverse for 1/x constants) |
| `kernel/sock.c` | `SOPoll` timebase to ms | `/ (hz / 4000)` |
| `loader/source/main.c` | libogc's bus clock for the loader's own delays | reads the PLL once hardware access is open |

## Clock detection (`common/include/SysClockMath.h`)

* Bollywood (`HW_VERSION >= 0x20`): `hz = 54 MHz * (M - M0) / ((R+1) * O)` with `M = CLKF + fraction/16384`
  and `M0 = 1 + 1492/16384`. Fitted to the octest measurements (252/261/270/279/288 MHz measured
  against the VI clock) to within 0.0006%.
* Vegas: `27 MHz * (CLKF+1) * 2 / (CLKR+1) / CLKO`.
* Anything outside 200..420 MHz, or within 0.05% of 243 MHz, counts as stock.
* Note: the autoclock "315 MHz" Bollywood setting actually runs at ~315.73 MHz; Nintendont uses the real figure.

## Testing

`cc -O2 -o t common/test/sysclock_test.c -lm && ./t` (also run by CI) checks the clock model against the
octest data and every scaled constant against independent math.

The kernel logs `SysClock:<hz> Hz (...)` (and `PatchTimers:Using ...` when overclocked) to the
debug log; enable "Log to SD" in Nintendont's settings to read it.

## Not covered

* Games that hardcode their own timing without going through the patterns in `PatchTimers.c`.
* IOS networking above ~250 MHz (IOS, not Nintendont).
