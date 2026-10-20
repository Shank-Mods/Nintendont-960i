# 960i mode (Swiss video patches)

Setting: **960i Field Render** (right column of the settings menu): Off, or
one of Swiss's "AVE Compatibility" modes (AVE-RVL, GCVideo, GCDigital,
CMPV-DOL, AVE N-DOL, AVE P-DOL). It is stored as `NIN_CFG_960I` (enable) plus
a 3 bit mode in `NIN_CFG_960I_COMPAT` (0 = AVE-RVL). On a Wii only AVE-RVL is
expected to give a picture; the others are there for experiments.

This forces GameCube games to use field rendering and outputs 960i
(1152i for the few PAL games Swiss runs at 50Hz). Each video field gets
its own full 480 line render instead of every frame being split into two
240 line fields. It is a port of the "Video Mode: 960i" + "Force Field
Rendering" options of [Swiss](https://github.com/emukidid/swiss-gc).

## Requirements

* An original Wii (not Wii U; its video path can't output 960i).
* Component cables with progressive scan enabled in the Wii settings.
* A display or scaler that accepts the Wii's 960i sync (see below).

If the Wii side requirements are missing the kernel silently falls back to the
normal video handling. Nintendont's own video options (Force Progressive,
forced video modes, Video Width, Screen Position) are ignored while 960i is
active. The setting is bit 20 of `NIN_CFG.Config`; the config version stays
at 10 so settings stay compatible with official Nintendont.

## Display compatibility (findings from hardware testing)

Tested on a Wii with component output, NTSC Kirby Air Ride:

* The VI ends up programmed exactly like Swiss's AVE-RVL mode: progressive
  flag (`DCR 0005`), 54 MHz clock, 1049 half-line frames
  (equ 12, prb 48/49, psb 5/4).
* The AVE-RVL only accepts 960i flagged as 480p and regenerates the sync
  itself. Per Extrems, the result is real 960i (the half line offsets the
  fields) but without proper equalization pulses. The tested monitor and two
  sync detectors could not hold vertical lock (picture rolls, only brief
  locks). Displays with more tolerant sync handling may work.
* Swiss's forced 480p and 480i modes work fine on the same setup, as do
  progressive frames of other whole-line lengths (526 lines). Flagging 960i
  as interlaced at 54 MHz gives no output at all. Swiss's pre-July-2026 960i
  timing (equ 6, prb 70/71, psb 1/0) behaves the same.

## How it works

`kernel/SwissVideo.c` hosts Swiss's `Patch_Video()` unmodified apart from a
few mechanical edits. It runs on the game executable right after it has been
loaded, before Nintendont's own pattern patching. The code Swiss uses for
960i on AVE-RVL (the Wii's video encoder) does the following:

* `__VIRetraceHandler` records the parity of the next field
  (`VIRetraceHandlerHook`), and `GXCopyDisp` latches it as the field being
  drawn (`GXCopyDispHook`).
* `GXSetViewport` is redirected to `GXSetViewportJitter` with that field.
  `__GXSetViewport` / `GXSetViewportJitter` are rewritten to offset the
  viewport by half a line on odd fields.
* `GXSetFieldMode` is forced on.
* `VIConfigure` is hooked (`VIConfigure960i` -> `VIConfigureFieldMode` ->
  `VIConfigureHook1`, chained by installing them back to back). This turns
  every render mode into a single field XFB mode shown in 960i timing, with
  `getTiming`'s table extended by Swiss's 960i timings.
* `OSGetProgressiveMode` / `OSGetEuRgb60Mode` / `__VIInit` are patched so the
  game starts in a progressive (31kHz) mode.

Low memory variables shared with the stubs live at `0x800009E0-0x800009FF`
(`reservedarea.h`). The PPC entry stub (`kernel/asm/FakeEntryLoad.S`)
invalidates that cache line before the game starts.

## Updating from Swiss

```
python3 kernel/swissvideo/port_patch_video.py <swiss-gc>/cube/swiss/source/patcher.c \
	kernel/swissvideo/Patch_Video.inc kernel/swissvideo/video_tables.inc
```

Then copy any changed stubs from `<swiss-gc>/cube/swiss/source/patches/{gx,vi}`
into `stubs/`, removing the trailing `*Length` / `*_size` labels. Sizes come
from bin2h instead.

Ported from swiss-gc commit `39a0867ec0350f152f6395d932cf4457d49eb825`.

## Experiments build

`make NIN960I_EXPERIMENTS=1` builds a version that reads two optional files
from the root of the boot device (templates in `ave/`):

* `nin960i_vi.txt`: Swiss settings (`gamevmode`, `forcevjitter`,
  `forcevfilter`, `avecompat`, `swiss=0` for a baseline) and 960i timing
  fields (`equ`, `prbodd`, `psbodd`, `nhlines`, ...).
* `nin960i_ave.txt`: raw AVE-RVL register writes (`RR=VV`). Register 00 bit 0
  shows the encoder's color bar test pattern; `01=23` is RVLoader's VGA mode.

Normal builds ignore both files.

## Diagnostics

Every 960i boot writes `/nin960i.log` to the boot device: settings, the
timing used, every Swiss signature found (`Found:[...]`) and ten snapshots
of the VI registers (captured by PADRead, logged ~3 s apart).

## Testing without a Wii

`test/` builds the same patcher as a PowerPC Linux program. It loads a
`main.dol`, runs the patcher and prints every function Swiss recognised and
every word it changed:

```
make -C kernel/swissvideo                  # stub headers (devkitPPC)
make -C kernel/swissvideo/test CC=powerpc-linux-gnu-gcc
qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALE      # NTSC
qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALP P    # PAL
```

## Not ported (yet)

* `Patch_GameSpecificVideo()`, Swiss's per-game fixes for games whose own
  code breaks with forced video modes. Its offsets are DOL file offsets
  and need converting to load addresses.
* The other Swiss video modes (240p, 1080i, 540p, ...) and options (TAA,
  horizontal scaling, vertical filter selection). `Patch_Video()` already
  supports them; only settings and menu entries would be needed.
