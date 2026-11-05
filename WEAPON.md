# The weapons (fork og3, og5)

On foot (`FOOT.md`) the walker carries a pistol, and takes the uzis and shotguns the ambushes' gunmen drop
(`FOES.md`). Shots hit what the view shows; vehicles stop and burn out. Marked `OG:` in the code.

| | Pad | Keyboard / mouse |
|---|---|---|
| Fire (the uzi: held) | RT | left mouse button, F |
| Aim down the sights (narrower view, slower turning, less spread) | hold LT | hold the right mouse button |
| Reload (also when firing an empty gun; the shotgun shell by shell, fire stops it) | X | R |
| Next / previous weapon | RB or d-pad right / d-pad left | Q, the mouse wheel; 1 2 3 choose |
| Look | right stick | the mouse (captured while on foot), or the arrow keys |

On foot the radio moved to the d-pad's down. The weapon, the rounds in it and in reserve show at the bottom right;
the crosshair (from the hip) opens with movement and firing (the shotgun's shows its pattern) and shows red ticks
when a vehicle or a gunman was hit.

## The three

| | Pistol | Uzi | Shotgun |
|---|---|---|---|
| Rounds / reserve at most | 12 / 96 | 32 / 192 | 8 / 40 |
| Fire | semi-automatic, 0.11 s | automatic, 0.085 s (about 700 a minute) | pump, 0.85 s |
| Pellets, spread from the hip | 1, 0.23° (more walking and firing) | 1, 0.69° | 9 over 3.2° |
| Gunman: body / head | 45 / 150 (three / one) | 26 / 100 (four / one) | 24 / 60 a pellet, less with distance (to a quarter at 2600) |
| Vehicle (5 stop it) | 1 | 0.55 | 0.36 a pellet (two shots) |
| Reload | 1.3 s | 1.6 s | 0.3 s, then 0.42 s a shell |

The game starts with the pistol and 12 + 24 rounds. A gunman's gun lies where he fell, glinting: walking over it
(or driving over it) takes it with 4 to 44 rounds (the pistol 10 to 20, the uzi 24 to 44, the shotgun 4 to 9); a new
weapon goes into the hand (when it is the next one up or the one in hand is empty), one already carried adds its
rounds ("UZI AMMO") as far as the reserve's limit (a full one stays lying). Firing an empty weapon with no reserve
changes to the next one with rounds. **Shot dead, the walker loses everything but the pistol and 12 + 24 rounds.**

## Code

* `src/enhanced/enh_gun.c`: the gun. Everything runs once per displayed frame from the walker's view
  (`foot_view` in `enhanced.c`): the aim and its zoom (`enh_gun_aim`, before the view's horizon row is set), the
  shots, the reload, the hand's motion (`enh_gun_frame`), and the drawing (`enh_gun_draw`). The particles (sparks,
  dust, splashes, the wrecks' smoke and flames) are moved and drawn in every race view (`enh_parts_frame`,
  `enh_parts_draw`), so a wreck smokes in the cockpit and the chase view too.
* `src/game/weapon.c`: the game side: hits per vehicle, wrecks (`weapon_hit`, `weapon_wrecked`), reset at each
  leg's load. `moving_objects_update` leaves a wreck where it is (`weapon_frozen`: no lane following, no movement;
  it is still built and collided with, so traffic stops behind it and the player's car crashes into it).
* `src/sound/pcm_sfx.c`: the sounds, synthesised on first use (no recordings): the shot (a filtered noise burst
  darkening as it dies, a thump, two late dark echoes), hits on metal (a damped three-partial ring), dirt and
  water, the empty click, the reload (magazine out, in, the slide), the wreck (a low whump and a hiss).
  `host_pcm_play` (`host.c`) mixes up to 12 of them over the OPL2 and the speaker.

## How a shot works

The line of sight goes from the view's eye (the interpolated eye of the displayed frame) through the crosshair
(row 100, where the horizon is when looking level) along the displayed heading, with a spread (from the hip:
0.23° standing, more walking and after each shot; aiming: 15 % of it). It is met with every polygon of the newest
snapshot at its place in the displayed frame (moving vehicles as they are drawn, so what is hit is what is seen)
and with the trees, rocks and the barrier (upright cylinders of radius 26), up to 9000 units (about 90 m). The
nearest hit decides:

* a vehicle (an object with flag 2000h whose face range holds the face): `weapon_hit`; sparks and a metal ring;
  the fifth hit wrecks it (a burst, black smoke, the wreck sound). The player's own car and trains take no damage.
* water (face type 0Eh): a splash; the road (10h..14h): grit and a spark; walls and rails (1, 2): sparks and dust;
  a tree: wood chips; anything else: dust.

## The guns in the hand

The uzi: the receiver with its top cover, the barrel, the cocking knob (it runs back with each shot), the rear sight,
the front post between two ears, the grip with the magazine below it, the guard, the folded stock's butt, the
right hand. The shotgun: the receiver, the long barrel with its rib, the magazine tube, the bead, the wooden pump with
the left hand on it (they run back 8 cm and home 0.22 .. 0.62 s after each shot, the pump sound at 0.22 s), the
wooden grip and stock, the right hand. Their poses from the hip and down the sights, their recoil and flash sizes
are their own (`wdef` in `enh_gun.c`); the shotgun is turned to load its shells.

The pistol: a model of 21 boxes (the slide, the muzzle, the sights with a white dot on the front post, the frame, the
trigger guard and trigger, the grip, the magazine's base; the right hand's fingers, trigger finger, thumb, palm and
wrist; the cuff and the sleeve) in centimetres, flat-shaded from one light (above, left, ahead: the top bright,
the left side mid, the back in shade), half as bright at night, lit by the muzzle flash. It is projected with the
view's own scale (`RAD2PX` times the zoom), clipped 3 cm before the eye, drawn back to front into a 2 × 2
supersampled patch (3 × 3 below `--res-scale 3`) and laid over the frame by coverage.

* Hip: 12.5 cm right, 14.5 down, 42 ahead, turned 11° inwards, 3° up and canted 7°. Aiming: the sights on the
  crosshair, 50 cm ahead (arm's length); the view's zoom goes from 0.72 to 1.05 (`ENH_FOOT_ZOOM`, `ZOOM_ADS`)
  with a 70 ms time constant, turning slows to 45 %.
* Recoil: up 9° (aiming 4°), back 3.2 cm, recovering in about 75 ms; the slide runs back 3 cm and home in about
  50 ms, and stays back on the last round until the reload. Reloading (1.3 s): the gun dips and rolls, the slide is
  racked near the end. Getting out: the gun comes up in 0.35 s. Walking bob, and sway against turning and looking.
* The muzzle flash: two soft glows and five rays at the muzzle for 60 ms.

## Wrecks

A wreck's faces are drawn in dark greys of the current palette (`enh_burnt_map`: each colour through the
neutral palette entry nearest a fifth of its brightness), smoke rises from it (4.5 puffs a second, growing to 170,
3 to 4 s) and small flames flicker on it, while it is within 7000 units. The particles are drawn with the target's
depth, so they hide behind what is nearer. A new leg starts with every vehicle whole.

## Developer aids

* `TD3_GIVE=1|2|3`: every weapon with full reserves from the start, that one in the hand.
* `TD3_GUN_DEBUG=1`: one line on stderr per shot (distance, face, object, face type, its flags) and the
  crosshair's values every 30th displayed frame.
* `TD3_PCM_DUMP=dir`: the sounds as `dir/pcm0..6.wav` when the first one plays.
* `TD3_SNAPSHOT_MS=n`: snapshots every n ms instead of 2 s.
* Headless shooting at the start: `TD3_PAD="9:lt=-1+rt=-1,10:y=1,11.2:y=0,12:rx=0.5+ry=1,12.39:rx=0,12.43:ry=0,13:rt=1,13.1:rt=-1,..."`
  (the triggers are -1 when let go; 0 is half pressed) hits the opponent's car ahead.
