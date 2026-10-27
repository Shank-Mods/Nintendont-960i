# BT.656 digital output (experiments build)

This build switches the Wii's digital video bus from Nintendo's format to
ITU-R BT.656 (embedded SAV/EAV timing codes), for a BT.656 encoder such as the
ADV7391. It is **on by default** in this build.

- **What it does.** After every vertical blank (a hook on the game's
  `__VIRetraceHandler`), it sets:
    - VI DCR (`0xCC002002`) bits 8-9 to 3, "debug" format, which is BT.656. This
      is the same bit the xyzzy-mod video tester toggles.
    - The 656 window registers: VI `0x72` = `0x8000 | 122` and VI `0x74` = `412`.
      Retail games never program these. The SDK only does on development
      hardware with a 656 encoder, using these values from its timing tables.
  The game's own VI code can rewrite DCR, so the values are put back every field.
- **Which video modes.** Pick them on the Swiss video page as usual:
    - **480p** is the SDK 480p timing: 54 MHz bus, 858 samples × 525 lines,
      59.94 Hz, 480 active lines starting at line 43.
    - **960i** is Swiss' 960i: the same line timing as 480p, interlaced (two
      fields of 480 active lines, 1049 lines per frame).

## Timing on the bus (480p and 960i)

| | Value | ADV7391 "ED at 54 MHz" (SMPTE 293M) |
|---|---|---|
| Byte clock | 54 MHz, 8-bit 4:2:2 (Cb Y Cr Y) | 54 MHz, 8-bit 4:2:2 |
| Samples per line | 858 | 858 |
| Active samples (SAV to EAV) | 720 (VI 656 window) | 720 |
| Game picture | 640, centred, black border on each side | — |
| Lines per frame | 525 (480p) / 1049 per frame, 2 fields (960i) | 525 progressive |
| Active lines | 480 (lines 43-522) | 483 (lines 43-525); the last 3 are blanked |
| Sync | embedded SAV/EAV only | external sync not supported in this mode |

960i is not a format the ADV7391 lists; per the request it uses 480p's
horizontal timing and line rate, interlaced.

## Settings (`/nin960i_vi.txt`)

    bt656=0        normal (Nintendo) digital output
    bt656hbe=122   656 active window start, VI units (default 122)
    bt656hbs=412   656 active window end, VI units (default 412)

With hlw = 429, the window runs from 122 to 429 + 412 = 841, i.e. 720 samples.
Move both values by the same amount to shift the 720 sample window, or change
their difference to resize it.

## Checking it worked

The 960i log (`/nin960i_NN.log`) gets these:
- At boot, `SwissVideo:bt656: ON - VI DCR format 3 ...`.
- Every few seconds, `bt656: retraces N, registers put back M times`.
- In the VI register dumps, `VI 00` second value 03xx, and `VI 70` showing
  `807A 019C` at 0x72/0x74.

BT.656 needs the Swiss video patches to be on (Swiss video page). It is off
when cheats or the debugger are enabled, because they use the same low
memory. The frame-time profiler still works alongside it.
