/* OG: the pistol's effect on the game (WEAPON.md). */
#include "game/game.h"
#include "game/weapon.h"

#include <string.h>

#define NOBJ 160

static double hits[NOBJ];
static bool wreck[NOBJ], frozen[NOBJ], stopped[NOBJ];
static int wanted;
static int stopq[8], nstopq;
static bool dead;
static u32 generation, deaths;

void weapon_reset(void)
{
    memset(hits, 0, sizeof hits);
    memset(wreck, 0, sizeof wreck);
    memset(frozen, 0, sizeof frozen);
    memset(stopped, 0, sizeof stopped);
    nstopq = 0;
    wanted = 0;
    dead = false;
    generation++;
}

void weapon_freeze(int o)
{
    if (o <= 0 || o >= NOBJ) return;
    frozen[o] = true;
    u8 h = (u8)(DSB(DS_obj_heading + 2 * o + 1) + 0x40);          /* across the road */
    DSW(DS_obj_heading + 2 * o) = (u16)(h << 8 | h);
    DSW(DS_obj_vel + 2 * o) = 0;
}

void weapon_player_killed(void) { dead = true; }

bool weapon_take_death(void)
{
    if (!dead) return false;
    dead = false;
    generation++;
    deaths++;
    return true;
}

u32 weapon_generation(void) { return generation; }
u32 weapon_deaths(void) { return deaths; }

static u32 races, legs_done;
void weapon_race_start(void) { races++; }
void weapon_leg_done(void) { legs_done++; }
u32 weapon_races(void) { return races; }
u32 weapon_legs_done(void) { return legs_done; }

void weapon_set_wanted(int stars) { wanted = stars < 0 ? 0 : stars > 3 ? 3 : stars; }
int  weapon_wanted(void) { return wanted; }

bool weapon_is_police(int o)
{
    if (o <= 0 || o >= NOBJ || o >= DSW(DS_obj_a475)) return false;
    u16 f = DSW(DS_obj_flags + 2 * o);
    u8 m = (u8)(f & 0x3F);
    return (f & 0x2000) && (m == 0x11 || m == 0x30);
}

void weapon_make_police(int o, int model)
{
    if (o <= 0 || o >= NOBJ) return;
    u16 f = DSW(DS_obj_flags + 2 * o);
    DSW(DS_obj_flags + 2 * o) = (u16)((f & ~0x3F) | (model & 0x3F));
}

void weapon_police_chase(int o)
{
    if (!weapon_is_police(o) || stopped[o] || wreck[o]) return;
    u16 f = DSW(DS_obj_flags + 2 * o);
    DSW(DS_obj_flags + 2 * o) = (u16)(f | 0x40C0);                   /* chase at speed class 3 */
    u16 w = DSW(DS_obj_waypoint + 2 * o);
    if (!(w & 0xFFC0)) DSW(DS_obj_waypoint + 2 * o) = (u16)(w + 0x40);   /* wake a parked one */
}

void weapon_police_stop(int o)
{
    if (o <= 0 || o >= NOBJ || stopped[o]) return;
    stopped[o] = true;
    DSW(DS_obj_flags + 2 * o) &= (u16)~0x4000;
    DSW(DS_obj_vel + 2 * o) = 0;
    if (nstopq < 8) stopq[nstopq++] = o;
}

int weapon_take_police_stop(void)
{
    if (!nstopq) return -1;
    int o = stopq[0];
    for (int i = 1; i < nstopq; i++) stopq[i - 1] = stopq[i];
    nstopq--;
    return o;
}

void weapon_police_release(void)
{
    police_pursuit_clear();
    memset(stopped, 0, sizeof stopped);
    nstopq = 0;
}

void weapon_bonus_life(void)
{
    if (DSB(DS_lives) < 9) DSB(DS_lives)++;
}

static bool is_vehicle(int o)
{
    if (o <= 0 || o >= NOBJ || o >= DSW(DS_obj_a475)) return false;   /* the moving objects come first */
    u16 f = DSW(DS_obj_flags + 2 * o);
    if (!(f & 0x2000)) return false;                          /* not a moving vehicle */
    u8 m = (u8)(f & 0x3F);
    return !(m >= 0x12 && m <= 0x14);                         /* trains take no notice */
}

int weapon_hit(int o, double amount)
{
    if (!is_vehicle(o)) return 0;
    if (wreck[o]) return 1;
    if ((hits[o] += amount) < WEAPON_CAR_HITS - 1e-6) return 1;
    wreck[o] = true;
    return 2;
}

bool weapon_wrecked(int o) { return o > 0 && o < NOBJ && wreck[o]; }

/* OG (TOWNS.md "Jobs"): a vehicle held where it is (a rival before the start, a stolen car stopped) or let go on;
 * the hits it took */
void weapon_hold(int o, bool on)
{
    if (o <= 0 || o >= NOBJ) return;
    frozen[o] = on;
    if (on) DSW(DS_obj_vel + 2 * o) = 0;
}
double weapon_hits(int o) { return o > 0 && o < NOBJ ? hits[o] : 0; }

bool weapon_frozen(u16 bx)
{
    int o = bx >> 1;
    if (o == 0) return foot_active();
    return weapon_wrecked(o) || (o < NOBJ && (frozen[o] || stopped[o]));
}
