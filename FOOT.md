# On foot (fork og2)

Stop the car, get out, walk about in the first person, and get back in. Marked `OG:` in the code. The walker is
in `src/game/foot.c`; the view is in `src/enhanced/enhanced.c` (`foot_view`) and `enh_scene.c` (`ZOOM`).

## Controls

| | Pad | Keyboard |
|---|---|---|
| Get out (also while driving) | hold Y (0.4 s; a short press still switches chase / cockpit) | E |
| Get in (near the car, "Y / E: GET IN" shows) | Y | E |
| Walk | left stick | W S, Up Down; A D sideways |
| Turn / look up and down | right stick | Left Right; Page Up / Page Down |
| Run | L3 (until the stick comes back) or hold LB | Shift |
| Jump | A | Space |
| Lights, radio, pause, leave | d-pad up, d-pad left, Start, Back | H, M, Ctrl+P, Esc |
| Fire, aim, reload (`WEAPON.md`) | RT, LT, X | left / right mouse button (F), R |

On foot the mouse looks around (the window captures it while it has the focus).

On foot the driving, cockpit and replay keys do nothing (gears, wipers, F1, F5, F6, F7, F10, ...). The car lets you
out on the ground, in a race in the cockpit or chase view.

**Getting out while driving.** Up to 15 km/h (`car_speed` ≤ 20, a unit is about 0.77 km/h) the walker is
out at once and the car stands where it was. Faster, the car brakes itself hard: the game's hard brake and about
12 m/s² on top, the pedals and the reverse do nothing meanwhile. The walker is out once it is down to 15 km/h,
from 70 km/h in under a second. Pressing the gas again (after letting it go) drives on instead. Before, the car had
to stand (`car_speed` ≤ 2), and a brake held a little too long at the standstill put the reverse in (`REVERSE.md`):
stressful in an ambush. `foot_bail_controls` in `foot.c`, called by `controls_poll`.

## How it works

* **The walker takes over the player's position.** The game's camera, world build, ground under the camera,
  sprite list and the enhanced far ring all follow sprite slot 0 and `DS:pos_x/z/y`. On foot `foot_physics` moves
  those instead of `car_physics`; the car's state (positions, history, headings, pitch, its eye height, the
  near-face thresholds, the engine sound flag, the smooth headings) is put aside at the exit and back at the
  entry, so the car resumes exactly as it stood. The replay is not recorded on foot and is cleared at the entry.
* **The parked car** is object 0, whose `obj_x/z/y8/heading` the walker does not touch. `moving_objects_update`
  skips object 0 in the cockpit view; on foot it builds it like any other vehicle (without lane following), so it
  is drawn, sorted and collided with by the game itself. Traffic stops behind it as behind the player's car, and a
  traffic car touching it no longer means a crash.
* **Units.** x / z are the vertices' x4 units (about a centimetre: a car is about 460 long), heights the y units,
  which are about 2.3 times larger: a car is about 50 high and the driver's eye is 46 over the ground. The walker:
  eye 70 over the feet (a person's eye is about 1.4 times a car's roof), head 78, steps up to 18, body radius 28,
  walking 320 a second, running 700, jumping about 29 high.
* **The world.** `foot.c` reads the game's own face and vertex arrays (the last frame's). The ground is the highest
  polygon under the feet that is not steeper than 1.5 and not more than a step over them; water (type 0Eh) is not
  walked into. Walls are the steep polygons (vehicles included) and the solid lines whose height range meets the
  body (above the step): the walker is pushed out of them along the nearest edge and slides along. Trees, rocks
  and the crossing barrier are the sprites of collision class 60h, circles of 36 more.
* **The car's collision responses are off on foot**: `near_face_hits`, the wall / rail response of the face under
  the camera, `bounce_back`, the tree / rock crash, windscreen splats, the road-side messages, the knock-over bump
  and damage (the sign still falls over), and every crash flag (`foot_after_draw`). Walking onto the finish does
  not end the leg; the car has to be driven in. A leg restart, a lost life or leaving the race puts the player back
  in the car (`foot_reset`).
* **The view.** The walker's view covers the whole screen like the chase view (the top panels stay, the dials and
  the gear lever go; the gun, the crosshair and the rounds left are drawn over it, `WEAPON.md`). Its position is the game's interpolated eye, but the heading and the look up / down are
  turned at the display rate (`foot_view_update`), not at the game's 10 frames a second. The original projection's
  field of view (56 x 35 degrees at 4:3) is a telephoto's for a walker: on foot the scene is built for a window
  1 / 0.72 as large and drawn into the target at 0.72 (`ZOOM`: polygons, sprites, the sky and ground, and the
  depth's lines of sight), about 76 x 48 degrees at 4:3; aiming down the sights narrows it (`WEAPON.md`). The
  horizon sits at row 100 (the crosshair's) looking level; looking turns the view 26° up and 46° down. `TD3_FOOT_ZOOM` (0.3 .. 1) tries other values.

## Developer aids

* `TD3_FOOT_LOG=file`: the exit and entry, and one line a frame on foot (position, feet, ground and its type,
  heading, look, speed, `hit` when a wall pushed, `near` when the car is in reach). `TD3_FOOT_DUMP=n`: every n-th
  frame also the faces within 400 units.
* Headless: `TD3_PAD="10:y=1,11.2:y=0,12:ly=-1,16:ly=0"` (after `TD3_KEYS="4:1c,7:1c"`) gets out at the start
  and walks for 4 s.

## The map's edge (og8)

A leg's map is 32 x 16 cells (131072 x 65536 x4 units); its coordinates wrap at the edges (z at exactly the
map's height), which no car reaches. A walker who went off the edge came out at the far side of the map, with the
car hundreds of metres away and no way back. The walker is now kept 1024 units (about 10 m) inside the edges: the
edge holds like a wall (`keep_on_map` in `foot.c`; also where the walker gets out and where a stolen car is left).
