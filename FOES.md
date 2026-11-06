# Ambushes (fork og4)

Now and then, while the player drives, gunmen wait on the road ahead: a car from the traffic stopped across the
road with three of them round it, or, with no car to stop, three on a piece of road. They shoot at the player, in
the car and on foot. Marked `OG:` in the code: `src/enhanced/enh_foes.c` (the ambushes, the gunmen, the player's
health), `src/game/weapon.c` (the stopped car, the death, the extra life), `race_run` (a life lost).

## How it plays

* **When.** The first ambush after 45 to 75 s of driving (the race clock running, the car faster than 15), then
  70 to 110 s of driving after each. The place: a traffic car 2500 to 7000 units ahead and at most 1500 to the
  side (not the police, the opponents or a train) is stopped and turned across the road (`weapon_freeze`); else
  the road face nearest to 4500 straight ahead (3500 to 6500, within 3000 to the side). The three stand to the
  right and the left of it and behind it, facing the player.
* **The gunmen** see the player within 8000 units (about 80 m) along a line of sight through the displayed frame's
  faces (`enh_ray_world`; the player's own car in the chase view left out), checked five times a second. Seen, they
  alert each other ("AMBUSH!"), turn (5 rad/s) and after a reaction of 0.5 to 1 s fire bursts of three (0.16 s
  apart, 1.2 to 2.2 s between bursts), and after half the bursts step aside 1.2 to 2.4 m. Lost from sight for 3 s
  they come closer; a walker within 7 m makes them back off. A shot from the player within 6000 units alerts them.
* **Their aim:** each shot hits with 0.21 × (1.15 − distance / 7000, between 0.2 and 1) / (1 + the player's speed /
  600 units a second); half that in the first second. A miss within 3000 may whine past (a ricochet).
* **The player's health** (100): a hit takes 6 to 11 on foot, 35 % of that in the car. The edges of the screen
  flash red and a red chevron round the centre shows where the shot came from; below 30 the edges stay reddish.
  After 4 s without a hit it comes back at 12 a second. At 0 a life is lost the game's own way (the message "You
  have n lives left", the car put back): `weapon_player_killed` → `race_run` → `foot_reset`, `life_lost`. The bar
  shows bottom left on foot, under the left top panel in the car (when not full or in an ambush).
* **Their guns (og5):** each carries a pistol (45 %), an uzi (30 %) or a shotgun (25 %), seen in his hands. Pistol:
  bursts of three as above. Uzi: bursts of seven 0.085 s apart, each a little more than half as likely to hit,
  4 to 8. Shotgun: one shot, 1.7 to 2.7 s apart, half the time at close range and nothing beyond 3750, 12 to 21 near,
  less far off. Dead, he drops it with rounds (`WEAPON.md`), and his cash, $30 to $120 (og8, `TOWNS.md`).
* **Hitting them:** a line of sight (each of the shotgun's pellets) meets a gunman before anything else (an upright
  cylinder of radius 24, 176 cm high); the damage is the weapon's (`WEAPON.md`; the pistol: 45 a body hit, three
  kill; a head hit, the top 26 cm, kills). They flinch, bleed a little and
  fall backwards. Driving into one (faster than 8, within 190 units) runs him over.
* **Cleared:** all three down: "AMBUSH CLEARED +1 LIFE" (at most 9 lives) and $250 (og8, `TOWNS.md`). An ambush the player leaves 20000 units
  behind (or 9000 with everyone down) is gone. A new leg or a death clears it, health back to 100.

## The figures

Sixteen blocks in centimetres, posed per frame from their joints: shoes, shins, thighs, the hips, the body (a brown
leather jacket over denim), upper arms and forearms, the gloves, the gun, the head in a black balaclava with a
skin-coloured eye slit. Standing alert they aim (the arms forward, the gun at the shoulder), unalerted they hold it
low; stepping, the legs swing; hit, they lean back for 0.18 s; dead, they fall backwards about the feet in 0.55 s.
Heights are divided by 2.3 into the game's height units (a car is about 50 high). Lit by one sun (above, left,
ahead), half as bright at night, hazed towards the horizon's colour with distance, and drawn back to front only
where they are nearer than the scene (the front target's depth, 14 units of slack). Their muzzle flashes are two
glows. Like the particles they are drawn in every race view, only where the frame shows the 3D view (not over a
message box, the dashboard or the mirror's rim: `enh_draw_mask`).

## Sounds

`pcm_sfx.c`: the gangsters' gun (the shot 1.25 times as fast), a hit on a gunman (a dull thud), the player hit (a low
thump), the ricochet (a falling whine). Their volume falls with distance and they are panned by direction.

## Developer aids

* `TD3_AMBUSH=s`: the first ambush after s seconds of driving; `TD3_AMBUSH_EVERY=s`: the next ones s seconds of
  driving after each (for recording: `TD3 Aufnahme.bat` next to the launcher sets 8 and 40 and starts it).
* `TD3_AMBUSH_TEST=d`: an ambush d units straight ahead (default 2200) s seconds after the race starts, also standing.
* `TD3_FOES_DEBUG=1`: the ambushes placed and every hit on a gunman, on stderr.
* `TD3_FOES_AUTOAIM=1`: on foot the view follows the nearest living gunman, with none the nearest officer (og8), then
  the nearest dropped gun (for headless shooting and punching tests).
