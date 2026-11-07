# The animals (fork)

The cows and horses in the fields graze, walk about their pasture and run from a fast car, the walker and gunfire,
their legs swinging as they go. They are the game's own models, polished, not replaced. Marked `OG:` in
`src/game/animals.c` (all of it), `render_object.c` (`parked_vehicles_emit`), `render_world.c` (the world's build),
`enh_far.c` (the far ring) and `enh_foes.c` (`enh_foes_heard`).

## Which objects

At a leg's start every static or parked object whose model is small (at most 2.6 m long and wide, 140 height units
high) and has at least four legs is an animal. A leg is a chain of line faces (the models draw their lower legs as
lines) in the lower 45 % of the model, its highest corner the hip, reaching down to the lowest fifth (the horse's
model has a front hoof lifted). Diagonal legs step together. The head is at the end of the body whose corners are
higher. Found that way (2026-11-07):

| Scene | Model | Kind | Legs | On the legs |
|---|---|---|---|---|
| Cape Cod - Niagara | 58 | the black and white cow (static) | 4 lines | 6 to 20 animals a leg |
| Cape Cod - Niagara | 39 | the horse (parked) | 4 chains of 2 or 3 lines | (with the cows) |
| Pacific - Yosemite | 58 | the brown cattle (static) | 4 lines | 3 to 6 a leg |

The seagulls (model 56) are moving objects of the game's own and stay as they are.

## How they live

* **Grazing**: they stand 4 to 13 s (at the start 2 to 10), then walk to a spot of their pasture: within 8 m of
  where the game put them, on ground that is not water, road or a hole, not in a house (the leg's own or a planned
  one) nor 1.5 m round it.
* **Walking**: 0.7 m/s in the game's units (its houses are about half size: a calm walk), turning 80 degrees a second
  towards the spot, slowing down to turn round. A step up or down of more than 40 height units (a cliff, a wall),
  water, road or a house on the way: they stop and graze.
* **Running**: 3.3 m/s, turning faster, for 3 to 7 s away from what frightened them: the player's car faster than
  18 (about 14 km/h) within 26 m, a shot of the player's within 50 m. The walker within 3.8 m makes them step 5 m
  away.
* **The legs**: a stride takes 1 s walking, 0.45 s running; the legs swing up to 18 degrees walking, 32 running, about
  the hip in the model's length, and come to rest when they stand.
* Further than 300 m from the player they stand as they are. In a replay they stand.

## How they are drawn

* **A parked one** (the horse) is the game's: its slot's place, height (the leg's ground, `village_ground`) and turn
  are set every frame, and the game draws it as any parked object (`parked_vehicles_emit`, its vertex cache cleared
  so that the pose is new).
* **A static one** (the cows) the world's build leaves out (`animals_static_hidden`); `animals_emit_statics` draws it
  every frame after the parked objects, within the same window round the camera, at any turn (`model_emit_vertices`
  turns by the full byte). The far ring draws those the frame did not take, where they are now (`animals_far`).
* **The legs**: just before an animal's model is emitted, its leg lines' corners are turned about their hip in the
  model's own vertex arrays, and put back right after (`animals_pose_slot` / `animals_unpose`): every animal has its
  own step though they share one model. The far ring draws them at rest.

Not tested: whether the car meets them. The cows are emitted after the static faces, among the parked objects'
(with no object slot); a horse is a parked object like the parked cars.

## Developer aids

* `TD3_NO_ANIMALS=1`: they stand as the game put them.
* `TD3_ANIMAL_WATCH=k`: once out of the car, the walker stands 6 m to the side of animal k (the parked ones first,
  then the statics, in slot order) looking at it wherever it goes; with `TD3_SNAPSHOT_DIR` a film of it.
* `TD3_FOES_DEBUG=1`: the animal models found (legs, head), how many on the leg, every one that runs.
* `TD3_OBJ_DUMP=1` (with `TD3_TOWN_DUMP`): every object of the leg with its model, flags and size;
  `TD3_MODEL_DUMP=m` (`m,s` for a static's format): a model's corners and faces; `TD3_OBJ_TOUR=dir,model`: a picture
  of every object of that model; `TD3_SPRITE_DUMP=dir`: the scene's sprite images and the leg's sprite instances
  (the animals are not sprites).
