#pragma once
/* OG: (ANIMALS.md) the leg's cows and horses graze, walk about their pasture and run from a fast car, the walker
 * and gunfire; their legs swing as they go. The game's own models: a parked one (the horse) is drawn by the game
 * from its slot, which is moved; a static one (the cows) is left out of the world's build and drawn every frame
 * here. The legs (the models' lower legs are lines from the hip down to the hoof) are turned about the hip just
 * for the drawing of each animal, and put back. */
#include "types.h"

#include <stdbool.h>

/* parked_vehicles_emit, before the parked objects: the animals move (once a game frame) */
void animals_update(void);
/* around obj_emit_vehicle of a parked object: true when slot2_bx is an animal, its legs now in its pose (the
 * vertex cache cleared); animals_unpose puts the model back */
bool animals_pose_slot(u16 slot2_bx);
void animals_unpose(void);
/* parked_vehicles_emit, after the parked objects: the static animals within the view window */
void animals_emit_statics(void);
/* world_build_visible and the far ring: a static object (slot i) drawn here instead */
bool animals_static_hidden(int i);
/* the far ring: the static animals not emitted this frame (model, its vertex arrays' format, position units,
 * height, turn); false past the last */
bool animals_far(int k, u8 *model, u16 *px, u16 *pz, u16 *py, u8 *rot);
/* the player fired (enh_foes_heard): the animals near run */
void animals_heard(void);
