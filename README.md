# Test Drive III Super Enhanced

A fork of [kylofon/testdrive3-enhanced](https://github.com/kylofon/testdrive3-enhanced) (Test Drive III Enhanced,
the SDL3 port by Krzysztof Kania) that turns the 1990 racer into a small open-world action game. On top of
everything upstream does:

* Chase view, modern gamepad controls, widescreen (`CHASE.md`).
* On foot: get out (while driving, the car brakes itself), walk, run and jump in the first person, get back in
  (`FOOT.md`).
* Fists and weapons: punches, a pistol, uzis and shotguns, aiming down the sights; cars shot into burning wrecks
  (`WEAPON.md`).
* Reversing with the automatic gearbox (`REVERSE.md`).
* Ambushes: gunmen waiting on the road, who drop their guns (`FOES.md`).
* Civilians walking along the roads and round the buildings (`CIVS.md`).
* Wanted levels: police chases, arrests, searches on foot, hiding; car thieves (`POLICE.md`).
* Villages with names on every leg, garages, gun shops, pubs and police stations; money and body armour
  (`TOWNS.md`).
* Cows and horses that graze, walk about and run from the car, the walker and gunfire (`ANIMALS.md`).
* Jobs made up at random: parcels, races, getaways and taxi fares from the pub, bounties and stolen cars from the
  police station (`TOWNS.md`).
* Start at any section: the course selection's Left / Right choose where the race begins.

Everything the fork adds is marked `OG:` in the code; `CHANGELOG.md` lists it by version (`-og1` .. `-og10`). It
needs the original game (version 3.0) as upstream does; the game files are not included. The port, its faithful
engine and the enhanced renderer are upstream's work. MIT, see `LICENSE`.

## Download and play

