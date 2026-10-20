Nintendont 960i - TEST BUILD
============================

What this is
------------
A test build of Nintendont that can force GameCube games to use field
rendering and output 960i (1152i for a few PAL games), like the
"Video Mode: 960i" option of Swiss. Each field is rendered at 480 lines,
so the game is drawn at twice the usual vertical resolution.

It uses Swiss's own video patches (AVE-RVL compatibility mode), ported to
Nintendont. Swiss and its video code are by emu_kidid, Extrems and
contributors: https://github.com/emukidid/swiss-gc

IMPORTANT: display compatibility
--------------------------------
The Wii's video encoder only accepts 960i flagged as 480p. It regenerates
the sync itself, so the signal really is 960i (fields offset by half a
line), but the vertical sync has no proper equalization pulses. Some
displays and scalers lock onto this, others don't (the picture rolls
vertically or there is no signal). That is a property of the Wii's
encoder, not something this build can fix. We are trying to find out
which displays work - please report your results either way.

Requirements
------------
- An original Wii (not Wii U / vWii)
- Component cables
- Progressive scan (480p) enabled in the Wii settings
- A display, scaler or capture device that accepts 960i

Installing
----------
Copy the "apps" folder to the root of your SD card or USB drive. It goes
into apps/Nintendont-960i and shows up in the Homebrew Channel as
"Nintendont 960i (test build)". It does not replace your normal
Nintendont, and both share the same settings.

Using it
--------
1. Start "Nintendont 960i (test build)".
2. Open Settings. In the right column, set "960i Field Render" to On.
   (It is grayed out if component cables / progressive scan aren't set up.)
3. Start a game.

Turning "960i Field Render" On makes the build ignore Nintendont's own
video options (Force Progressive, Video Mode, Video Width, Screen
Position). Turn it Off to use Nintendont normally.

Reporting results
-----------------
Please report:
- Game name and region (NTSC / PAL)
- Display / scaler model and how it is connected
- Result: stable picture / rolls vertically / no signal / other
- The file nin960i.log from the root of your SD card or USB drive
  (written by this build every time a game starts in 960i mode)

Source code
-----------
This is free software under the GNU GPL v2, like Nintendont and Swiss.
Source: https://github.com/Shank-Mods/Nintendont-960i (branch field-render-960i)
