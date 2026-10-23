# Swiss video patches

A port of the video options of [Swiss](https://github.com/emukidid/swiss-gc)
(by emu_kidid, Extrems and contributors) to Nintendont: forced video modes
(240p, 480p, 960i, 1080i, ...), forced field rendering, vertical filters,
horizontal scaling, vertical offset, pixel center fix, alpha dithering,
anisotropic filtering and widescreen.

## Settings

Settings menu, right column: **Swiss Video** (press A) opens its own page.

| Setting | Values (Swiss numbering) |
|---|---|
| Swiss Video Patches | Off / On: master switch |
| AVE Compatibility | AVE N-DOL, AVE P-DOL, CMPV-DOL, GCDigital, GCVideo, AVE-RVL (default) |
| Force DTV Status | No, Yes, Region (Switch); not for AVE N-DOL / P-DOL |
| RetroTINK-4K Input | No / Yes; GCDigital and GCVideo only |
| Force Video Active | No / Yes |
| NTSC Video Mode | Auto, 480i, 480sf, 240p, 960i, 480p, 1080i60, 540p60 |
| PAL Video Mode | the above plus 576i, 576sf, 288p, 1152i, 576p, 1080i50, 540p50 |
| Horizontal Scale | Auto, 1:1, 11:10, 9:8, 640px, 656px, 672px, 704px, 720px |
| Vertical Offset | -99 to +99 lines |
| Vertical Filter | Auto, 0, 1, 2 |
| Field Rendering | Auto, On, Off, TAA |
| Invert Field Order | No / Yes (not a Swiss option, see below) |
| 1280x240p (exp.) | Off / Left half / Two-pass (experiment, see below) |
| Fix Pixel Center | No, 1/24, 1/12 |
| Disable Dithering | No / Yes |
| Anisotropic Filter | No / Yes |
| Force Widescreen | No, 3D, 2D+3D |

Like in Swiss, progressive modes (960i, 480p, 1080i, 540p, 1152i, 576p) can
only be picked with DTV status (component cable + progressive scan in the
Wii settings, or Force DTV Status), and 1080i/540p only with CMPV-DOL.
Settings that don't apply to the chosen compatibility mode can't be changed.
While the master switch is on, Nintendont's own Force Progressive, Video
Mode, Video Width and Screen Position are ignored, and Force Widescreen is
replaced by Swiss's when that is set. Not available on Wii U.

Only 960i, 480p and 480i have been tested on a Wii so far (AVE-RVL). The
other modes are exposed as on a GameCube and are untested.

## Where the settings live

* `/nincfg_swiss.bin` on the boot device holds `SWISS_CFG`
  (`common/include/SwissVideoConfig.h`). `nincfg.bin` is unchanged, so it
  stays compatible with official Nintendont. Settings of the early 960i test
  builds (bits 20-23 of `NIN_CFG.Config`) are moved over once and cleared.
* When a game boots, the loader (`loader/source/swissvideo.c`) works out the
  values for that game, as Swiss's `config_load_current()` does: NTSC or PAL
  mode by region, the 50Hz variant for Swiss's PAL-only games (960i becomes
  1152i), no progressive modes without DTV status, the per-mode "Auto"
  defaults, and Swiss's per-game pixel center / anisotropy defaults. The
  result (`SWISS_BOOT`) is placed at `0x93004840`, next to `NIN_CFG`, for
  the kernel.
* The loader also starts the game in the forced mode (Swiss's
  `ogc_video__reset()`), which on a Wii sets up the A/V encoder, and sets the
  SRAM progressive / PAL60 flags. "Auto" keeps the game's own video mode;
  Swiss's "Auto (Progressive)" isn't offered, pick 480p instead.

## How it works