You need your own copy of *Test Drive III: The Passion*, **version 3.0**. Nothing of the original game is included.
The downloads are on the [Releases page](https://github.com/olivergrosche/testdrive3-super-enhanced/releases).

* **Windows:** unpack `TestDrive3-Super-Enhanced-<version>-win64.zip`, copy all your game files into its `Game`
  folder, then run `Test Drive III Enhanced.exe` (the launcher: car, course, picture, keys; **Play** starts the game)
  or `testdrive3-enhanced.exe` directly.
* **Linux:** make `TestDrive3-Super-Enhanced-<version>-x86_64.AppImage` executable and start it. The first start
  opens `~/.local/share/testdrive3-super-enhanced/Game`; copy your game files there and start again
  (`packaging/appimage/README-LINUX.md`).
* **Steam Deck:** the AppImage, added to Steam as a non-Steam game with the launch option `--play` (no Proton
  needed), or the Windows zip's `testdrive3-enhanced.exe` under Proton.
* **Which copy works:** version 3.0. Its `TDIII.EXE` is **125,251 bytes**, dated 26 April 1994, with the SHA-1
  `a09b0d620b91247976a7d172fd433e8c6af25926` (SHA-256
  `8c5e8fd00f39fa094aa7227d8a212fa16af3fbe47a6ca31c85cf32a740b03b96`). Many copies around are other versions; they
  stop with "unexpected load image size". Check yours: `testdrive3-enhanced.exe --check` (the AppImage: `--check`)
  loads `TDIII.EXE` and says whether it fits; or compare the hash (Windows: `certutil -hashfile TDIII.EXE SHA1`,
  Linux: `sha1sum TDIII.EXE`). Upper or lower case file names do not matter.

**First minutes:** drive as in the original (pad: RT gas, LT brake, left stick; keyboard: arrows). Tap Y on the pad
(F8) for the chase view. Hold Y (E) to get out, also while driving: the car brakes itself. On foot RT (left mouse)
punches or fires, RB (Q) changes the weapon. In a village walk into the ring at a door to shop or take a job. All
controls: *Controls* below.

---

# Test Drive III Enhanced — SDL3

An enhanced native port of Accolade's *Test Drive III: The Passion* (1990). Not an emulator. The original game
files are not included.

## Features

* Smooth 3D view at your display's refresh rate.
* High resolution (up to 2560 × 1600) with anti-aliasing.
* Longer draw distance and gradual distance fog.
* Smooth steering; spins, slides and the handbrake as in the original.
* Truer game speed; the race clock counts real seconds.
* Finish marker on the compass.
* Fixes to police, landings and other original quirks (see `CHANGELOG.md`).
* A launcher for car, course, skill and picture options.

## How to play (Windows)

1. Put your original game files in a folder named `Game`.
2. Place the program files next to it:

   ```text
   Test Drive III Enhanced\
   ├── Game\                           <- your game files (TDIII.EXE, DATAA.DAT, PLAYDISK.DAT, ...)
   ├── Test Drive III Enhanced.exe     <- the launcher
   ├── testdrive3-enhanced.exe         <- the game
   └── SDL3.dll and the other DLLs
   ```

3. Run `Test Drive III Enhanced.exe`, pick your options and press **Play**.

* The folder must be writable (high scores and choices are saved in `Game`).
* Alt+Enter: full screen.
* Launcher settings: `%APPDATA%\Test Drive III Enhanced\settings.ini`.

## Requirements

* Game files: `TDIII.EXE` (or `TD3.EXE`), `PLAYDISK.DAT`, `DATAA–C.DAT`, `INSTR.DAT`, the car (`C*`) and scene (`SCENE*`) files.
* A multi-core CPU for the default picture; lower the resolution or anti-aliasing on slow machines.
* To build: CMake 3.24+, C11, SDL 3; the launcher needs C++17 and wxWidgets 3.2.

## Build

In Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

* `-DTD3E_LAUNCHER=OFF` builds the game only.
* The MSYS2 `SDL3.dll` also needs `libiconv-2.dll` from `C:\msys64\mingw64\bin`.

## Run

```bash
./build/testdrive3-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Game files folder (default `Game`) |
| `--scale N` | Window size, × 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--res-scale N` | Resolution, × 320×200 (default 4, 1–8) |
| `--aa N` | Anti-aliasing, N×N samples (default 2, 1 = off) |
| `--motion-delay P` | Smooth view delay, % of a game frame (default 100) |
| `--haze P` | Distance fog, % of the sky colour at the edge of the view (default 70, 0 = off) |
| `--draw-distance N` | Map cells drawn around the camera (default 7, 0 = original) |
| `--fog-start P` | Where the fog begins, % of the way to where it is full (default 10) |
| `--beam-night P`, `--beam-day P` | Headlight beams' strength at night / by day, % (default 100 / 35) |
| `--beam-soft N` | Headlight beams' soft edge, pixels (default 3, 0 = hard) |
| `--enh-lights 0\|1` | Enhanced headlight beams (default 1; 0 = the original's) |
| `--keys NAME=CODE,...` | Key bindings (the launcher's Game settings > Key Bindings) |
| `--finish-marker 0\|1` | Finish marker on the compass (default 1) |
| `--frame-ticks N` | Game speed, timer ticks per frame (default 14) |
| `--classic` | Original picture only |
| `--sound adlib\|speaker` | Sound device |
| `--car CODE`, `--course CODE` | Start car / course, e.g. `CCNSX`, `SCENE02` |
| `--skill N` | Skill level 1–9 (1–3 automatic) |
| `--check` | Check that `TDIII.EXE` loads, then exit |
| `--aspect auto\|4:3\|16:9\|16:10\|W:H` | Picture shape (default auto = the screen's; fork og1, `CHASE.md`) |

## Controls

* Arrows / keypad: steer, gas, brake. Space: handbrake. A / Z: shift up / down.
* R mirror, H headlights, W wipers, C wheel centring, M radio.
* F1 window, F2 detail, F3 steering sensitivity, F5 chase view, F6 back to road, F7 mouse steering.
* F10 replay, F9 pause replay. Esc: leave.
* Ctrl-P pause, Ctrl-S sound, Ctrl-Q music, Ctrl-E engine sound, Ctrl-J joystick, Ctrl-K keyboard.
* The launcher's Game settings > Key Bindings changes the driving, car and game keys.
* F8: switch between the cockpit (the start view) and the chase view (fork og1, `CHASE.md`). F5 is still the original's
  frozen look-around view.
* Gamepad: RT gas, LT brake, left stick steering, A handbrake, B / RB shift up, X / LB shift down,
  Y chase view / cockpit, right stick look around (pull back: behind), Start pause, Back leave,
  d-pad lights / wipers / radio / mirror, L3 back to road, R3 replay. Menus: d-pad, A, B (`CHASE.md`).
* Sections (fork og8): in the course selection (main menu, COURSE) Up / Down choose the course as before, Left /
  Right the section the race starts at (1 to 5; the picture above shows it, with its name). Both courses (Pacific -
  Yosemite, and the Road & Car disk's Cape Cod - Niagara) have five sections; the original always started at the first.
* Radio (fork og9): the radio key (M, pad d-pad) now has a fourth, silent station after the three music ones.
* Jobs (fork og9, `TOWNS.md`): the pub's and the police station's menus offer a job; take it, follow the arrow,
  deliver on foot into the ring at the door (or clear the gang).
* Villages (fork og8, `TOWNS.md`): walk into the ring before a shop's door to buy (up / down choose,
  A / Space buy, B / E leave); with a wanted level drive into a garage's bay for a respray. Money top right.
* Police (fork og7, `POLICE.md`): crimes bring one to three stars; the police chase, get out, arrest or shoot,
  and search on foot; hide out of sight long enough to lose them. A car left alone may be stolen.
* Civilians (fork og6, `CIVS.md`): people walk along the roads and round the buildings, run from gunfire and
  jump out of the way of the car.
* Ambushes (fork og4, `FOES.md`): now and then gunmen wait on the road ahead and shoot; fight them on foot or
  drive through. Health bottom left (on foot) or top left (in the car); a cleared ambush gives a life.
* Reversing (automatic gearbox, fork og3, `REVERSE.md`): keep the brake held at a standstill (Down / LT), the
  brake key then drives backwards; the gas puts 1st in again.
* On foot (fork og2, `FOOT.md`): stop, then hold Y or press E to get out; walk with the left stick (W A S D),
  look with the right stick (arrows, Page Up / Down), run L3 / LB (Shift), jump A (Space); Y or E next to the car
  gets back in.
* Weapons on foot (fork og3, og5, `WEAPON.md`): fire RT (left mouse, F), aim LT (right mouse), reload X (R),
  change RB / d-pad (Q, wheel, 1 2 3 4); out of the car the walker is unarmed (RT punches), changing brings out the
  pistol, and the uzis and shotguns dropped by the gunmen.

## Layout

* `src/` — the game (`ENGINE.md`; changes marked `ENH:`).
* `src/enhanced/` — the enhanced renderer (`ENHANCED.md`).
* `launcher/` — the launcher (`launcher/README.md`).
* `third_party/nuked-opl3/` — the OPL2 emulator.

## License

MIT (`LICENSE`). Nuked-OPL3 is LGPL 2.1+. *Test Drive III* is © Accolade; its files are not included.

## Support

https://buymeacoffee.com/krzysztofkania
