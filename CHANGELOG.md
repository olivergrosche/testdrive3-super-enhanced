# Changelog

## 0.2.0-og8 (fork)

- Villages: the game's own houses along every leg are grouped into villages with names (Western ones on Pacific -
  Yosemite, New England ones on Cape Cod - Niagara), shown when the player comes in. Some houses hold a service,
  with a sign over the door and a ring painted on the ground before it: a garage (car armour; driven into its bay
  with a wanted level, a respray loses the police), a gun shop (rounds, the uzi, the shotgun, body armour), a pub
  (beer, food: health) and a police station (pay the fine). Walk into the ring to shop. Nothing else points to them:
  no arrows, no glow, they are found by looking (`TOWNS.md`).
- A real village: PILGRIM COVE by the start of Cape Cod - Niagara, nine buildings of the leg's own models added on
  the level middle of the main street (gun shop, pub, garage, houses, a farm; the police beyond the bend), solid to
  the car and the walker. Houses go on level ground only (`src/game/village.c`, `TOWNS.md` "Real villages").
- Money: gunmen drop cash, a cleared ambush and a finished leg pay, civilians drop wallets; tickets, the hospital,
  the shops and the bail cost. Busted with enough money, the bail is paid instead of a life (the weapons are
  taken). Body armour takes most of a hit. The money shows small, top right (`TOWNS.md`).
- Fists: out of the car the walker is unarmed (nothing in view); changing the weapon brings out the guns. RT
  punches, a fist out from below, left and right; three punches drop a gunman, an officer or a passer-by
  (`WEAPON.md`). 1 is the fists now, 2 3 4 the guns.
- Sections: the course selection's Left / Right choose the section the race starts at (its picture and name show).
- On foot the map's edge holds: walking off it came out at the far side of the map (its coordinates wrap) with no
  way back to the car (`FOOT.md`).
- No glint on dropped guns and cash any more.

## 0.2.0-og7 (fork)

- Wanted levels: crimes someone sees (shooting near the police, hurting civilians, wrecking cars, attacking the
  police) bring one to three stars. Police cars chase with the original's pursuit (traffic out of sight joins as
  police when there are too few); caught up, officers get out to arrest (one star: "BUSTED!" costs a life) or to
  shoot. Out of their sight they search on foot where they last saw the player; long enough out of sight, they lose
  the player. The police cars keep to the roads, so hiding off the road works.
- Car thieves: a car left alone too long is driven off by a civilian and left further down the road; a chevron
  shows the way to it (`POLICE.md`).

## 0.2.0-og6 (fork)

- Civilians: up to six people (at most two of them along the roads) walk along the road edges and stroll round the buildings, each dressed differently
  (shirts, trousers or dresses, caps, hair, skin tones, heights). They jump out of the way of a fast car and run
  from gunfire and ambushes with their hands on their heads (`CIVS.md`).

## 0.2.0-og5 (fork)

- Two more weapons: an uzi (automatic, 32 rounds) and a pump shotgun (8 shells, nine pellets, loaded shell by shell),
  each with its own model, recoil, sound and sights. The gunmen carry pistols, uzis or shotguns and fire them their
  own way; dead, they drop them with rounds to pick up. Rounds are scarce now: the pistol starts with 12 + 24, and
  dying loses everything else. RB / the d-pad (Q, the mouse wheel, 1 2 3) change the weapon (`WEAPON.md`).

## 0.2.0-og4 (fork)

- Ambushes: now and then a car ahead stops across the road and three gunmen wait round it (or on the road). They
  spot the player, fire bursts, step aside, flinch, fall; three body hits or one head hit drop them, and the car
  runs them over. The player has health (red flashes and a chevron show where shots came from, it comes back after
  a while); at nothing a life is lost. A cleared ambush gives a life (`FOES.md`).

## 0.2.0-og3 (fork)

- A pistol on foot, shown in the first person: 12 rounds, reloading, aiming down the sights, recoil, muzzle flash.
  Shots hit what the view shows: sparks on metal, dust, grit, splashes. Five hits stop a vehicle, which burns out,
  smokes and stays where it is (also seen from the car). Synthesised shot, hit and reload sounds. On foot the mouse
  looks around (`WEAPON.md`).
- Reversing with the automatic gearbox: keep the brake held at a standstill and the car drives backwards; the gas
  takes it forward again (`REVERSE.md`).

## 0.2.0-og2 (fork)