`kernel/SwissVideo.c` hosts Swiss's `Patch_Video()`, `Patch_Widescreen()` and
`Patch_TexFilt()`, unmodified apart from a few mechanical edits. They run on
the game executable right after it has been loaded, before Nintendont's own
pattern patching (whose video patches are skipped). For 960i on AVE-RVL (the
Wii's video encoder) Swiss's code does the following:

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

**Invert Field Order** is a Nintendont addition. Swiss's forced field
rendering renders each frame for the parity `VIRetraceHandlerHook` predicts
(`VAR_NEXT_FIELD = !current field`, latched by `GXCopyDispHook`). Games that
show their frames a field later than that (triple buffering, CPU running a
frame ahead of the GPU, ...) end up with every field rendered for the wrong
parity. With the option on, the kernel replaces the hook's `xori r3,r3,1`
with a `nop` after patching, which inverts the parity for every forced field
rendering path. Games that pass their own field to `GXSetViewportJitter`,
and 480sf (which latches the field in `VISetRegs`), are not affected.

Force Widescreen hooks `C_MTXFrustum` / `C_MTXPerspective` (and their light
variants, `GXSetProjection` as a fallback; `C_MTXOrtho` and `GXSetScissor`
for 2D+3D). Force Anisotropic Filter hooks `GXInitTexObjLOD`.

Low memory variables shared with the stubs live at `0x800009DC-0x800009FF`
(`reservedarea.h`, Swiss's layout except `VAR_RMODE`). The PPC entry stub
(`kernel/asm/FakeEntryLoad.S`) invalidates those cache lines before the game
starts.

## 1280x240p two-pass experiment (not in Swiss)

Renders NTSC games at 1280x240 and outputs 1280x240p: 15.7 kHz lines at the
54 MHz VI clock, so 1280 pixels per line fit. Built on Swiss's 480p path
(progressive boot, encoder in 480p mode), first tuned for Kirby Air Ride.

**VI** (`stubs/VIConfigure1280x240p.S`, `video_timing_1280x240p`): replaces
Swiss's 480p mode stub. Sets `fbWidth` 1280 (the VI only uses it for the
fetch width), `viWidth` 640 / `viXOrigin` 80 and 240 lines; the timing is
NTSC 240p vertically (263 lines) with every horizontal value doubled
(half-line 858, sync 127). Active window: clocks 295-1575.

**GX** (`hires/`, a position independent PowerPC payload in the patch area;
the build links it at two addresses and fails unless the blobs match):

* `GXSetViewport`: a 640x480 viewport becomes 1280x240 (x doubled, y halved;
  projection unchanged).
* `GXSetScissor`: the full screen scissor is limited to the left 640 columns
  (the EFB is 640 wide), followed by a scissor offset write; the FIFO write
  pointer is recorded.
* `GXCopyDisp`: copies the left half (copy registers in `__GXData` set to
  640x240, 1280 pixel stride), waits until the GPU has read the FIFO, then
  patches the frame's recorded scissors to the right half with offset 640,
  turns the copy into a right half copy, points the CP read pointer back at
  the start of the frame and lets the GPU run it again. A NOP run marks
  each frame start in the FIFO.

"Left half" skips the second pass (right half shows the clear colour), to
test the signal and GX setup separately. `nin960i.log` gets counters
(replays, skipped frames and why, FIFO size, largest frame, scissor and
viewport values seen). Limits: needs the frame to fit in the game's FIFO;
render-to-texture passes run again in the second pass; full screen post
effects drawn from EFB copies will be wrong in one half; GXSetScissor must
be found exactly once.

## Display compatibility (960i, findings from hardware testing)

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
* GCVideo and CMPV-DOL compatibility gave a black screen on a stock Wii.

## Updating from Swiss

```
python3 kernel/swissvideo/port_patch_video.py <swiss-gc>/cube/swiss/source/patcher.c \
	kernel/swissvideo/Patch_Video.inc kernel/swissvideo/video_tables.inc \
	kernel/swissvideo/Patch_Extras.inc
```

Then copy any changed stubs from
`<swiss-gc>/cube/swiss/source/patches/{gx,mtx,vi}` into `stubs/`, removing
the trailing `*Length` / `*_size` labels. Sizes come from bin2h instead. If
Swiss's `config_load_current()` changes, update `SwissVideoPrepareBoot()` in
the loader.

Ported from swiss-gc commit `39a0867ec0350f152f6395d932cf4457d49eb825`.

## Experiments build

`make NIN960I_EXPERIMENTS=1` builds a version that reads two optional files
from the root of the boot device (templates in `ave/`):

* `nin960i_vi.txt`: Swiss settings (`gamevmode`, `forcevjitter`,
  `forcevfilter`, `avecompat`, `swiss=0` for a baseline) and 960i timing
  fields (`equ`, `prbodd`, `psbodd`, `nhlines`, ...). These override the
  settings page.
* `nin960i_ave.txt`: raw AVE-RVL register writes (`RR=VV`). Register 00 bit 0
  shows the encoder's color bar test pattern; `01=23` is RVLoader's VGA mode.

Normal builds ignore both files.

## Diagnostics

Every boot with the Swiss video patches writes `/nin960i.log` to the boot
device: the settings used, the timing, every Swiss signature found
(`Found:[...]`) and ten snapshots of the VI registers (captured by PADRead,
logged ~3 s apart).

## Testing without a Wii

`test/` builds the same patcher as a PowerPC Linux program. It loads a
`main.dol`, runs the patcher and prints every function Swiss recognised and
every word it changed:

```
make -C kernel/swissvideo                  # stub headers (devkitPPC)
make -C kernel/swissvideo/test CC=powerpc-linux-gnu-gcc
qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALE              # NTSC, 960i
qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALP P            # PAL
qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALE vmode=3 wide=2 aniso=1
```

## Not ported (yet)

* `Patch_GameSpecificVideo()`, Swiss's per-game fixes for games whose own
  code breaks with forced video modes. Its offsets are DOL file offsets
  and need converting to load addresses.
* Per-game settings. Swiss keeps settings per game; here they are global.
