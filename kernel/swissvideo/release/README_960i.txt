Nintendont 960i - TEST BUILD
============================

What this is
------------
A test build of Nintendont with the video options of Swiss ported to it:
forced video modes (240p, 480p, 960i, 1080i, ...), forced field
rendering, vertical filters, scaling, anisotropic filtering, widescreen
and more. With 960i each field is rendered at 480 lines, so the game is
drawn at twice the usual vertical resolution.

These are Swiss's own video patches, ported to Nintendont. Swiss and its
video code are by emu_kidid, Extrems and contributors:
https://github.com/emukidid/swiss-gc

IMPORTANT: display compatibility (960i)
---------------------------------------
The Wii's video encoder only accepts 960i flagged as 480p. It regenerates
the sync itself, so the signal really is 960i (fields offset by half a
line), but the vertical sync has no proper equalization pulses. Some
displays and scalers lock onto this, others don't (the picture rolls
vertically or there is no signal). That is a property of the Wii's
encoder, not something this build can fix. We are trying to find out
which displays work - please report your results either way.

Only 960i, 480p and 480i have been tested on a Wii so far. The other
modes are offered like Swiss offers them on a GameCube and are untested.

Requirements
------------
- An original Wii (not Wii U / vWii)
- For 960i, 480p and the other progressive modes: component cables and
  progressive scan (480p) enabled in the Wii settings
- For 960i: a display, scaler or capture device that accepts 960i

Installing
----------
Copy the "apps" folder to the root of your SD card or USB drive. It goes
into apps/Nintendont-960i and shows up in the Homebrew Channel as
"Nintendont 960i (test build)". It does not replace your normal
Nintendont. Both share nincfg.bin; the Swiss video settings are kept in
their own file, nincfg_swiss.bin, which normal Nintendont ignores.

Using it
--------
1. Start "Nintendont 960i (test build)".
2. Open Settings. In the right column, select "Swiss Video" and press A.
3. On the Swiss Video Settings page, set "Swiss Video Patches" to On and
   pick a mode in "NTSC Video Mode" / "PAL Video Mode" (e.g. 960i).
   Keep "AVE Compatibility" on AVE-RVL (the Wii's video encoder).
   Up/Down select, Left/Right/A change, B goes back.
4. Start a game.

Each setting is explained on the right side of the page. Progressive
modes only show up when the Wii has a component cable and progressive
scan enabled (or "Force DTV Status" is Yes); 1080i and 540p only with
CMPV-DOL compatibility, like in Swiss. If you get a black screen, power
off the Wii and set "Swiss Video Patches" to Off, or the video mode back
to Auto, or AVE Compatibility back to AVE-RVL.

"Invert Field Order" (not in Swiss) swaps which field each frame is
rendered for. Try it if a game looks combed or shimmery with field
rendering on (960i, or Field Rendering On/TAA), as if its fields were
swapped. If it helps, please report the game.

"1280x240p (exp.)" is an experiment for NTSC games (made for Kirby Air
Ride): the game is drawn at 1280x240 and sent as 1280x240p (240p with
twice the usual horizontal pixels). It needs component cables with 480p
enabled and overrides the video mode settings. "Left half" only draws the
left half (to test whether your display takes the signal); "Two-pass"
draws each frame twice, once per half. Please send nin960i.log with any
report.

While "Swiss Video Patches" is On, Nintendont's own video options (Force
Progressive, Video Mode, Video Width, Screen Position) are ignored. Turn it
Off to use Nintendont normally.

If you used an earlier test build with "960i Field Render" On, that
becomes Swiss Video Patches On with 960i for NTSC and PAL games.

Reporting results
-----------------
Please report:
- Game name and region (NTSC / PAL)
- Display / scaler model and how it is connected
- The Swiss Video settings you used (video mode, AVE compatibility, ...)
- Result: stable picture / rolls vertically / no signal / other
- The file nin960i.log from the root of your SD card or USB drive
  (written by this build every time a game starts with the Swiss video
  patches)

Source code
-----------
This is free software under the GNU GPL v2, like Nintendont and Swiss.
Source: https://github.com/Shank-Mods/Nintendont-960i (branch field-render-960i)
