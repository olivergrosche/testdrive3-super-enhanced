#pragma once
/* OG: the pistol's effect on the game (WEAPON.md): hit points of the vehicles, wrecks. The gun itself (aiming,
 * shots, the view model) is the enhanced view's (enhanced/enh_gun.c), which hits what it shows. */
#include "types.h"

#define WEAPON_CAR_HITS 5.0    /* the damage that stops a vehicle: a pistol hit is 1 */

/* a new leg: every vehicle whole again */
void weapon_reset(void);
/* a shot hit object `obj` (a slot of DS:obj_*) with `amount` of damage (a pistol hit 1); returns 1 hit, 2 the hit
 * that wrecked it, 0 not a target (the player's car, trains, parked scenery) */
int  weapon_hit(int obj, double amount);
/* the vehicle is a wreck: it stays where it is (moving_objects_update) and is drawn burnt out */
bool weapon_wrecked(int obj);
/* moving_objects_update leaves this vehicle where it is (a wreck, an ambush's car, or the parked player's car) */
bool weapon_frozen(u16 slot2);

/* OG (FOES.md): an ambush's car stops where it is, turned across the road */
void weapon_freeze(int obj);
/* the player was shot dead (enh_foes.c); race_run takes it (weapon_take_death) and loses a life */
void weapon_player_killed(void);
bool weapon_take_death(void);
/* OG (TOWNS.md "Jobs"): a vehicle held where it is or let go on; the hits it took (WEAPON_CAR_HITS wreck it) */
void weapon_hold(int obj, bool on);
double weapon_hits(int obj);
/* changes with every new leg and every death: the enhanced view clears its ambush then */
u32  weapon_generation(void);
/* changes with every death: the walker's weapons are lost then (WEAPON.md) */
u32  weapon_deaths(void);
/* an ambush cleared: one life more (at most 9) */
void weapon_bonus_life(void);
/* OG (TOWNS.md): a new race begins (the money starts afresh), a leg was finished (a prize); counters the enhanced
 * view watches */
void weapon_race_start(void);
void weapon_leg_done(void);
u32  weapon_races(void);
u32  weapon_legs_done(void);

/* ---- OG: the police (POLICE.md) ---- */
/* the wanted level (0 .. 3 stars), kept by the enhanced view */
void weapon_set_wanted(int stars);
int  weapon_wanted(void);
/* a police car (models 11h, 30h) */
bool weapon_is_police(int obj);
/* a traffic car becomes a police car (model `model`) */
void weapon_make_police(int obj, int model);
/* a police car gives chase (the original's chase: fast along the lanes, overtaking) */
void weapon_police_chase(int obj);
/* a chasing police car caught up with the wanted player (police_update), or stopped to search: it stays where it
 * is and its crew gets out (weapon_take_police_stop gives the enhanced view the car, -1 none) */
void weapon_police_stop(int obj);
int  weapon_take_police_stop(void);
/* the wanted level is gone: no more chases, the stopped police cars drive on */
void weapon_police_release(void);
