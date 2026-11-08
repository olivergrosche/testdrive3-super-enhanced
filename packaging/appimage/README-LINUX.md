# Test Drive III Super Enhanced on Linux

A native Linux build (no Wine). The game files are **not** included: you need your own copy of *Test Drive III:
The Passion*, **version 3.0**.

## Start

1. Make the image executable and start it once:

       chmod +x TestDrive3-Super-Enhanced-*.AppImage
       ./TestDrive3-Super-Enhanced-*.AppImage

   The first start creates `~/.local/share/testdrive3-super-enhanced/Game`, says that the game files are missing
   and opens that folder.
2. Copy all files of your Test Drive III into that folder: `TDIII.EXE` (or `TD3.EXE`), `PLAYDISK.DAT`, `DATAA.DAT`,
   `DATAB.DAT`, `DATAC.DAT`, `INSTR.DAT`, the car files (`C*.DAT`, `.LST`, `.POB`) and the scene files
   (`SCENE*.DAT`, `.LST`, `.HI`). Upper or lower case does not matter.
3. Start the image again. Double-clicking opens the launcher (car, course, graphics, keys); **Play** starts the
   game. Directly into the game:

       ./TestDrive3-Super-Enhanced-*.AppImage --play

Any game option also starts the game directly, for example `--classic` or `--aspect 4:3`. All of them:
`--game-help`. Is your copy the right version? `./TestDrive3-Super-Enhanced-*.AppImage --check` loads `TDIII.EXE`
and says so. Version 3.0 is needed; an older 1990 `TD3.EXE` (123,745 bytes) stops with "unexpected load image size".

**Steam Deck:** in Desktop Mode put the image somewhere (say `~/Games`), make it executable (Properties >
Permissions), copy your game files as above, then *Add to Steam* as a non-Steam game with the launch option
`--play`. No Proton needed.

## Where your files are

| What | Where |
| --- | --- |
| Your game files, high scores, last choices | `~/.local/share/testdrive3-super-enhanced/Game` |
| Options for `--play` | `~/.local/share/testdrive3-super-enhanced/testdrive3-enhanced.args` |
| This text and the mod's documents | `~/.local/share/testdrive3-super-enhanced/docs` |
| The launcher's settings | `~/.config/test-drive-iii-enhanced/settings.ini` |

If you choose another game folder in the launcher, the image uses that one. A newer image never overwrites your files.
Delete the folders to start over.

## If it does not start

* **`fuse: failed to ...` or `Cannot mount AppImage`:** FUSE is missing (`fusermount`, usually the package `fuse3`).
  Without it: `./TestDrive3-Super-Enhanced-*.AppImage --appimage-extract-and-run`
* **The launcher does not open:** it needs GTK 3 from the system. Without GTK 3 the image goes straight into the game
  with the options in `testdrive3-enhanced.args`.
* **No title bar in a window (GNOME on Wayland):** install `libdecor`, or start with `SDL_VIDEO_DRIVER=x11`.
* **Needs:** x86-64 and glibc 2.31 or newer (Ubuntu 20.04, Debian 11, Fedora 32 or later).
  Picture, sound and gamepads go through SDL 3, which uses what the system has (X11 or Wayland; PipeWire through its
  PulseAudio layer, PulseAudio or ALSA; OpenGL, Vulkan or software).
