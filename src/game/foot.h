#pragma once
/* OG: on foot (FOOT.md). The player gets out of the stopped car, walks about in the first person and gets back
 * in. The walker takes over the player's position (DS:pos_*, sprite slot 0 = the camera); the car stays where it
 * was parked as object 0, which the game then builds like any other vehicle. */
#include "types.h"
#include "host.h"

/* the player is on foot */
bool foot_active(void);

/* frame_update, before the simulation: a pending exit / entry (pad Y, key E) */
void foot_frame_begin(void);
/* frame_update, instead of car_physics while on foot */
void foot_physics(void);
/* frame_draw, after the ground under the camera is known and before crash_start: the car's crash and wall
 * responses do not apply to the walker */
void foot_after_draw(void);
/* a leg (re)start, a lost life, the end of a race: back in the car (the state as the game set it) */
void foot_reset(void);

/* the enhanced view, once per displayed frame: reads the walking controls (*in), turns the walker at the display
 * rate (sens: 1 = full speed) and gives its heading (u16 units) and how far the view looks up (rows of the
 * projection at zoom 1, + = up); dt = seconds since the last call */
void foot_view_update(double dt, double sens, HostFootInput *in, double *yaw, double *look_rows);
/* the walker's speed over the ground (x4 units a second) and whether the feet are on it */
double foot_speed(bool *on_ground);
/* POLICE.md: where the parked car is (x4, z4, the ground's height, its heading u16); false in the car */
bool foot_car_pos(double *x4, double *z4, double *y, u16 *heading);
/* POLICE.md: the parked car is somewhere else now (a thief drove it there) */
void foot_move_car(double x4, double z4, double y, u16 heading);
/* developer aid (TOWNS.md TD3_TOWN_GOTO): the walker is put at (x4, z4), feet at height y */
void foot_teleport(double x4, double z4, double feet);
/* developer aid: turns the walker's view (heading u16 units, look rows) */
void foot_set_view(double yaw, double look_rows);
/* the walker is near enough to the car to get in (for the prompt) */
bool foot_near_car(void);