- On foot: stop the car, hold the pad's Y (or press E) to get out, walk, run and jump in the first person, and
  get back in with Y / E next to the car. The parked car stays in the world as a solid vehicle that traffic stops
  for; walls, vehicles, trees and rocks block the walker, water is not walked into. The view covers the screen
  with a wider field of view and turns at the display rate (`FOOT.md`).

## 0.2.0-og1 (fork)

- Chase view: drive with the camera behind the car, over the whole screen. The game runs its cockpit view
  underneath, so physics and collisions are unchanged. The car is added to the enhanced view. The dials and the
  gear lever's window move to the corners, and the headlight beams light the road ahead. The game starts in the
  cockpit; F8 or the pad's Y switches views (`CHASE.md`).
- Gamepad: triggers for gas and brake, analog steering on the left stick, buttons for shifting, handbrake, lights,
  pause and the camera, the right stick to look around. The pad also works in the menus and message boxes.
- Widescreen (`--aspect`, default the screen's shape). The 3D view gets wider, nothing is stretched. The
  dashboard and roof continue outwards into shadow, and in the chase view the instruments sit in the corners.
  Menus keep 4:3 with black bars.
- Builds with a static SDL3 (no SDL3.dll to copy).
- `testdrive3-enhanced.args` next to the exe holds start options (for GameHub and Steam shortcuts), and the
  `Game` folder next to the exe is found from any working directory.

## 0.2.0

- Distance fog per pixel: the scenery sinks gradually into the fog instead of face by face; stronger by default (70 %).
- Headlight beams have soft edges, with their own strength by day and at night (Enhanced lights, `--enh-lights`).
- Launcher: menus (File > Preferences with Always on top, Game settings, About), as in Aces of the Pacific Enhanced.
- Launcher: Game settings > Graphics, with sliders for motion, draw distance, fog, where the fog begins and the headlight beams (strength at night and by day, soft edge).
- Launcher: Game settings > Key Bindings; the game applies them while racing (`--keys`).

## 0.1.1

- The game executable may be named `TD3.EXE` as well as `TDIII.EXE`.

## 0.1.0

- Project started from the faithful Test Drive III port, with its launcher renamed "Test Drive III Enhanced".
- Enhanced renderer: the 3D view and mirror are redrawn smoothly at the display's rate (`--motion-delay`).
- High resolution (`--res-scale`) with anti-aliasing (`--aa`), sprites drawn from their native images.
- The cockpit overlays are replayed over the enhanced view; the dashboard, HUD and menus stay the original's.
- The main menu's turning car is enhanced too; `--classic` shows the original picture only.
- Fixed shimmer where parts of the view fell back to the blocky original for a frame.
- The car answers the keys a frame sooner.
- Wheels of the other cars keep their size as the cars drive away.
- Default game speed is 14 ticks a frame (`--frame-ticks`), and the race clock counts real seconds.
- Fixed double images of trees and roadside sprites when entering a new map cell.
- Fixed another car's parts being carried with the player's car after leaving the chase view.
- Fixed flickering road markings and trees, which are now depth-tested against the scenery.
- The sun and moon are a smooth disc and crescent.
- Finish marker on the compass (`--finish-marker`).
- Draw distance of up to 10 map cells (`--draw-distance`), with the scenery, objects and trees beyond the game's own.
- Distance haze (`--haze`), after Play Stunts' upgraded renderer.
- Vehicles and buildings always use their full model in the enhanced view.
- The eye no longer dips under the ground on steep slopes or when landing.
- Smooth keyboard steering, from a small correction to a hard turn; spins, slides and the handbrake are unchanged.
- The car moves along its exact heading instead of in 64 fixed directions.
- Landings on a downslope after a jump no longer count as a drop onto the flat.
- The aeroplane is drawn small and flies slowly, so it looks far away.
- Drifting sprites (the aeroplane, clouds, birds) keep moving at the longer draw distance.
- Police cars give chase only once the player has passed them, and ticket opponents only once overtaken.
- The ticket message reads "20 second penalty" instead of ":20 penalty".
- The menu preview shows its trees on the first visit, with no pixelated frame when a race starts.
- Launcher: a "Picture" box for graphics, resolution, anti-aliasing, motion, haze and draw distance.
- Developer aids: `TD3_ENH_COMPARE` (enhanced and original side by side) and `TD3_ENH_LOG` (per-frame log).
