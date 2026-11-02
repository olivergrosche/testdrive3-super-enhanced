# Chase view and gamepad (fork og1)

Marked `OG:` in the code.

## Chase view

The game keeps running its cockpit view. Physics, collisions and the race clock read the ground from faces
around the cockpit camera, so they stay correct. The original's own outside view (F5) freezes the game for this
reason. Only the enhanced view (`enhanced.c`, `enh_scene.c`) is drawn from behind the car:

* **The car.** The cockpit view does not build the player's car (`509b` skips object 0).
  `enh_frame_drawn` emits it with `obj_emit_vehicle_moving` / `model_emit_faces` past the ends of the game's
  vertex and face arrays, takes it into the snapshot, then puts the arrays and every global it touched back
  (`car_emit` / `car_restore`). Its faces are drawn last, sorted by the original's key from the chase camera.
  Brake lights (type 9) and the side lamps (types 4-6) get 4db6's runtime colours for that camera.
* **Camera.** `chase_view` (in `view_compute`): the car's vertices are placed rigidly relative to the
  interpolated cockpit eye, so the car does not shake against the camera. The camera hangs `chase_dist`
  behind the eye and `chase_height` above it. Its heading follows the car with a 0.22 s lag. The horizon sits at
  row `chase_row`, and the camera stays above the ground under it. To try other values:
  `TD3_CHASE="dist,height,row"` (default `720,135,66`).
* **Screen.** The view covers all 320 × 200 (`ENH_CHASE_ROWS`). The top panels and any message box (rows
  71h..85h, `enh_msgbox`) stay in place. The two dials are cut out as ellipses (VGA pixels are tall) and moved
  to the bottom left. The gear lever's window is moved to the bottom right, and shows only while the game
  shows it (`DS_look_rect_dirty`). No mirror and no windscreen overlays (rain, drops, wipers, dashboard edge).
* **Headlights.** The cockpit's beams are two screen quads (`headlight_beam_draw`). `beam_capture` intersects
  their corners' lines of sight with the ground under the car. The near edge, which the dashboard hides, is
  moved to the car's front. The patch is drawn as a light before the car.
* **When.** A race in the cockpit view (`enh_keys_racing`, not `ext_view`, not the menu preview). F5, replays
  and the crash sequence are the original's. F8 or the pad's Y switch between chase and cockpit. The default is
  chase (`ENH_CHASE_DEFAULT`); `TD3_VIEW=cockpit` starts in the cockpit.

Cost at 1280 × 800, 2 × 2 AA: about 8 ms a frame (the cockpit window alone: about 4 ms).

## Gamepad

`host.c` `pad_update` feeds the game the keys it knows. They are fed past the key bindings, as the actions'
own keys.

| Context | Pad |
|---|---|
| Race | RT gas (Up), LT brake (Down), A handbrake (Space), B / RB gear up (A), X / LB gear down (Z), Y chase/cockpit, Start pause (Ctrl+P), Back Esc, d-pad up lights (H), down wipers (W), left radio (M), right mirror (R), L3 back to road (F6), R3 replay (F10) |
| Race, steering | left stick (`host_pad_steer` → `steer_frame`): dead zone 10 %, curve x^1.6, full lock standing, 55 % of it flat out, at most 4 locks a second |
| Race, looking | right stick left/right up to 90°; pulled back: behind |
| Message box | A = Y, B = N, Start = Enter, Back = Esc |
| Menus | d-pad / left stick = cursor keys (repeat after 0.4 s), A / Start = Enter, B / Back = Esc |

A steering key takes over from the stick until the stick is moved again.

## Developer aids

* `TD3_PAD="<s>:<ctl>=<v>[+...],..."`: a virtual gamepad (`lx ly rx ry` -1..1, `lt rt` -1 (up)..1, buttons
  `a b x y lb rb start back up down left right ls rs` 0/1).
* `TD3_NO_PADS=1`: real pads are ignored (the virtual one is still used).
* `TD3_VIEW=cockpit`, `TD3_CHASE=dist,height,row`.

Headless test: `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy TD3_NO_PADS=1 TD3_SNAPSHOT_DIR=snaps
TD3_KEYS="4:1c,7:1c,10:e048p" testdrive3-enhanced --game-dir Game`. This goes through the menu (two Enters)
and drives off.
