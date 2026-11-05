/* OG: ambushes (FOES.md): gangsters on foot who shoot at the player, in the enhanced view.
 *
 * Now and then, while the player drives, a car ahead stops across the road and three gunmen stand round it. They
 * see the player (a line of sight through the displayed frame's faces), turn, fire bursts and step aside; they
 * flinch when hit and fall when dead. Their shots hit the player by chance (distance, speed, cover in the car);
 * the player's health shows at the side, hits flash red from where they came, it comes back after a while
 * without hits, and at nothing a life is lost (game/weapon.c, race_run). An ambush cleared gives a life.
 *
 * The figures are boxes like the gun's, posed per frame (standing, aiming, walking, falling), in the world: lit by
 * one sun, hazed with distance, darker at night, and drawn only where they are nearer than the scene (the front
 * target's depth). Everything runs once per displayed frame and stops while the game does (pause, messages). */
#include "enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../host.h"
#include "game/foot.h"
#include "game/weapon.h"
#include "sound/pcm_sfx.h"

#define RAD2PX (1024.0 / M_PI)
#define A2RAD  (2.0 * M_PI / 65536.0)
#define YCM    2.3                      /* centimetres a height unit (x / z: about one a unit) */

#define NFOE        3
#define FOE_HP      100
#define SEE_RANGE   8000.0
#define DRAW_RANGE  14000.0

typedef struct {
    bool on;
    double x, z, y;                     /* feet: x4 units, height units */
    double yaw;                         /* facing, radians (0 = +z, + = towards +x) */
    int hp;
    bool alert, see;
    double seen_for, react, next_fire, los_t, fall_t, hit_t, flash_t, lost_t;
    int burst;
    double step_vx, step_vz, step_t, walk_phase;
    double gx, gy, gz;                  /* the muzzle, world (for the flash) */
    int wtype;                          /* WEAPON.md: 0 pistol, 1 uzi, 2 shotgun (what he fires, and drops) */
} Foe;

static Foe foe[NFOE];

/* a person as drawn (gunmen and civilians, CIVS.md): where, how posed, how dressed */
typedef struct {
    double x, z, y, yaw;
    double fall;                        /* radians: + forwards (running), - backwards (dead, a flinch) */
    double gait, phase;                 /* legs: 0 standing, 1 walking, 1.6 running; the step's phase */
    int arms;                           /* 0 hanging (a gun: held low), 1 aiming, 2 swinging, 3 hands on the head */
    double scale;                       /* height / 176 cm */
    int gun;                            /* -1 none, 0 pistol, 1 uzi, 2 shotgun */
    bool mask, dress, longhair, cap;
    u32 top, pants, shoes, skin, hair, capc;
} Body;

/* CIVS.md: people walking along the roads and round the houses */
#define NCIV 6
#define NCIV_ROAD 2                     /* at most this many of them along the (country) roads */
typedef struct {
    bool on;
    double x, z, y, yaw;
    int mode, after;                    /* 0 along a road, 1 round a place, 2 standing, 3 fleeing */
    double t, speed, phase, check_t;
    double cx, cz, tx, tz;              /* round a place: its middle, where to next */
    int side;                           /* along a road: the road on this side (+1 right, -1 left) */
    bool roadie;                        /* placed along a road (counts against NCIV_ROAD) */
    double fx, fz;                      /* fleeing from */
    int hp;
    double fall_t, hit_t;
    Body look;
} Civ;
static Civ civ[NCIV];
static double civ_spawn_t;

/* POLICE.md: the wanted level, the police on foot, the car thief */
#define NCOP 6
typedef struct {
    bool on, see;
    double x, z, y, yaw;
    int hp, task, car, burst;           /* task: 0 searching (to the last place seen, round it), 1 engaging, 2 back to the car */
    double tx, tz, t, los_t, seen_for, next_fire, fall_t, hit_t, flash_t, phase, react;
    double speed;                       /* now, for the legs */
    u32 skin;
} Cop;
static Cop cop[NCOP];
static struct {
    int stars;
    double seen_t, lkx, lkz, lky, msg_t, busted_t, unit_t, sees_t, since;
    int msg;                            /* 1 WANTED, 2 LOST THEM, 3 BUSTED */
    bool seen, arrived;                 /* arrived: the police got near the last place (the search can be lost) */
} wnt;
static struct { int state, civ; double away_t, t, v, sx, sz, sy; u16 heading; bool done; } thief;
static double ppx, ppy, ppz;            /* the player, this frame */
#define NCIVMODE_THIEF 4
static void step_to(const EnhSnap *c, const EnhView *v, double *x, double *z, double *y, double *yaw, double want, double sp,
                    double dt, double *phase);

/* WEAPON.md: what the dead leave: their gun and its rounds, lying where they fell */
#define NPICK 8
typedef struct { bool on; double x, z, y, yaw; int wtype, rounds; double t; } Pick;
static Pick pick[NPICK];

static void drop(const Foe *f)
{
    static const int lo[3] = { 10, 24, 4 }, hi[3] = { 20, 44, 9 };
    for (int i = 0; i < NPICK; i++) {
        if (pick[i].on) continue;
        Pick *k = &pick[i];
        k->on = true;
        k->x = f->x - sin(f->yaw) * 60; k->z = f->z - cos(f->yaw) * 60; k->y = f->y;   /* where his hands fell */
        k->yaw = f->yaw + 0.7;
        k->wtype = f->wtype;
        k->rounds = lo[f->wtype] + (int)((hi[f->wtype] - lo[f->wtype] + 1) * ((double)(rand() & 0x7FFF) / 32768.0));
        k->t = 0;
        return;
    }
}
static struct {
    bool on, alerted, cleared;
    int car;                            /* the stopped car's object, -1 none */
    double cx, cz, cy;
    double wait;                        /* seconds of driving to the next one */
    double msg_t;
    int msg;                            /* 1 AMBUSH!, 2 cleared */
} amb = { .wait = -1, .car = -1 };

static double health = 100, since_hurt = 9, hurt_amount, hurt_bear;
static bool dead_sent;
static u32 gen_seen;
static u16 last_fc;
static double still_t;
static u32 rng = 0x9E3779B9u;

static double frand(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xFFFFFF) / 16777216.0;
}
static double srand1(void) { return frand() * 2 - 1; }
static inline double wraps(double a) { a = fmod(a, 65536.0); if (a < -32768) a += 65536; if (a >= 32768) a -= 65536; return a; }

static void clear_all(void)
{
    memset(foe, 0, sizeof foe);
    memset(pick, 0, sizeof pick);
    amb.on = amb.alerted = amb.cleared = false;
    amb.car = -1;
}

static double env_num(const char *name, double def)
{
    const char *e = SDL_getenv(name);
    return e ? SDL_atof(e) : def;
}

/* the player: the newest snapshot's eye (on foot the walker's, in the car the driver's) */
static void player_pos(const EnhSnap *c, double *x, double *y, double *z)
{
    *x = c->cam_x4; *z = c->cam_z4; *y = c->cam_y;
}

static double player_speed(void)
{
    if (foot_active()) return foot_speed(NULL);
    return DSW(DS_car_speed) * 20.0;
}

/* ------------------------------------------------------------------------------------------------------
 * Ambushes
 * ------------------------------------------------------------------------------------------------------ */

static void place(Foe *f, double x, double z, double y, double px, double pz)
{
    memset(f, 0, sizeof *f);
    f->on = true;
    f->x = x; f->z = z; f->y = y;
    f->yaw = atan2(wraps(px - x), wraps(pz - z));
    f->hp = FOE_HP;
    f->react = 0.5 + 0.5 * frand();
    double r = frand();
    f->wtype = r < 0.45 ? 0 : r < 0.75 ? 1 : 2;                       /* pistol, uzi, shotgun */
    f->next_fire = 0.3 + frand();
    f->los_t = frand() * 0.2;
}

static bool spawn(const EnhSnap *c, bool test)
{
    double px, py, pz;
    player_pos(c, &px, &py, &pz);
    double h = c->heading * A2RAD, fx = sin(h), fz = cos(h), rx = cos(h), rz = -sin(h);
    int best = -1;
    double best_along = 1e9;
    int n = DSW(DS_obj_a475);
    if (n > c->nobj) n = c->nobj;
    for (int o = 1; o < n && !test; o++) {
        u16 fl = DSW(DS_obj_flags + 2 * o);
        u8 m = (u8)(fl & 0x3F);
        if (!(fl & 0x2000) || m == 0x11 || m == 0x30 || (m >= 0x12 && m <= 0x14)) continue;   /* police, trains */
        if (DSB(DS_race_computer_cars) != 0 && o <= 2) continue;                              /* the opponents */
        if (weapon_frozen((u16)(2 * o))) continue;
        double dx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - px), dz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - pz);
        double along = dx * fx + dz * fz, lat = dx * rx + dz * rz;
        if (along < 2500 || along > 7000 || fabs(lat) > 1500) continue;
        if (along < best_along) { best_along = along; best = o; }
    }
    double cx, cz, cy = py - 46;
    if (best >= 0) {
        weapon_freeze(best);
        cx = px + wraps((u16)(DSW(DS_obj_x + 2 * best) << 2) - px);
        cz = pz + wraps((u16)(DSW(DS_obj_z + 2 * best) << 2) - pz);
        cy = (s16)DSW(DS_obj_y8 + 2 * best) / 8.0;
    } else if (test) {
        double d = env_num("TD3_AMBUSH_TEST", 2200);                    /* developer aid: right ahead */
        if (d < 500) d = 2200;
        cx = px + fx * d; cz = pz + fz * d;
        double g = enh_ground_height(c, cx, cz, py + 200, 400);
        if (g > -1e8) cy = g;
    } else {
        /* no car to stop: a piece of road 3500 .. 6500 ahead, the nearest to 4500 straight on */
        double bs = 1e18;
        int bf = -1;
        for (int f = 0; f < c->nf; f++) {
            const u16 *r = c->face[f];
            int nn = (r[0] >> 14) + 1;
            u8 type = (u8)(r[3] >> 11);
            if (nn < 3 || type < 0x10 || type > 0x14) continue;
            double sx = 0, sz = 0, sy = 0;
            bool ok = true;
            for (int k = 0; k < nn; k++) {
                int vi = r[k] & 0x7FF;
                if (vi >= c->nv) { ok = false; break; }
                sx += wraps(c->vx[vi] - px); sz += wraps(c->vz[vi] - pz); sy += c->vy[vi];
            }
            if (!ok) continue;
            sx /= nn; sz /= nn; sy /= nn;
            double along = sx * fx + sz * fz, lat = sx * rx + sz * rz;
            if (along < 3500 || along > 6500 || fabs(lat) > 3000) continue;
            double score = (along - 4500) * (along - 4500) + 4 * lat * lat;
            if (score < bs) { bs = score; bf = f; cx = px + sx; cz = pz + sz; cy = sy; }
        }
        if (bf < 0) return false;
    }
    double ux = wraps(cx - px), uz = wraps(cz - pz), ul = sqrt(ux * ux + uz * uz);
    if (ul < 1) ul = 1;
    ux /= ul; uz /= ul;
    double qx = uz, qz = -ux;                                           /* right of the approach */
    static const double at[NFOE][2] = { { 330, -30 }, { -350, 80 }, { 140, 480 } };
    for (int i = 0; i < NFOE; i++) {
        double x = cx + qx * at[i][0] + ux * at[i][1], z = cz + qz * at[i][0] + uz * at[i][1];
        double g = enh_ground_height(c, x, z, cy + 100, 200);
        place(&foe[i], x, z, g > -1e8 ? g : cy, px, pz);
    }
    amb.on = true;
    amb.alerted = amb.cleared = false;
    amb.car = best;
    amb.cx = cx; amb.cz = cz; amb.cy = cy;
    if (SDL_getenv("TD3_FOES_DEBUG"))
        fprintf(stderr, "ambush: car %d at %.0f %.0f (player %.0f %.0f)\n", best, cx, cz, px, pz);
    return true;
}

/* ------------------------------------------------------------------------------------------------------
 * The player's health
 * ------------------------------------------------------------------------------------------------------ */

static void hurt(double amount, double from_x, double from_z, const EnhSnap *c)
{
    double px, py, pz;
    player_pos(c, &px, &py, &pz);
    if (!foot_active()) amount *= 0.35;                                 /* the car's body and glass */
    health -= amount;
    since_hurt = 0;
    hurt_amount = fmin(1.0, hurt_amount + amount / 25.0);
    hurt_bear = atan2(wraps(from_x - px), wraps(from_z - pz));
    pcm_sfx_play(PCM_HURT, 0.9, 0);
    if (health <= 0 && !dead_sent) {
        health = 0;
        dead_sent = true;
        weapon_player_killed();
    }
}

/* ------------------------------------------------------------------------------------------------------
 * Once per displayed frame
 * ------------------------------------------------------------------------------------------------------ */

static void foe_shoot(Foe *f, const EnhSnap *c, double dist, double pan)
{
    f->flash_t = 0;
    static const int snd[3] = { PCM_SHOT_FOE, PCM_UZI, PCM_SHOTGUN };
    pcm_sfx_play(snd[f->wtype], fmax(0.12, 0.75 - dist / 14000.0), pan);
    double sp = player_speed();
    double p = 0.21 * fmax(0.2, fmin(1.0, 1.15 - dist / 7000.0)) / (1.0 + sp / 600.0), dmg = 6 + 5 * frand();
    if (f->wtype == 1) { p *= 0.55; dmg = 4 + 4 * frand(); }               /* the uzi: many, wild, light */
    if (f->wtype == 2) {                                                 /* the shotgun: hard near, nothing far */
        p = 0.5 * fmax(0.0, fmin(1.0, 1.25 - dist / 3000.0)) / (1.0 + sp / 900.0);
        dmg = (12 + 9 * frand()) * fmax(0.3, 1 - dist / 4000.0);
    }
    if (f->seen_for < f->react + 1.0) p *= 0.5;                         /* the first burst: still finding the aim */
    if (frand() < p) hurt(dmg, f->x, f->z, c);
    else if (dist < 3000 && frand() < 0.35) pcm_sfx_play(PCM_RICO, 0.5, pan + srand1() * 0.3);
}

/* ------------------------------------------------------------------------------------------------------
 * CIVS.md: the civilians
 * ------------------------------------------------------------------------------------------------------ */

/* the walkable surface under (x, z) no higher than top: its height and face type (-1e9, 0xFF: none) */
static double surf_at(const EnhSnap *c, double x, double z, double top, u8 *type)
{
    double best = -1e9;
    *type = 0xFF;
    for (int f = 0; f < c->nf; f++) {
        const u16 *r = c->face[f];
        int n = (r[0] >> 14) + 1;
        u8 t = (u8)(r[3] >> 11);
        if (n < 3 || t == 0 || t == 2) continue;                        /* polygons, not the vehicles */
        int vi[4];
        bool ok = true;
        for (int k = 0; k < n; k++) { vi[k] = r[k] & 0x7FF; if (vi[k] >= c->nv) ok = false; }
        if (!ok) continue;
        for (int q = 0; q + 2 < n; q++) {
            int a = vi[0], b = vi[1 + q], d = vi[2 + q];
            double px[3] = { wraps(c->vx[a] - x), wraps(c->vx[b] - x), wraps(c->vx[d] - x) };
            double pz[3] = { wraps(c->vz[a] - z), wraps(c->vz[b] - z), wraps(c->vz[d] - z) };
            double py[3] = { c->vy[a], c->vy[b], c->vy[d] };
            double c0 = px[0] * pz[1] - pz[0] * px[1], c1 = px[1] * pz[2] - pz[1] * px[2], c2 = px[2] * pz[0] - pz[2] * px[0];
            if (!((c0 >= 0 && c1 >= 0 && c2 >= 0) || (c0 <= 0 && c1 <= 0 && c2 <= 0))) continue;
            double ux = px[1] - px[0], uy = py[1] - py[0], uz = pz[1] - pz[0];
            double wx = px[2] - px[0], wy = py[2] - py[0], wz = pz[2] - pz[0];
            double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            if (fabs(ny) < 1e-6 || sqrt(nx * nx + nz * nz) > 1.5 * fabs(ny)) continue;   /* a wall */
            double h = py[0] - (nx * -px[0] + nz * -pz[0]) / ny;
            if (h <= top && h > best) { best = h; *type = t; }
        }
    }
    return best;
}

static bool is_road(u8 t) { return t >= 0x10 && t <= 0x14; }

static void civ_look(Civ *k)
{
    static const u32 tops[] = { 0xEEE8DC, 0xB03028, 0x3060B0, 0xD8B030, 0x4A8040, 0xD07030, 0x808080, 0x80A8D8,
                                0xD088A0, 0x6A3A80 };
    static const u32 pants[] = { 0x2C3652, 0xA89870, 0x202020, 0x606468, 0x5A4030, 0x1E2A44 };
    static const u32 skins[] = { 0xF0C8A8, 0xD8A888, 0xB08060, 0x805838, 0x5A3A28 };
    static const u32 hairs[] = { 0x201810, 0x5A3A20, 0xD8B868, 0x909090, 0x8A3A20 };
    static const u32 caps[] = { 0xC02020, 0x204080, 0x202020, 0xE0E0E0 };
    Body *b = &k->look;
    memset(b, 0, sizeof *b);
    b->gun = -1;
    b->dress = frand() < 0.3;
    b->top = tops[(int)(frand() * 10) % 10];
    b->pants = b->dress ? tops[(int)(frand() * 10) % 10] : pants[(int)(frand() * 6) % 6];
    b->shoes = frand() < 0.5 ? 0x181818 : 0x5A3A28;
    b->skin = skins[(int)(frand() * 5) % 5];
    b->hair = hairs[(int)(frand() * 5) % 5];
    b->longhair = b->dress ? frand() < 0.6 : frand() < 0.1;
    b->cap = !b->dress && frand() < 0.25;
    b->capc = caps[(int)(frand() * 4) % 4];
    b->scale = (b->dress ? 0.90 : 0.95) + frand() * 0.10;
}

/* a place for a new one: on the edge of a road (walking along it) or near a building's wall (strolling round
 * it), 3500 .. 9000 from the player, out of sight or beyond 5000, never near an ambush */
static int cand[2048];

static bool civ_spawn(Civ *k, const EnhSnap *c, const EnhView *v, double px, double pz, bool road)
{
    double h = v->heading * A2RAD;
    /* the candidates: road faces, or tall steep faces of type 3 (the buildings' walls) */
    int nc = 0;
    for (int f = 0; f < c->nf && nc < 2048; f++) {
        const u16 *r = c->face[f];
        int n = (r[0] >> 14) + 1;
        u8 t = (u8)(r[3] >> 11);
        if (n < 3 || (road ? !is_road(t) : t != 3)) continue;
        double sx = 0, sz = 0, ylo = 1e9, yhi = -1e9;
        bool ok = true;
        for (int q = 0; q < n; q++) {
            int vi = r[q] & 0x7FF;
            if (vi >= c->nv) { ok = false; break; }
            sx += wraps(c->vx[vi] - px); sz += wraps(c->vz[vi] - pz);
            if (c->vy[vi] < ylo) ylo = c->vy[vi];
            if (c->vy[vi] > yhi) yhi = c->vy[vi];
        }
        if (!ok || (!road && yhi - ylo < 25)) continue;
        sx /= n; sz /= n;
        double d = sqrt(sx * sx + sz * sz);
        static int near = -1;                                           /* developer aid: TD3_CIVS_NEAR=1, in sight */
        if (near < 0) near = SDL_getenv("TD3_CIVS_NEAR") != NULL;
        if (near ? d < 600 || d > 2500 : d < (road ? 3500 : 2500) || d > 9000) continue;
        double rel = atan2(sx, sz) - h;
        rel -= floor(rel / (2 * M_PI) + 0.5) * 2 * M_PI;
        if (!near && fabs(rel) < 0.75 && d < 5000) continue;            /* in sight and near: not out of nowhere */
        if (amb.on && fabs(wraps(px + sx - amb.cx)) < 3000 && fabs(wraps(pz + sz - amb.cz)) < 3000) continue;
        cand[nc++] = f;
    }
    if (!nc) {
        if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "civ: no %s faces in reach\n", road ? "road" : "wall");
        return false;
    }
    double bx = 0, by = 0, bz = 0;
    bool found = false;
    for (int tries = 0; tries < 12 && !found; tries++) {
        const u16 *r = c->face[cand[(int)(frand() * nc) % nc]];
        int n = (r[0] >> 14) + 1;
        double cx = 0, cz = 0;
        for (int q = 0; q < n; q++) { int vi = r[q] & 0x7FF; cx += wraps(c->vx[vi] - px); cz += wraps(c->vz[vi] - pz); }
        cx = px + cx / n; cz = pz + cz / n;
        if (road) {
            u8 t;
            double y = surf_at(c, cx, cz, 1e6, &t);
            if (y < -1e8) continue;
            double best = 1e9, ex = 0, ez = 0, ey = 0, ux = 0, uz = 0;
            for (int dir = 0; dir < 8; dir++) {                         /* out to the road's edge */
                double a = dir * M_PI / 4, sa = sin(a), ca = cos(a);
                for (double st = 60; st <= 900 && st < best; st += 60) {
                    u8 t2;
                    double y2 = surf_at(c, cx + sa * st, cz + ca * st, y + 120, &t2);
                    if (y2 < -1e8 || t2 == 0x0E) break;
                    if (!is_road(t2)) { best = st; ex = cx + sa * (st + 90); ez = cz + ca * (st + 90); ey = y2; ux = sa; uz = ca; break; }
                }
            }
            if (best > 900) continue;
            bx = ex; bz = ez; by = ey;
            double walk = atan2(ux, uz) + (frand() < 0.5 ? M_PI / 2 : -M_PI / 2);
            k->yaw = walk;
            double rx = cos(walk), rz = -sin(walk);                      /* the walker's right */
            k->side = (rx * -ux + rz * -uz) > 0 ? 1 : -1;                /* the road lies back the way out */
            k->mode = 0;
            found = true;
        } else {                                                        /* in front of the wall, on open ground */
            for (int q = 0; q < 6 && !found; q++) {
                double a = frand() * 2 * M_PI, rr = 300 + 350 * frand();
                double x = cx + sin(a) * rr, z = cz + cos(a) * rr;
                u8 t;
                double y = surf_at(c, x, z, 1e6, &t);
                if (y < -1e8 || t == 0x0E || is_road(t)) continue;
                bx = x; bz = z; by = y;
                found = true;
            }
            if (!found) continue;
            k->mode = 1;
            k->cx = cx; k->cz = cz;
            k->tx = bx; k->tz = bz;
            k->yaw = frand() * 2 * M_PI;
        }
    }
    if (!found) {
        if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "civ: no place (%s)\n", road ? "road" : "wall");
        return false;
    }
    k->on = true;
    k->x = bx; k->z = bz; k->y = by;
    k->speed = 115 + 40 * frand();
    k->phase = frand() * 6.28;
    k->hp = 100;
    k->t = 0; k->check_t = 0; k->fall_t = 0; k->hit_t = 9;
    k->after = k->mode;
    k->roadie = road;
    civ_look(k);
    if (SDL_getenv("TD3_FOES_DEBUG"))
        fprintf(stderr, "civ: %s at %.0f %.0f (%.0f away)\n", k->mode == 0 ? "road" : "place", bx, bz,
                sqrt(wraps(bx - px) * wraps(bx - px) + wraps(bz - pz) * wraps(bz - pz)));
    return true;
}

/* frightened: away from (x, z) for a while; dodge = only out of a car's way (no hands on the head) */
static void civ_flee(Civ *k, double x, double z, double secs, bool dodge)
{
    if (k->hp <= 0) return;
    if (k->mode != 3) k->after = k->mode == 2 ? k->after : k->mode;
    k->mode = 3;
    k->fx = x; k->fz = z;
    k->t = secs;
    double a = atan2(wraps(k->x - x), wraps(k->z - z));
    k->yaw = a + srand1() * 0.5;
    if (dodge) k->after = k->after % 10 + 10;                           /* marks a dodge: no hands up */
    else k->after %= 10;
}

static void civs_update(const EnhSnap *c, const EnhView *v, double dt, double px, double py, double pz)
{
    int n = 0, nroad = 0;
    for (int i = 0; i < NCIV; i++) if (civ[i].on) { n++; if (civ[i].roadie) nroad++; }
    int want = (int)env_num("TD3_CIVS", NCIV);
    if (want > NCIV) want = NCIV;
    if ((civ_spawn_t -= dt) <= 0 && n < want) {
        civ_spawn_t = 1.5;
        /* the roads: now and then one walker (at most NCIV_ROAD); mostly round the buildings */
        bool road = nroad < NCIV_ROAD && frand() < 0.3;
        for (int i = 0; i < NCIV; i++)
            if (!civ[i].on) { civ_spawn(&civ[i], c, v, px, pz, road); break; }
    }
    bool car = !foot_active();
    double ch = c->heading * A2RAD, cfx = sin(ch), cfz = cos(ch), crx = cos(ch), crz = -sin(ch);
    double cspeed = DSW(DS_car_speed);
    for (int i = 0; i < NCIV; i++) {
        Civ *k = &civ[i];
        if (!k->on) continue;
        double dx = wraps(k->x - px), dz = wraps(k->z - pz), d = sqrt(dx * dx + dz * dz);
        if ((d > 15000 && k->mode != NCIVMODE_THIEF) || (k->hp <= 0 && d > 9000)) { k->on = false; continue; }
        k->hit_t += dt;
        if (k->hp <= 0) { k->fall_t += dt; continue; }
        /* the player's car: run over, or out of its way */
        if (car && cspeed > 8 && d < 190 && fabs(py - 46 - k->y) < 60) {
            k->hp = 0; k->fall_t = 0; k->hit_t = 0;
            k->yaw = atan2(dx, dz);
            enh_police_crime(2);                                         /* POLICE.md */
            enh_part_blood(k->x, k->y + 30, k->z, dx / d, dz / d);
            pcm_sfx_play(PCM_FLESH, 1.0, 0);
            pcm_sfx_play(PCM_HIT_METAL, 0.6, 0);
            for (int j = 0; j < NCIV; j++)
                if (civ[j].on && j != i && fabs(wraps(civ[j].x - k->x)) < 4000 && fabs(wraps(civ[j].z - k->z)) < 4000)
                    civ_flee(&civ[j], k->x, k->z, 7 + 3 * frand(), false);
            continue;
        }
        if (car && cspeed > 25 && k->mode != 3) {
            double along = dx * cfx + dz * cfz, lat = dx * crx + dz * crz;
            if (along > 0 && along < 1700 && fabs(lat) < 260) {
                double sx = lat >= 0 ? 1 : -1;
                civ_flee(k, k->x - crx * sx * 500, k->z - crz * sx * 500, 1.4, true);
                k->yaw = atan2(crx * sx, crz * sx);
            }
        }
        /* an ambush that started near */
        if (amb.on && amb.alerted && k->mode != 3 && fabs(wraps(k->x - amb.cx)) < 7000 && fabs(wraps(k->z - amb.cz)) < 7000)
            civ_flee(k, amb.cx, amb.cz, 9 + 4 * frand(), false);

        if (k->mode == NCIVMODE_THIEF) {                                 /* POLICE.md: to the parked car */
            double cx, cz, cy;
            u16 ch;
            if (foot_car_pos(&cx, &cz, &cy, &ch)) {
                step_to(c, v, &k->x, &k->z, &k->y, &k->yaw, atan2(wraps(cx - k->x), wraps(cz - k->z)), 190, dt, &k->phase);
                continue;
            }
            k->mode = 0;
        }
        double sp = k->mode == 3 ? 380 : k->mode == 2 ? 0 : k->speed;
        if ((k->check_t -= dt) <= 0) {                                   /* look where to go, 3 times a second */
            k->check_t = 0.33;
            double fx = sin(k->yaw), fz = cos(k->yaw), rx = cos(k->yaw), rz = -sin(k->yaw);
            u8 here, ahead;
            double gy = surf_at(c, k->x, k->z, k->y + 60, &here);
            if (gy > -1e8) k->y = gy;
            double ay = surf_at(c, k->x + fx * 220, k->z + fz * 220, k->y + 60, &ahead);
            double fd[3] = { fx, 0, fz };
            bool wall = enh_ray_world(c, v, k->x, k->y + 40, k->z, fd, 160) < 160;   /* a wall, a car, a tree */
            if (k->mode != 3 && (ahead == 0x0E || ay < -1e8 || fabs(ay - k->y) > 45)) {  /* water, a drop: back */
                k->yaw += M_PI;
                k->side = -k->side;
            } else if (wall) {                                           /* round it */
                k->yaw += (k->mode == 0 ? -k->side : (frand() < 0.5 ? 1 : -1)) * (M_PI / 2);
                if (k->mode == 1) { k->tx = k->x + sin(k->yaw) * 300; k->tz = k->z + cos(k->yaw) * 300; }
            } else if (k->mode == 0) {                                   /* along the road's edge */
                u8 t1, t2;
                surf_at(c, k->x + rx * k->side * 160, k->z + rz * k->side * 160, k->y + 60, &t1);
                if (is_road(here)) k->yaw -= k->side * 0.35;              /* on it: off it */
                else if (!is_road(t1)) {
                    surf_at(c, k->x + rx * k->side * 340, k->z + rz * k->side * 340, k->y + 60, &t2);
                    k->yaw += k->side * (is_road(t2) ? 0.12 : 0.30);     /* it bends away: after it */
                }
            } else if (k->mode == 1) {                                   /* round a place */
                double tx = wraps(k->tx - k->x), tz = wraps(k->tz - k->z);
                if (tx * tx + tz * tz < 50.0 * 50.0) {
                    k->mode = 2; k->after = 1; k->t = 2 + 4 * frand();
                } else {
                    double want = atan2(tx, tz), dd = want - k->yaw;
                    dd -= floor(dd / (2 * M_PI) + 0.5) * 2 * M_PI;
                    k->yaw += dd * 0.6;
                }
            }
        }
        if (k->mode == 2 || k->mode == 3) {
            if ((k->t -= dt) <= 0) {
                if (k->mode == 3) { k->mode = 2; k->t = 1.5 + 2 * frand(); }   /* breath, then on */
                else {
                    k->mode = k->after % 10;
                    if (k->mode == 1) {                                  /* a new spot round the place */
                        double a = frand() * 2 * M_PI, r = 250 + 400 * frand();
                        k->tx = k->cx + sin(a) * r; k->tz = k->cz + cos(a) * r;
                    }
                    if (k->mode == 2) k->mode = 0;
                }
            }
        }
        if (sp > 0) {
            k->x += sin(k->yaw) * sp * dt;
            k->z += cos(k->yaw) * sp * dt;
            k->phase += dt * (sp > 300 ? 11.0 : 7.5);
        }
    }
}

/* ------------------------------------------------------------------------------------------------------
 * POLICE.md: the police
 * ------------------------------------------------------------------------------------------------------ */

static bool someone_near(double x, double z, double police_r, double civ_r)
{
    int n = DSW(DS_obj_a475);
    for (int o = 1; o < n && o < 160; o++) {
        if (!weapon_is_police(o) || weapon_wrecked(o)) continue;
        double dx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - x), dz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - z);
        if (dx * dx + dz * dz < police_r * police_r) return true;
    }
    for (int i = 0; i < NCOP; i++)
        if (cop[i].on && cop[i].hp > 0 && fabs(wraps(cop[i].x - x)) < police_r && fabs(wraps(cop[i].z - z)) < police_r) return true;
    for (int i = 0; i < NCIV; i++)
        if (civ[i].on && civ[i].hp > 0 && fabs(wraps(civ[i].x - x)) < civ_r && fabs(wraps(civ[i].z - z)) < civ_r) return true;
    return false;
}

void enh_police_crime(int level)
{
    if (!someone_near(ppx, ppz, 9000, 5000)) return;                    /* nobody saw it */
    int s0 = wnt.stars, ns = s0 > level ? s0 : level;
    if (s0 >= level && level >= 2) ns = s0 + 1;
    if (ns > 3) ns = 3;
    if (!s0 && ns) { wnt.msg = 1; wnt.msg_t = 2.5; wnt.unit_t = 0; wnt.arrived = false; wnt.since = 0; }
    wnt.stars = ns;
    weapon_set_wanted(ns);
    wnt.seen_t = 0;                                                     /* called in: they know where */
    wnt.lkx = ppx; wnt.lkz = ppz; wnt.lky = ppy;
    if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "crime %d -> %d stars\n", level, ns);
}

static void police_clear(int msg)
{
    wnt.stars = 0;
    weapon_set_wanted(0);
    weapon_police_release();
    for (int i = 0; i < NCOP; i++) if (cop[i].on) cop[i].task = 2;
    if (msg) { wnt.msg = msg; wnt.msg_t = 2.5; }
}

static void cop_place(Cop *k, int car, double x, double z, double y)
{
    memset(k, 0, sizeof *k);
    static const u32 skins[] = { 0xF0C8A8, 0xD8A888, 0xB08060, 0x805838, 0x5A3A28 };
    k->on = true;
    k->x = x; k->z = z; k->y = y;
    k->yaw = atan2(wraps(ppx - x), wraps(ppz - z));
    k->hp = 100;
    k->car = car;
    k->react = 0.5 + 0.4 * frand();
    k->next_fire = 0.5 + frand();
    k->hit_t = 9;
    k->tx = wnt.lkx; k->tz = wnt.lkz;
    k->skin = skins[(int)(frand() * 5) % 5];
}

static void cop_shoot(Cop *k, const EnhSnap *c, double dist, double pan)
{
    k->flash_t = 0;
    pcm_sfx_play(PCM_SHOT_FOE, fmax(0.12, 0.75 - dist / 14000.0), pan);
    double sp = player_speed();
    double p = 0.17 * fmax(0.2, fmin(1.0, 1.15 - dist / 7000.0)) / (1.0 + sp / 600.0);
    if (k->seen_for < k->react + 1.0) p *= 0.5;
    if (frand() < p) hurt(6 + 5 * frand(), k->x, k->z, c);
    else if (dist < 3000 && frand() < 0.35) pcm_sfx_play(PCM_RICO, 0.5, pan + srand1() * 0.3);
}

/* a person walks: turned towards (want), moved, on the ground, round walls */
static void step_to(const EnhSnap *c, const EnhView *v, double *x, double *z, double *y, double *yaw, double want, double sp,
                    double dt, double *phase)
{
    double dd = want - *yaw;
    dd -= floor(dd / (2 * M_PI) + 0.5) * 2 * M_PI;
    double rate = 6.0 * dt;
    *yaw += dd > rate ? rate : dd < -rate ? -rate : dd;
    double fd[3] = { sin(*yaw), 0, cos(*yaw) };
    if (enh_ray_world(c, v, *x, *y + 40, *z, fd, 120) < 120) *yaw += M_PI / 2;   /* a wall: round it */
    *x += sin(*yaw) * sp * dt;
    *z += cos(*yaw) * sp * dt;
    u8 t;
    double g = surf_at(c, *x, *z, *y + 50, &t);
    if (g > -1e8) *y = g;
    *phase += dt * (sp > 250 ? 11.0 : 7.5);
}

static void police_update(const EnhSnap *c, const EnhView *v, double dt, double px, double py, double pz)
{
    if (wnt.msg_t > 0) wnt.msg_t -= dt;
    static u32 test_gen = 0xFFFFFFFF;                                   /* developer aid: TD3_WANTED=n at the start */
    if (test_gen != gen_seen && SDL_getenv("TD3_WANTED")) {
        test_gen = gen_seen;
        wnt.stars = (int)env_num("TD3_WANTED", 1);
        weapon_set_wanted(wnt.stars);
        wnt.lkx = px; wnt.lkz = pz; wnt.lky = py; wnt.seen_t = 0; wnt.arrived = false; wnt.since = 0;
    }
    double heading = v->heading * A2RAD;
    /* who sees the player: the cops' own looks, and the police cars, four times a second */
    bool seen = false;
    for (int i = 0; i < NCOP; i++) if (cop[i].on && cop[i].hp > 0 && cop[i].see) seen = true;
    if ((wnt.sees_t -= dt) <= 0) {
        wnt.sees_t = 0.25;
        wnt.seen = false;
        int n = DSW(DS_obj_a475);
        for (int o = 1; o < n && o < c->nobj && wnt.stars > 0; o++) {
            if (!weapon_is_police(o) || weapon_wrecked(o)) continue;
            double ox = px + wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - px), oz = pz + wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - pz);
            double oy = (s16)DSW(DS_obj_y8 + 2 * o) / 8.0 + 65;
            double dx = wraps(px - ox), dz = wraps(pz - oz), dy = py - oy, l = sqrt(dx * dx + dz * dz + dy * dy);
            if (l > 7000 || l < 1) continue;
            double d[3] = { dx / l, dy / l, dz / l };
            if (enh_ray_world(c, v, ox, oy, oz, d, l - 60) >= l - 60) { wnt.seen = true; break; }
        }
    }
    seen = seen || wnt.seen;
    if (wnt.stars > 0) {
        wnt.since += dt;
        if (!wnt.arrived) {                                             /* the police on the scene yet? */
            int n = DSW(DS_obj_a475);
            for (int o = 1; o < n && o < 160 && !wnt.arrived; o++) {
                if (!weapon_is_police(o)) continue;
                double lx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - wnt.lkx), lz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - wnt.lkz);
                if (lx * lx + lz * lz < 4000.0 * 4000.0) wnt.arrived = true;
            }
            for (int i = 0; i < NCOP; i++) if (cop[i].on && cop[i].hp > 0) wnt.arrived = true;
            if (wnt.since > 45) wnt.arrived = true;
        }
        if (seen) { wnt.seen_t = 0; wnt.lkx = px; wnt.lkz = pz; wnt.lky = py; wnt.arrived = true; }
        else if (wnt.arrived) wnt.seen_t += dt;
        static const double lose[4] = { 0, 12, 20, 30 };
        if (wnt.seen_t > lose[wnt.stars]) police_clear(2);               /* LOST THEM */
    }
    if (wnt.stars > 0 && (wnt.unit_t -= dt) <= 0) {
        wnt.unit_t = 1.0;
        /* the cars on it: every police car within reach gives chase; too few: traffic out of sight becomes police */
        int n = DSW(DS_obj_a475), chasing = 0, model = 0;
        for (int o = 1; o < n && o < 160; o++) {
            if (!weapon_is_police(o) || weapon_wrecked(o)) continue;
            model = DSW(DS_obj_flags + 2 * o) & 0x3F;
            double dx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - px), dz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - pz);
            if (dx * dx + dz * dz > 25000.0 * 25000.0) continue;
            weapon_police_chase(o);
            if (DSW(DS_obj_flags + 2 * o) & 0x4000) chasing++;
            /* not seen for a while and near the last place: it stops there, the crew searches on foot */
            if (wnt.seen_t > 3 && (DSW(DS_obj_flags + 2 * o) & 0x4000)) {
                double lx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - wnt.lkx), lz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - wnt.lkz);
                if (lx * lx + lz * lz < 2500.0 * 2500.0) weapon_police_stop(o);
            }
        }
        static int leg_model;
        if (model) leg_model = model;
        if (chasing < wnt.stars + 1 && leg_model) {
            for (int o = 1; o < n && o < 160; o++) {
                u16 f = DSW(DS_obj_flags + 2 * o);
                u8 m = (u8)(f & 0x3F);
                if (!(f & 0x2000) || m == 0x11 || m == 0x30 || (m >= 0x12 && m <= 0x14)) continue;
                if (DSB(DS_race_computer_cars) != 0 && o <= 2) continue;
                if (weapon_frozen((u16)(2 * o))) continue;
                double dx = wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - px), dz = wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - pz);
                double d = sqrt(dx * dx + dz * dz), rel = atan2(dx, dz) - heading;
                rel -= floor(rel / (2 * M_PI) + 0.5) * 2 * M_PI;
                if (d < 6000 || d > 16000 || (fabs(rel) < 1.0 && d < 12000)) continue;   /* out of sight */
                weapon_make_police(o, leg_model);
                weapon_police_chase(o);
                if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "police: car %d joins (%.0f away)\n", o, d);
                break;
            }
        }
    }
    /* a police car stopped (caught up, or to search): two get out */
    int o;
    while ((o = weapon_take_police_stop()) >= 0) {
        if (wnt.stars <= 0) continue;
        double ox = px + wraps((u16)(DSW(DS_obj_x + 2 * o) << 2) - px), oz = pz + wraps((u16)(DSW(DS_obj_z + 2 * o) << 2) - pz);
        double oy = (s16)DSW(DS_obj_y8 + 2 * o) / 8.0;
        double h = ((DSW(DS_obj_heading + 2 * o) >> 8) * 256.0 + 0x4000) * A2RAD, sx = cos(h), sz = -sin(h);
        int placed = 0;
        for (int i = 0; i < NCOP && placed < 2; i++) {
            if (cop[i].on) continue;
            double side = placed ? -1 : 1;
            cop_place(&cop[i], o, ox + sx * 150 * side, oz + sz * 150 * side, oy);
            placed++;
        }
        if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "police: car %d stopped, %d out, at %.0f %.0f (player %.0f %.0f)\n", o, placed, ox, oz, px, pz);
    }
    /* the cops */
    bool near_cop = false;
    for (int i = 0; i < NCOP; i++) {
        Cop *k = &cop[i];
        if (!k->on) continue;
        k->flash_t += dt; k->hit_t += dt;
        double dx = wraps(px - k->x), dz = wraps(pz - k->z), dist = sqrt(dx * dx + dz * dz);
        if (dist > 16000) { k->on = false; continue; }
        if (k->hp <= 0) { k->fall_t += dt; if (dist > 9000) k->on = false; continue; }
        if (!foot_active() && DSW(DS_car_speed) > 8 && dist < 190 && fabs(py - 46 - k->y) < 60) {   /* run over */
            k->hp = 0; k->fall_t = 0; k->yaw = atan2(dx, dz);
            enh_part_blood(k->x, k->y + 30, k->z, -dx / dist, -dz / dist);
            pcm_sfx_play(PCM_FLESH, 1.0, 0);
            enh_police_crime(3);
            continue;
        }
        if ((k->los_t -= dt) <= 0) {
            k->los_t = 0.2;
            k->see = false;
            if (dist < 8000 && wnt.stars > 0) {
                double chest = k->y + 66, ddy = (py + 4) - chest, l = sqrt(dist * dist + ddy * ddy), d[3] = { dx / l, ddy / l, dz / l };
                double hit = enh_ray_world(c, v, k->x, chest, k->z, d, l - 40);
                k->see = hit >= l - 40;
                if (SDL_getenv("TD3_LOS_DEBUG")) fprintf(stderr, "los cop %d: l %.0f hit %.0f (cop y %.0f, player %.0f)\n", i, l, hit, k->y, py);
            }
        }
        if (k->see) k->seen_for += dt; else k->seen_for = fmax(0, k->seen_for - dt * 0.5);
        k->speed = 0;
        if (k->task == 2 || wnt.stars <= 0) {                            /* back to the car, then gone */
            k->task = 2;
            k->t += dt;
            double cx = k->x, cz = k->z;
            if (k->car > 0) { cx = px + wraps((u16)(DSW(DS_obj_x + 2 * k->car) << 2) - px); cz = pz + wraps((u16)(DSW(DS_obj_z + 2 * k->car) << 2) - pz); }
            double tx = wraps(cx - k->x), tz = wraps(cz - k->z);
            if (tx * tx + tz * tz < 120.0 * 120.0 || k->t > 30) { k->on = false; continue; }
            k->speed = 150;
            step_to(c, v, &k->x, &k->z, &k->y, &k->yaw, atan2(tx, tz), k->speed, dt, &k->phase);
            continue;
        }
        if (k->see) {                                                    /* engaging */
            k->task = 1;
            double want = atan2(dx, dz), bear = atan2(wraps(k->x - px), wraps(k->z - pz)) - heading;
            if (wnt.stars >= 2) {                                        /* they shoot */
                double dd = want - k->yaw;
                dd -= floor(dd / (2 * M_PI) + 0.5) * 2 * M_PI;
                double rate = 5.0 * dt;
                k->yaw += dd > rate ? rate : dd < -rate ? -rate : dd;
                k->next_fire -= dt;
                if (k->seen_for > k->react && k->next_fire <= 0 && fabs(dd) < 0.35) {
                    cop_shoot(k, c, dist, sin(bear) * 0.8);
                    if (++k->burst < 2) k->next_fire = 0.25;
                    else { k->burst = 0; k->next_fire = 1.3 + frand(); }
                }
            } else if (dist > 110) {                                     /* one star: they come to arrest */
                k->speed = 320;
                step_to(c, v, &k->x, &k->z, &k->y, &k->yaw, want, k->speed, dt, &k->phase);
            }
            if (dist < 150 && foot_active()) near_cop = true;
            if (dist < 260 && !foot_active() && DSW(DS_car_speed) < 3) near_cop = true;
            continue;
        }
        k->task = 0;                                                     /* searching */
        double tx = wraps(wnt.lkx - k->x), tz = wraps(wnt.lkz - k->z), tdist = sqrt(tx * tx + tz * tz);
        if (tdist > 400) { k->speed = 280; step_to(c, v, &k->x, &k->z, &k->y, &k->yaw, atan2(tx, tz), k->speed, dt, &k->phase); }
        else {                                                           /* round the place, spot by spot */
            double sx = wraps(k->tx - k->x), sz = wraps(k->tz - k->z);
            if ((k->t -= dt) <= 0 || sx * sx + sz * sz < 60.0 * 60.0) {
                double a = frand() * 2 * M_PI, r = 300 + 1200 * frand();
                k->tx = wnt.lkx + sin(a) * r; k->tz = wnt.lkz + cos(a) * r;
                k->t = 4 + 3 * frand();
            }
            k->speed = 150;
            step_to(c, v, &k->x, &k->z, &k->y, &k->yaw, atan2(wraps(k->tx - k->x), wraps(k->tz - k->z)), k->speed, dt, &k->phase);
        }
    }
    {                                                                    /* developer aid: where they are */
        static double dbg_t;
        if (SDL_getenv("TD3_FOES_DEBUG") && (dbg_t += dt) > 2) {
            dbg_t = 0;
            for (int i = 0; i < NCOP; i++)
                if (cop[i].on) fprintf(stderr, "cop %d task %d hp %d at %.0f %.0f (%.0f away) see %d stars %d seen_t %.1f\n", i, cop[i].task,
                                       cop[i].hp, cop[i].x, cop[i].z, sqrt(wraps(cop[i].x - px) * wraps(cop[i].x - px) + wraps(cop[i].z - pz) * wraps(cop[i].z - pz)),
                                       cop[i].see, wnt.stars, wnt.seen_t);
        }
    }
    /* busted: at one star, a cop at hand for a moment */
    if (wnt.stars == 1 && (near_cop || wnt.busted_t > 1.2)) {
        wnt.busted_t += dt;
        if (wnt.busted_t > 1.2 && wnt.msg != 3) { wnt.msg = 3; wnt.msg_t = 4; }   /* BUSTED!, then the life goes */
        if (wnt.busted_t > 2.2 && !dead_sent) {
            dead_sent = true;
            weapon_player_killed();
        }
    } else wnt.busted_t = 0;
}

/* ------------------------------------------------------------------------------------------------------
 * POLICE.md: the car thief
 * ------------------------------------------------------------------------------------------------------ */

static void thief_update(const EnhSnap *c, double dt, double px, double pz)
{
    double cx, cz, cy;
    u16 ch;
    if (thief.done || !foot_car_pos(&cx, &cz, &cy, &ch)) {
        if (!foot_active()) { thief.state = 0; thief.away_t = 0; }
        return;
    }
    double dx = wraps(cx - px), dz = wraps(cz - pz), d = sqrt(dx * dx + dz * dz);
    if (thief.state == 0) {
        thief.away_t = d > 2500 ? thief.away_t + dt : 0;
        if (thief.away_t < env_num("TD3_THIEF", 40)) return;
        for (int i = 0; i < NCIV; i++) {                                 /* someone near the car */
            Civ *k = &civ[i];
            if (!k->on || k->hp <= 0 || k->mode == 3) continue;
            double tx = wraps(cx - k->x), tz = wraps(cz - k->z);
            if (tx * tx + tz * tz > 6000.0 * 6000.0) continue;
            thief.state = 1; thief.civ = i;
            k->mode = NCIVMODE_THIEF;
            if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "thief: civilian %d goes for the car\n", i);
            return;
        }
        return;                                                          /* nobody about: not this time */
    }
    if (thief.state == 1) {                                              /* walking to it */
        Civ *k = &civ[thief.civ];
        if (!k->on || k->hp <= 0 || k->mode != NCIVMODE_THIEF) { thief.state = 0; thief.away_t = 20; return; }
        double tx = wraps(cx - k->x), tz = wraps(cz - k->z);
        if (tx * tx + tz * tz < 140.0 * 140.0) {                         /* in: away it goes */
            k->on = false;
            thief.state = 2; thief.t = 0; thief.v = 0;
            thief.sx = cx; thief.sz = cz; thief.sy = cy; thief.heading = ch;
            wnt.msg = 4; wnt.msg_t = 3;
            pcm_sfx_play(PCM_PUMP, 0.6, 0);                              /* the door */
        }
        return;
    }
    if (thief.state == 2) {                                              /* driven off along its heading */
        thief.t += dt;
        thief.v = fmin(1600, thief.v + 500 * dt);
        double h = thief.heading * A2RAD, x, z, y;
        u16 hh;
        foot_car_pos(&x, &z, &y, &hh);
        x += sin(h) * thief.v * dt; z += cos(h) * thief.v * dt;
        u8 t;
        double g = surf_at(c, x, z, y + 60, &t);
        if (g > -1e8) y = g;
        foot_move_car(x, z, y, thief.heading);
        double ex = wraps(x - px), ez = wraps(z - pz), ed = sqrt(ex * ex + ez * ez);
        double rel = atan2(ex, ez) - c->heading * A2RAD;
        rel -= floor(rel / (2 * M_PI) + 0.5) * 2 * M_PI;
        bool gone = ed > 6000 || (ed > 3000 && fabs(rel) > 0.9);         /* far, or out of the player's view */
        if (thief.t > 7 || gone) {                                       /* left by a road further on */
            double best = 1e18, bx = 0, bz = 0, by = 0;
            /* the road faces of the game's cells and of the far ring: about 7000 on from where it was taken */
            for (int f = 0; f < c->nf + c->nff; f++) {
                int n, vi[4];
                u8 type;
                const s16 *VX, *VY, *VZ;
                int lim;
                if (f < c->nf) {
                    const u16 *r = c->face[f];
                    n = (r[0] >> 14) + 1; type = (u8)(r[3] >> 11);
                    for (int q = 0; q < n && q < 4; q++) vi[q] = r[q] & 0x7FF;
                    VX = c->vx; VY = c->vy; VZ = c->vz; lim = c->nv;
                } else {
                    const EnhFarFace *e = &c->ff[f - c->nf];
                    n = (e->w0 >> 14) + 1; type = (u8)(e->w3 >> 11);
                    for (int q = 0; q < n && q < 4; q++) vi[q] = e->v[q] - c->nv;
                    VX = c->fvx; VY = c->fvy; VZ = c->fvz; lim = c->nfv;
                }
                if (n < 3 || n > 4 || !is_road(type)) continue;
                double sx = 0, sz = 0, sy = 0;
                bool ok = true;
                for (int q = 0; q < n; q++) {
                    if (vi[q] < 0 || vi[q] >= lim) { ok = false; break; }
                    sx += wraps(VX[vi[q]] - thief.sx); sz += wraps(VZ[vi[q]] - thief.sz); sy += VY[vi[q]];
                }
                if (!ok) continue;
                sx /= n; sz /= n; sy /= n;
                double along = sx * sin(h) + sz * cos(h), lat = sx * cos(h) - sz * sin(h);
                if (along < 4500 || along > 11000) continue;
                double score = (along - 7000) * (along - 7000) + 4 * lat * lat;
                if (score < best) { best = score; bx = thief.sx + sx; bz = thief.sz + sz; by = sy; }
            }
            if (best > 1e17) {                                           /* no road there: straight on, 50 .. 70 m */
                double d = 5000 + 2000 * frand();
                bx = thief.sx + sin(h) * d; bz = thief.sz + cos(h) * d; by = thief.sy;
                double g2 = surf_at(c, bx, bz, by + 400, &t);
                if (g2 > -1e8) by = g2;
            }
            foot_move_car(bx, bz, by, thief.heading);
            thief.state = 3; thief.done = true;
            if (SDL_getenv("TD3_FOES_DEBUG"))
                fprintf(stderr, "thief: car left at %.0f %.0f, %.0f from where it was taken (%s)\n", bx, bz,
                        sqrt(wraps(bx - thief.sx) * wraps(bx - thief.sx) + wraps(bz - thief.sz) * wraps(bz - thief.sz)),
                        best > 1e17 ? "no road found" : "on a road");
        }
    }
}

/* the player fired near: they run */
static void civs_heard(double x, double z)
{
    for (int i = 0; i < NCIV; i++) {
        Civ *k = &civ[i];
        if (!k->on || k->hp <= 0) continue;
        double dx = wraps(x - k->x), dz = wraps(z - k->z);
        if (dx * dx + dz * dz < 5000.0 * 5000.0) civ_flee(k, x, z, 7 + 4 * frand(), false);
    }
}

void enh_foes_frame(const EnhSnap *c, const EnhView *v, double dt)
{
    if (dt < 0 || dt > 0.25) dt = 0.25;
    u32 g = weapon_generation();
    if (g != gen_seen) {                                                /* a new leg, or shot dead: start afresh */
        gen_seen = g;
        clear_all();
        memset(civ, 0, sizeof civ);
        memset(cop, 0, sizeof cop);
        int keep_msg = wnt.msg;                                          /* "BUSTED!" stays over the message */
        double keep_t = wnt.msg_t;
        memset(&wnt, 0, sizeof wnt);
        wnt.msg = keep_msg; wnt.msg_t = keep_t;
        memset(&thief, 0, sizeof thief);                                 /* the car is the game's again */
        weapon_set_wanted(0);
        health = 100;
        dead_sent = false;
        amb.wait = env_num("TD3_AMBUSH", 45 + 30 * frand());
    }
    if (c->menu_preview || !enh_keys_racing()) return;
    /* the game stands (pause, a message, the crash sequence): so does everything here */
    if (c->frame_counter != last_fc) { last_fc = c->frame_counter; still_t = 0; }
    else if ((still_t += dt) > 0.3) return;
    if (DSW(DS_race_state) != 1 || DSB(DS_crashed) != 0 || DSB(DS_replay_state) != 0) return;

    since_hurt += dt;
    hurt_amount = fmax(0, hurt_amount - dt * 1.6);
    if (amb.msg_t > 0) amb.msg_t -= dt;
    if (since_hurt > 4.0 && health < 100 && health > 0) health = fmin(100, health + 12 * dt);

    double px, py, pz;
    player_pos(c, &px, &py, &pz);
    ppx = px; ppy = py; ppz = pz;
    civs_update(c, v, dt, px, py, pz);                                   /* CIVS.md */
    police_update(c, v, dt, px, py, pz);                                 /* POLICE.md */
    thief_update(c, dt, px, pz);

    if (!amb.on) {
        bool test = SDL_getenv("TD3_AMBUSH_TEST") != NULL;
        bool driving = !foot_active() && DSB(DS_clock_running) != 0 && DSW(DS_car_speed) > 15;
        if (driving || test) amb.wait -= dt;
        if (amb.wait <= 0 && (driving || test)) {
            if (!spawn(c, test)) {
                amb.wait = 5;
                if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "ambush: no place yet\n");
            }
            else amb.wait = test ? 1e9 : env_num("TD3_AMBUSH_EVERY", 70 + 40 * frand());
        }
        if (!amb.on) return;
    }

    double cdx = wraps(amb.cx - px), cdz = wraps(amb.cz - pz), cdist = sqrt(cdx * cdx + cdz * cdz);
    int alive = 0;
    for (int i = 0; i < NFOE; i++) if (foe[i].on && foe[i].hp > 0) alive++;
    if (cdist > 20000 || (alive == 0 && cdist > 9000)) { clear_all(); return; }
    if (alive == 0 && !amb.cleared) {
        amb.cleared = true;
        amb.msg = 2; amb.msg_t = 3.5;
        weapon_bonus_life();
    }

    for (int i = 0; i < NPICK; i++) {                                   /* WEAPON.md: the dropped guns */
        Pick *k = &pick[i];
        if (!k->on) continue;
        k->t += dt;
        double dx = wraps(px - k->x), dz = wraps(pz - k->z), reach = foot_active() ? 95 : 190;
        double feet = foot_active() ? py - 70 : py - 46;
        if (dx * dx + dz * dz < reach * reach && fabs(feet - k->y) < 60 && enh_gun_pickup(k->wtype, k->rounds)) k->on = false;
    }
    double heading = v->heading * A2RAD;
    for (int i = 0; i < NFOE; i++) {
        Foe *f = &foe[i];
        if (!f->on) continue;
        f->flash_t += dt; f->hit_t += dt;
        if (f->hp <= 0) { f->fall_t += dt; continue; }
        double dx = wraps(px - f->x), dz = wraps(pz - f->z), dist = sqrt(dx * dx + dz * dz);
        double chest = f->y + 58;
        if (!foot_active() && DSW(DS_car_speed) > 8 && dist < 190 && fabs(py - 46 - f->y) < 60) {   /* run over */
            f->hp = 0; f->fall_t = 0; f->hit_t = 0;
            f->yaw = atan2(dx, dz);                                      /* thrown away from the car */
            drop(f);
            enh_part_blood(f->x, f->y + 30, f->z, -dx / dist, -dz / dist);
            pcm_sfx_play(PCM_FLESH, 1.0, 0);
            pcm_sfx_play(PCM_HIT_METAL, 0.6, 0);
            for (int k = 0; k < NFOE; k++) if (foe[k].on) foe[k].alert = true;
            continue;
        }
        /* the line of sight, five times a second */
        if ((f->los_t -= dt) <= 0) {
            f->los_t = 0.2;
            f->see = false;
            if (dist < SEE_RANGE) {
                double dy = (py - 8) - chest, l = sqrt(dist * dist + dy * dy), d[3] = { dx / l, dy / l, dz / l };
                f->see = enh_ray_world(c, v, f->x, chest, f->z, d, l - 40) >= l - 40;
            }
        }
        if (f->see) { f->seen_for += dt; f->lost_t = 0; } else { f->seen_for = fmax(0, f->seen_for - dt * 0.5); f->lost_t += dt; }
        if (f->see && f->seen_for > f->react * 0.5 && !f->alert) {
            f->alert = true;
            if (!amb.alerted) { amb.alerted = true; amb.msg = 1; amb.msg_t = 2.5; }
            for (int k = 0; k < NFOE; k++) if (foe[k].on && !foe[k].alert) { foe[k].alert = true; foe[k].react += 0.4; }
        }
        if (!f->alert) continue;
        /* turn towards the player */
        double want = atan2(dx, dz), dy = want - f->yaw;
        dy -= floor(dy / (2 * M_PI) + 0.5) * 2 * M_PI;
        double rate = 5.0 * dt;
        f->yaw += dy > rate ? rate : dy < -rate ? -rate : dy;
        /* fire in bursts while the player is in sight */
        double bear = atan2(wraps(f->x - px), wraps(f->z - pz)) - heading, pan = sin(bear) * 0.8;
        f->next_fire -= dt;
        if (f->see && f->seen_for > f->react && f->next_fire <= 0 && fabs(dy) < 0.35) {
            foe_shoot(f, c, dist, pan);
            static const int bursts[3] = { 3, 7, 1 };
            static const double gaps[3] = { 0.16, 0.085, 0 }, pause[3] = { 1.2, 1.6, 1.7 };
            if (++f->burst < bursts[f->wtype]) f->next_fire = gaps[f->wtype];
            else {
                f->burst = 0;
                f->next_fire = pause[f->wtype] + frand();
                if (frand() < 0.5 && f->step_t <= 0) {                   /* step aside */
                    double s = frand() < 0.5 ? -1 : 1, l = 120 + 120 * frand();
                    f->step_t = l / 160.0;
                    f->step_vx = cos(f->yaw) * s * 160; f->step_vz = -sin(f->yaw) * s * 160;
                }
            }
        }
        /* lost from sight a while: closer, carefully */
        if (f->lost_t > 3 && f->step_t <= 0 && dist < 6000 && dist > 1500) {
            f->step_t = 1.5;
            f->step_vx = dx / dist * 110; f->step_vz = dz / dist * 110;
            f->lost_t = 0;
        }
        if (foot_active() && dist < 700 && f->step_t <= 0) {             /* too close: back off */
            f->step_t = 1.0;
            f->step_vx = -dx / dist * 150; f->step_vz = -dz / dist * 150;
        }
        if (f->step_t > 0) {
            f->step_t -= dt;
            f->x += f->step_vx * dt; f->z += f->step_vz * dt;
            f->walk_phase += dt * 9.0;
            double gnd = enh_ground_height(c, f->x, f->z, f->y + 25, 50);
            if (gnd > -1e8) f->y = gnd;
        }
    }
    (void)py;
}

/* the player's shot: the nearest gunman on the line before tmax; *head when in the head */
int enh_foes_ray(double ox, double oy, double oz, const double *d, double tmax, double *t, bool *head)
{
    int best = -1;
    double bt = tmax;
    for (int i = 0; i < NFOE; i++) {
        Foe *f = &foe[i];
        if (!f->on || f->hp <= 0) continue;
        double cx = wraps(f->x - ox), cz = wraps(f->z - oz), r = 24;
        double a = d[0] * d[0] + d[2] * d[2], b = -2 * (cx * d[0] + cz * d[2]), cc = cx * cx + cz * cz - r * r;
        double disc = b * b - 4 * a * cc;
        if (a < 1e-9 || disc < 0) continue;
        double tt = (-b - sqrt(disc)) / (2 * a);
        if (tt < 1 || tt >= bt) continue;
        double y = oy + d[1] * tt - f->y;
        if (y < 0 || y > 176 / YCM) continue;
        bt = tt; best = i;
        *head = y > 150 / YCM;
    }
    for (int i = 0; i < NCOP; i++) {                                    /* POLICE.md: the cops */
        Cop *k = &cop[i];
        if (!k->on || k->hp <= 0) continue;
        double cx = wraps(k->x - ox), cz = wraps(k->z - oz), r = 24;
        double a = d[0] * d[0] + d[2] * d[2], b = -2 * (cx * d[0] + cz * d[2]), cc = cx * cx + cz * cz - r * r;
        double disc = b * b - 4 * a * cc;
        if (a < 1e-9 || disc < 0) continue;
        double tt = (-b - sqrt(disc)) / (2 * a);
        if (tt < 1 || tt >= bt) continue;
        double y = oy + d[1] * tt - k->y;
        if (y < 0 || y > 176 / YCM) continue;
        bt = tt; best = NFOE + NCIV + i;
        *head = y > 150 / YCM;
    }
    for (int i = 0; i < NCIV; i++) {                                    /* CIVS.md: people in the way */
        Civ *k = &civ[i];
        if (!k->on || k->hp <= 0) continue;
        double cx = wraps(k->x - ox), cz = wraps(k->z - oz), r = 22;
        double a = d[0] * d[0] + d[2] * d[2], b = -2 * (cx * d[0] + cz * d[2]), cc = cx * cx + cz * cz - r * r;
        double disc = b * b - 4 * a * cc;
        if (a < 1e-9 || disc < 0) continue;
        double tt = (-b - sqrt(disc)) / (2 * a);
        if (tt < 1 || tt >= bt) continue;
        double y = oy + d[1] * tt - k->y, top = 176 / YCM * k->look.scale;
        if (y < 0 || y > top) continue;
        bt = tt; best = NFOE + i;
        *head = y > top * 0.85;
    }
    if (best >= 0) *t = bt;
    return best;
}

/* developer aid (TD3_FOES_AUTOAIM): the heading (u16 units) and the look up (rows of the projection at zoom 1)
 * from the eye to the nearest living gunman's chest */
bool enh_foes_aim(double ex, double ey, double ez, double *yaw, double *look)
{
    int best = -1;
    double bd = 1e18;
    for (int i = 0; i < NFOE; i++) {
        if (!foe[i].on || foe[i].hp <= 0) continue;
        double dx = wraps(foe[i].x - ex), dz = wraps(foe[i].z - ez), d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) {                                                     /* all down: the nearest dropped gun */
        for (int i = 0; i < NPICK; i++) {
            if (!pick[i].on) continue;
            double dx = wraps(pick[i].x - ex), dz = wraps(pick[i].z - ez), d = dx * dx + dz * dz;
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) return false;
        double dx = wraps(pick[best].x - ex), dz = wraps(pick[best].z - ez);
        *yaw = atan2(dx, dz) / A2RAD;
        *look = atan2(pick[best].y - ey, sqrt(dx * dx + dz * dz)) * RAD2PX;
        return true;
    }
    const Foe *f = &foe[best];
    double dx = wraps(f->x - ex), dz = wraps(f->z - ez), dy = f->y + 120 / YCM - ey;
    *yaw = atan2(dx, dz) / A2RAD;
    *look = atan2(dy, sqrt(dx * dx + dz * dz)) * RAD2PX;
    return true;
}

/* a gunman hit: hp, the flinch, the fall; everyone alerted */
int enh_foes_damage(int i, int dmg)
{
    if (i >= NFOE + NCIV && i < NFOE + NCIV + NCOP) {                   /* POLICE.md: a cop */
        Cop *k = &cop[i - NFOE - NCIV];
        if (!k->on || k->hp <= 0) return 0;
        k->hp -= dmg;
        k->hit_t = 0;
        k->see = true; k->seen_for = k->react;
        if (k->hp <= 0) { k->fall_t = 0; enh_police_crime(3); return 2; }
        enh_police_crime(2);
        return 1;
    }
    if (i >= NFOE && i < NFOE + NCIV) {                                 /* CIVS.md: a civilian */
        Civ *k = &civ[i - NFOE];
        if (!k->on || k->hp <= 0) return 0;
        k->hp -= dmg;
        k->hit_t = 0;
        civs_heard(k->x, k->z);
        if (k->hp <= 0) { k->fall_t = 0; enh_police_crime(2); return 2; }
        enh_police_crime(1);
        civ_flee(k, k->x - sin(k->yaw) * 100, k->z - cos(k->yaw) * 100, 9, false);
        return 1;
    }
    if (i < 0 || i >= NFOE || !foe[i].on || foe[i].hp <= 0) return 0;
    Foe *f = &foe[i];
    f->hp -= dmg;
    f->hit_t = 0;
    if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "foe %d hit %d, hp %d\n", i, dmg, f->hp);
    for (int k = 0; k < NFOE; k++) if (foe[k].on) { foe[k].alert = true; }
    if (!amb.alerted) { amb.alerted = true; amb.msg = 1; amb.msg_t = 2.5; }
    if (f->hp <= 0) { f->fall_t = 0; drop(f); return 2; }
    return 1;
}

/* the player fired: who hears it turns round */
void enh_foes_heard(double x, double z)
{
    civs_heard(x, z);                                                    /* CIVS.md */
    if (someone_near(x, z, 5000, 0)) enh_police_crime(1);               /* POLICE.md: shots near the police */
    for (int i = 0; i < NFOE; i++) {
        Foe *f = &foe[i];
        if (!f->on || f->hp <= 0) continue;
        double dx = wraps(x - f->x), dz = wraps(z - f->z);
        if (dx * dx + dz * dz < 6000.0 * 6000.0) f->alert = true;
    }
}

/* ------------------------------------------------------------------------------------------------------
 * The figures
 * ------------------------------------------------------------------------------------------------------ */

#define C_JACKET 0x5C402C                 /* brown leather */
#define C_PANTS  0x2C3652                 /* denim */
#define C_SHOES  0x161616
#define C_MASK   0x1C1C1E
#define C_EYES   0xB08468
#define C_GLOVE  0x202020
#define C_GUN    0x121212

#define NPOLY 1536
static EnhPoly polys[NPOLY];
static int npoly;
static int S_, offx_, offy_;
static bool night;
static u32 haze_rgb_;
static double haze_k;

static inline u32 shade(u32 rgb, double k, double fog)
{
    int r = (int)(((rgb >> 16) & 0xFF) * k), g = (int)(((rgb >> 8) & 0xFF) * k), b = (int)((rgb & 0xFF) * k);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    if (fog > 0) {
        r = (int)(r + (((haze_rgb_ >> 16) & 0xFF) - r) * fog);
        g = (int)(g + (((haze_rgb_ >> 8) & 0xFF) - g) * fog);
        b = (int)(b + ((haze_rgb_ & 0xFF) - b) * fog);
    }
    return (u32)r << 16 | (u32)g << 8 | (u32)b;
}

/* the figure's frame: local cm (x right, y up, z ahead) to the world (x4, height units, z4) */
typedef struct { double ox, oy, oz, cy, sy, ct, st; double fall, sc; } Frame;

static void to_world(const Frame *fr, const double *l, double *w)
{
    double k = fr->sc > 0 ? fr->sc : 1, x = l[0] * k, y = l[1] * k, z = l[2] * k;
    /* falling: about the feet's x axis, backwards */
    double cf = cos(fr->fall), sf = sin(fr->fall), y2 = y * cf + z * sf, z2 = -y * sf + z * cf;
    y = y2; z = z2;
    /* facing: + yaw turns +z towards +x */
    double wx = x * fr->cy + z * fr->sy, wz = -x * fr->sy + z * fr->cy;
    w[0] = fr->ox + wx; w[1] = fr->oy + y / YCM; w[2] = fr->oz + wz;
}

/* a block from a to b (local cm) wa x da thick at a, wb x db at b; side: the figure's right */
static void limb(const Frame *fr, const double *a, const double *b, double wa, double da, double wb, double db,
                 u32 rgb, double cam_x, double cam_y, double cam_z, double light, double fog)
{
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ul = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul < 1e-6) return;
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    double s[3] = { 1, 0, 0 }, k = s[0] * u[0];
    s[0] -= u[0] * k; s[1] -= u[1] * k; s[2] -= u[2] * k;
    double sl = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 0.2) { s[0] = 0; s[1] = 0; s[2] = 1; k = s[2] * u[2]; s[0] -= u[0] * k; s[1] -= u[1] * k; s[2] -= u[2] * k;
                    sl = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); }
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    double t[3] = { u[1] * s[2] - u[2] * s[1], u[2] * s[0] - u[0] * s[2], u[0] * s[1] - u[1] * s[0] };
    double w[8][3];
    for (int i = 0; i < 8; i++) {
        const double *e = i & 4 ? b : a;
        double ww = i & 4 ? wb : wa, dd = i & 4 ? db : da;
        double sx = i & 1 ? ww : -ww, ty = i & 2 ? dd : -dd;
        double l[3] = { e[0] + s[0] * sx + t[0] * ty, e[1] + s[1] * sx + t[1] * ty, e[2] + s[2] * sx + t[2] * ty };
        double wv[3];
        to_world(fr, l, wv);
        w[i][0] = wraps(wv[0] - cam_x); w[i][1] = wv[1] - cam_y; w[i][2] = wraps(wv[2] - cam_z);
    }
    static const int quads[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
    const double L[3] = { -0.38, 0.84, 0.39 };
    double ctr[3] = { 0, 0, 0 };
    for (int i = 0; i < 8; i++) for (int j = 0; j < 3; j++) ctr[j] += w[i][j] / 8;
    double sx[8], sy[8];
    bool ok[8];
    for (int i = 0; i < 8; i++) {
        double tx, ty, dist;
        ok[i] = enh_front_point(cam_x + w[i][0], cam_y + w[i][1], cam_z + w[i][2], &tx, &ty, &dist);
        sx[i] = (tx + offx_) * S_; sy[i] = (ty + offy_) * S_;
    }
    for (int q = 0; q < 6 && npoly < NPOLY; q++) {
        const int *qi = quads[q];
        if (!ok[qi[0]] || !ok[qi[1]] || !ok[qi[2]] || !ok[qi[3]]) continue;
        double *pa = w[qi[0]], *pb = w[qi[1]], *pc = w[qi[2]], *pd = w[qi[3]];
        double e1[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] }, e2[3] = { pd[0] - pb[0], pd[1] - pb[1], pd[2] - pb[2] };
        double n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        double fc[3] = { (pa[0] + pb[0] + pc[0] + pd[0]) / 4, (pa[1] + pb[1] + pc[1] + pd[1]) / 4, (pa[2] + pb[2] + pc[2] + pd[2]) / 4 };
        if (n[0] * (fc[0] - ctr[0]) + n[1] * (fc[1] - ctr[1]) + n[2] * (fc[2] - ctr[2]) < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        if (n[0] * fc[0] + n[1] * fc[1] + n[2] * fc[2] >= 0) continue;     /* facing away */
        double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 1e-9) continue;
        double li = 0.42 + 0.62 * fmax(0, (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / nl);
        EnhPoly *P = &polys[npoly++];
        P->n = 4;
        for (int k2 = 0; k2 < 4; k2++) { P->x[k2] = sx[qi[k2]]; P->y[k2] = sy[qi[k2]]; }
        double d = sqrt(fc[0] * fc[0] + fc[1] * fc[1] + fc[2] * fc[2]);
        P->z = d;
        P->zd = (float)(d - 14);
        P->rgb = shade(rgb, li * light, fog);
    }
}

static void body(const Body *b, double cam_x, double cam_y, double cam_z)
{
    double dx = wraps(b->x - cam_x), dz = wraps(b->z - cam_z), dist = sqrt(dx * dx + dz * dz);
    if (dist > DRAW_RANGE) return;
    Frame fr = { cam_x + dx, b->y, cam_z + dz, cos(b->yaw), sin(b->yaw), 0, 0, b->fall, b->scale };
    double light = night ? 0.5 : 1.0, fog = haze_k * fmin(1.0, fmax(0.0, (dist - 2000) / 10000.0));
    double sw = b->gait > 0 ? sin(b->phase) : 0, g = b->gait, run = g > 1.2;
    /* the legs */
    double hipL[3] = { -10, 92, 0 }, hipR[3] = { 10, 92, 0 };
    double kneeL[3] = { -10, 48, 3 + 14 * sw * g }, kneeR[3] = { 10, 48, 3 - 14 * sw * g };
    double ankL[3] = { -10, 8, -2 + 22 * sw * g }, ankR[3] = { 10, 8, -2 - 22 * sw * g };
    if (g > 0) { if (sw > 0) ankL[1] += 6 * sw * g; else ankR[1] -= 6 * sw * g; }
    if (run) { kneeL[2] += 6; kneeR[2] += 6; }
    double toeL[3] = { ankL[0], 4, ankL[2] + 20 }, toeR[3] = { ankR[0], 4, ankR[2] + 20 };
    double pel[3] = { 0, 88, 0 }, neck[3] = { 0, 148, 0 }, waist[3] = { 0, 100, 0 };
    double shL[3] = { -20, 144, 0 }, shR[3] = { 20, 144, 0 };
    double elL[3], elR[3], hL[3], hR[3], muz[3] = { 0, 0, 0 };
    if (b->arms == 1) {                                                  /* aiming */
        double e1[3] = { -9, 126, 24 }, e2[3] = { 13, 127, 22 }, h3[3] = { 2, 134, 44 }, m3[3] = { 2, 139, 68 };
        memcpy(elL, e1, sizeof elL); memcpy(elR, e2, sizeof elR); memcpy(hL, h3, sizeof hL); memcpy(hR, h3, sizeof hR);
        memcpy(muz, m3, sizeof muz);
    } else if (b->arms == 0 && b->gun >= 0) {                            /* the gun held low */
        double e1[3] = { -18, 118, 8 }, e2[3] = { 19, 117, 6 }, h3[3] = { 6, 100, 22 }, m3[3] = { 2, 88, 44 };
        memcpy(elL, e1, sizeof elL); memcpy(elR, e2, sizeof elR); memcpy(hL, h3, sizeof hL); memcpy(hR, h3, sizeof hR);
        memcpy(muz, m3, sizeof muz);
    } else if (b->arms == 3) {                                           /* hands on the head */
        double e1[3] = { -27, 160, 4 }, e2[3] = { 27, 160, 4 }, h1[3] = { -11, 172, 3 }, h2[3] = { 11, 172, 3 };
        memcpy(elL, e1, sizeof elL); memcpy(elR, e2, sizeof elR); memcpy(hL, h1, sizeof hL); memcpy(hR, h2, sizeof hR);
    } else {                                                             /* hanging, swinging with the legs */
        double aw = b->arms == 2 ? g : 0;
        if (run) {
            double e1[3] = { -21, 122, -10 - 10 * sw }, e2[3] = { 21, 122, -10 + 10 * sw };
            double h1[3] = { -16, 130, 12 - 18 * sw }, h2[3] = { 16, 130, 12 + 18 * sw };
            memcpy(elL, e1, sizeof elL); memcpy(elR, e2, sizeof elR); memcpy(hL, h1, sizeof hL); memcpy(hR, h2, sizeof hR);
        } else {
            double e1[3] = { -21, 120, 2 - 8 * sw * aw }, e2[3] = { 21, 120, 2 + 8 * sw * aw };
            double h1[3] = { -20, 98, 6 - 20 * sw * aw }, h2[3] = { 20, 98, 6 + 20 * sw * aw };
            memcpy(elL, e1, sizeof elL); memcpy(elR, e2, sizeof elR); memcpy(hL, h1, sizeof hL); memcpy(hR, h2, sizeof hR);
        }
    }
    limb(&fr, ankL, toeL, 5.5, 4, 5.5, 3, b->shoes, cam_x, cam_y, cam_z, light, fog);
    limb(&fr, ankR, toeR, 5.5, 4, 5.5, 3, b->shoes, cam_x, cam_y, cam_z, light, fog);
    u32 shin = b->dress ? b->skin : b->pants;
    limb(&fr, kneeL, ankL, b->dress ? 5 : 6.5, b->dress ? 5 : 6.5, 4.5, 4.5, shin, cam_x, cam_y, cam_z, light, fog);
    limb(&fr, kneeR, ankR, b->dress ? 5 : 6.5, b->dress ? 5 : 6.5, 4.5, 4.5, shin, cam_x, cam_y, cam_z, light, fog);
    if (b->dress) {                                                      /* a dress down to the knees */
        double hem[3] = { 0, 50, 2 + 4 * fabs(sw) * g };
        limb(&fr, waist, hem, 16, 10, 22, 14, b->pants, cam_x, cam_y, cam_z, light, fog);
    } else {
        limb(&fr, hipL, kneeL, 8, 8, 6.5, 6.5, b->pants, cam_x, cam_y, cam_z, light, fog);
        limb(&fr, hipR, kneeR, 8, 8, 6.5, 6.5, b->pants, cam_x, cam_y, cam_z, light, fog);
        limb(&fr, pel, waist, 17, 10, 17, 10, b->pants, cam_x, cam_y, cam_z, light, fog);
    }
    limb(&fr, waist, neck, b->dress ? 15 : 18, 11, b->dress ? 18 : 21, 12, b->dress ? b->pants : b->top, cam_x, cam_y, cam_z, light, fog);
    u32 sleeve = b->dress ? b->pants : b->top;
    limb(&fr, shL, elL, 5.5, 5.5, 5, 5, sleeve, cam_x, cam_y, cam_z, light, fog);
    limb(&fr, shR, elR, 5.5, 5.5, 5, 5, sleeve, cam_x, cam_y, cam_z, light, fog);
    u32 fore = b->gun >= 0 || b->mask ? b->top : b->skin;               /* the gunmen's jackets reach the wrists */
    limb(&fr, elL, hL, 4.5, 4.5, 4, 4, fore, cam_x, cam_y, cam_z, light, fog);
    limb(&fr, elR, hR, 4.5, 4.5, 4, 4, fore, cam_x, cam_y, cam_z, light, fog);
    u32 handc = b->mask ? C_GLOVE : b->skin;
    double hb[3] = { hR[0], hR[1] - 3, hR[2] - 3 }, hb2[3] = { hL[0], hL[1] - 3, hL[2] - 3 };
    limb(&fr, hb, hR, 4.5, 4.5, 4.5, 4.5, handc, cam_x, cam_y, cam_z, light, fog);
    if (b->arms != 1 && !(b->arms == 0 && b->gun >= 0))
        limb(&fr, hb2, hL, 4.5, 4.5, 4.5, 4.5, handc, cam_x, cam_y, cam_z, light, fog);
    double *hands = hR;
    if (b->gun == 0) limb(&fr, hands, muz, 2.2, 4.5, 1.8, 3.5, C_GUN, cam_x, cam_y, cam_z, light, fog);
    else if (b->gun > 0) {
        double dir[3] = { muz[0] - hands[0], muz[1] - hands[1], muz[2] - hands[2] }, l = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= l;
        if (b->gun == 1) {                                               /* the uzi: a box and its magazine */
            double a[3] = { hands[0] - dir[0] * 6, hands[1] - dir[1] * 6 + 3, hands[2] - dir[2] * 6 };
            double e[3] = { hands[0] + dir[0] * 22, hands[1] + dir[1] * 22 + 3, hands[2] + dir[2] * 22 };
            double m0[3] = { hands[0], hands[1], hands[2] }, m1[3] = { hands[0], hands[1] - 16, hands[2] + 2 };
            limb(&fr, a, e, 3, 4.5, 2.5, 3.5, C_GUN, cam_x, cam_y, cam_z, light, fog);
            limb(&fr, m0, m1, 1.6, 2.2, 1.6, 2.2, C_GUN, cam_x, cam_y, cam_z, light, fog);
        } else {                                                         /* the shotgun: long, with wood */
            double a[3] = { hands[0] - dir[0] * 34, hands[1] - dir[1] * 34 + 2, hands[2] - dir[2] * 34 };
            double e[3] = { hands[0] + dir[0] * 62, hands[1] + dir[1] * 62 + 2, hands[2] + dir[2] * 62 };
            double p0[3] = { hands[0] + dir[0] * 22, hands[1] + dir[1] * 22, hands[2] + dir[2] * 22 };
            double p1[3] = { hands[0] + dir[0] * 40, hands[1] + dir[1] * 40, hands[2] + dir[2] * 40 };
            limb(&fr, a, hands, 3, 5, 2.2, 3.5, 0x6E4626, cam_x, cam_y, cam_z, light, fog);
            limb(&fr, hands, e, 1.6, 2.2, 1.4, 1.8, C_GUN, cam_x, cam_y, cam_z, light, fog);
            limb(&fr, p0, p1, 2.6, 3.2, 2.6, 3.2, 0x6E4626, cam_x, cam_y, cam_z, light, fog);
        }
    }
    double head[3] = { 0, 152, 0 }, crown[3] = { 0, 176, 0 };
    if (b->mask) {
        double eyesA[3] = { 0, 162, 9 }, eyesB[3] = { 0, 166, 9 };
        limb(&fr, head, crown, 9, 10, 9, 10, C_MASK, cam_x, cam_y, cam_z, light, fog);
        limb(&fr, eyesA, eyesB, 6.5, 1.3, 6.5, 1.3, C_EYES, cam_x, cam_y, cam_z, light, fog);
    } else {
        double brA[3] = { 0, 163, 9.6 }, brB[3] = { 0, 164.6, 9.6 };
        limb(&fr, head, crown, 8.5, 9.5, 8.5, 9.5, b->skin, cam_x, cam_y, cam_z, light, fog);
        limb(&fr, brA, brB, 5.5, 0.8, 5.5, 0.8, 0x3A2A20, cam_x, cam_y, cam_z, light, fog);     /* the brows */
        if (b->cap) {
            double c0[3] = { 0, 172, -1 }, c1[3] = { 0, 180, -1 }, r0[3] = { 0, 173, 9 }, r1[3] = { 0, 173, 17 };
            limb(&fr, c0, c1, 9.6, 10.6, 9.4, 10.2, b->capc, cam_x, cam_y, cam_z, light, fog);
            limb(&fr, r0, r1, 8.5, 0.9, 8.5, 0.9, b->capc, cam_x, cam_y, cam_z, light, fog);    /* the peak */
        } else {
            double h0[3] = { 0, 171, -1 }, h1[3] = { 0, 179, -1 };
            limb(&fr, h0, h1, 9.4, 10.4, 9.0, 9.6, b->hair, cam_x, cam_y, cam_z, light, fog);
        }
        double bk0[3] = { 0, b->longhair ? 136 : 156, -9 }, bk1[3] = { 0, 178, -9 };
        limb(&fr, bk0, bk1, 9.2, 2.0, 9.4, 2.0, b->hair, cam_x, cam_y, cam_z, light, fog);       /* the back of the head */
    }
}

static void figure(const Foe *f, double cam_x, double cam_y, double cam_z)
{
    Body b = { f->x, f->z, f->y, f->yaw, 0, 0, f->walk_phase, 0, 1.0, f->wtype, true, false, false, false,
               C_JACKET, C_PANTS, C_SHOES, 0, 0, 0 };
    if (f->hp <= 0) { double k = fmin(1.0, f->fall_t / 0.55); b.fall = -k * k * 1.50; }
    else if (f->hit_t < 0.18) b.fall = -0.18 * (1 - f->hit_t / 0.18);
    b.gait = f->step_t > 0 && f->hp > 0 ? 1 : 0;
    b.arms = f->alert || f->hp <= 0 ? 1 : 0;
    body(&b, cam_x, cam_y, cam_z);
}

static void cop_figure(const Cop *k, double cam_x, double cam_y, double cam_z)
{
    Body b = { k->x, k->z, k->y, k->yaw, 0, 0, k->phase, 0, 1.0, 0, false, false, false, true,
               0x1E2A48, 0x1A2238, 0x141414, k->skin, 0x201810, 0x16203A };
    b.gait = k->hp <= 0 ? 0 : k->speed > 250 ? 1.6 : k->speed > 0 ? 1 : 0;
    b.arms = k->hp <= 0 ? 0 : k->task == 1 && wnt.stars >= 2 ? 1 : 0;
    if (k->hp <= 0) { double q = fmin(1.0, k->fall_t / 0.55); b.fall = -q * q * 1.50; }
    else if (k->hit_t < 0.18) b.fall = -0.18 * (1 - k->hit_t / 0.18);
    else if (b.gait > 1.2) b.fall = 0.12;
    body(&b, cam_x, cam_y, cam_z);
}

static void civ_figure(const Civ *k, double cam_x, double cam_y, double cam_z)
{
    Body b = k->look;
    b.x = k->x; b.z = k->z; b.y = k->y; b.yaw = k->yaw; b.phase = k->phase;
    b.gait = k->hp <= 0 ? 0 : k->mode == 3 ? 1.6 : k->mode == 2 ? 0 : 1;
    b.arms = k->hp <= 0 ? 0 : k->mode == 3 ? (k->after >= 10 ? 2 : 3) : 2;
    b.fall = k->mode == 3 && k->hp > 0 ? 0.12 : 0;
    if (k->hp <= 0) { double q = fmin(1.0, k->fall_t / 0.55); b.fall = -q * q * 1.50; }
    else if (k->hit_t < 0.18) b.fall = -0.18 * (1 - k->hit_t / 0.18);
    body(&b, cam_x, cam_y, cam_z);
}

/* a dropped gun lying on the ground (WEAPON.md) */
static void pickup_model(const Pick *k, double cam_x, double cam_y, double cam_z)
{
    double dx = wraps(k->x - cam_x), dz = wraps(k->z - cam_z), dist = sqrt(dx * dx + dz * dz);
    if (dist > DRAW_RANGE) return;
    Frame fr = { cam_x + dx, k->y, cam_z + dz, cos(k->yaw), sin(k->yaw), 0, 0, 0, 1 };
    double light = night ? 0.5 : 1.0, fog = haze_k * fmin(1.0, fmax(0.0, (dist - 2000) / 10000.0));
    static const double len[3] = { 20, 30, 95 };
    double a[3] = { 0, 3, -len[k->wtype] / 2 }, b[3] = { 0, 3, len[k->wtype] / 2 };
    limb(&fr, a, b, k->wtype == 2 ? 2.2 : 2.6, 2.6, k->wtype == 2 ? 1.6 : 2.4, 2.4, C_GUN, cam_x, cam_y, cam_z, light, fog);
    if (k->wtype == 2) {
        double s0[3] = { 0, 3, -len[2] / 2 }, s1[3] = { 0, 4, -len[2] / 2 + 32 };
        limb(&fr, s0, s1, 3, 4, 3, 3, 0x6E4626, cam_x, cam_y, cam_z, light, fog);
    } else {
        double g0[3] = { 0, 3, -3 }, g1[3] = { 0, 3.5, -3 + (k->wtype == 1 ? 18 : 10) };
        limb(&fr, g0, g1, 1.5, 6, 1.5, 6, C_GUN, cam_x, cam_y, cam_z, light, fog);    /* the grip, lying flat */
    }
}

void enh_foes_draw(u32 *xrgb, const EnhSnap *s, const EnhView *v, int em, int offx, int offy, int y0, int y1, u32 haze)
{
    if (s->menu_preview) return;
    S_ = enh_scale; offx_ = offx; offy_ = offy;
    night = !s->day;
    haze_rgb_ = haze;
    haze_k = enh_haze / 100.0;
    npoly = 0;
    for (int i = 0; i < NCIV; i++) if (civ[i].on) civ_figure(&civ[i], v->cam_x4, v->cam_y, v->cam_z4);
    for (int i = 0; i < NCOP; i++) if (cop[i].on) cop_figure(&cop[i], v->cam_x4, v->cam_y, v->cam_z4);
    for (int i = 0; i < NCOP; i++) {                                    /* the cops' muzzle flashes */
        const Cop *k = &cop[i];
        if (!k->on || k->hp <= 0 || k->flash_t > 0.05) continue;
        double dx = wraps(k->x - v->cam_x4), dz = wraps(k->z - v->cam_z4);
        Frame fr = { v->cam_x4 + dx, k->y, v->cam_z4 + dz, cos(k->yaw), sin(k->yaw), 0, 0, 0, 1 };
        double m[3] = { 2, 139, 72 }, w[3], tx, ty, dist;
        to_world(&fr, m, w);
        if (!enh_front_point(w[0], w[1], w[2], &tx, &ty, &dist) || dist < 10) continue;
        double r = fmax(2.0, atan2(16, dist) * RAD2PX * enh_scene_zoom() * enh_scale);
        enh_glow(xrgb, em, (tx + offx) * enh_scale, (ty + offy) * enh_scale, r * 1.4, 0xFF9030, 0.9);
    }
    if (!amb.on) { enh_polys(xrgb, em, polys, npoly, offx, offy, y0, y1); return; }
    for (int i = 0; i < NFOE; i++) if (foe[i].on) figure(&foe[i], v->cam_x4, v->cam_y, v->cam_z4);
    for (int i = 0; i < NPICK; i++) if (pick[i].on) pickup_model(&pick[i], v->cam_x4, v->cam_y, v->cam_z4);
    enh_polys(xrgb, em, polys, npoly, offx, offy, y0, y1);
    /* the dropped guns glint, so that they are found */
    for (int i = 0; i < NPICK; i++) {
        const Pick *k = &pick[i];
        double tx, ty, dist;
        if (!k->on || !enh_front_point(k->x, k->y + 12, k->z, &tx, &ty, &dist) || dist < 20 || dist > 6000) continue;
        double r = fmax(2.0, atan2(14, dist) * RAD2PX * enh_scene_zoom() * enh_scale) * (0.8 + 0.4 * sin(k->t * 6));
        enh_glow(xrgb, em, (tx + offx) * enh_scale, (ty + offy) * enh_scale, r, 0xFFE070, 0.55);
    }
    /* their muzzle flashes */
    for (int i = 0; i < NFOE; i++) {
        const Foe *f = &foe[i];
        if (!f->on || f->hp <= 0 || f->flash_t > 0.05) continue;
        double dx = wraps(f->x - v->cam_x4), dz = wraps(f->z - v->cam_z4);
        Frame fr = { v->cam_x4 + dx, f->y, v->cam_z4 + dz, cos(f->yaw), sin(f->yaw), 0, 0, 0, 1 };
        double m[3] = { 2, 139, 72 }, w[3], tx, ty, dist;
        to_world(&fr, m, w);
        if (!enh_front_point(w[0], w[1], w[2], &tx, &ty, &dist) || dist < 10) continue;
        double r = fmax(2.0, atan2(16, dist) * RAD2PX * enh_scene_zoom() * enh_scale);
        enh_glow(xrgb, em, (tx + offx) * enh_scale, (ty + offy) * enh_scale, r * 1.6, 0xFF9030, 0.9);
        enh_glow(xrgb, em, (tx + offx) * enh_scale, (ty + offy) * enh_scale, r * 0.7, 0xFFF0C0, 1.0);
    }
}

/* ------------------------------------------------------------------------------------------------------
 * Health, hits from where, messages
 * ------------------------------------------------------------------------------------------------------ */

void enh_foes_hud(u32 *xrgb, const EnhSnap *s, const EnhView *v, int em)
{
    if (s->menu_preview || !enh_keys_racing()) return;
    int S = enh_scale, FW = (320 + 2 * em) * S, FH = 200 * S;
    /* red at the edges: a fresh hit, and low health */
    double red = fmax(hurt_amount * 0.75, health < 30 ? (30 - health) / 30.0 * 0.35 : 0);
    if (red > 0.01) {
        int band = 60 * S;
        for (int y = 0; y < FH; y++) {
            int dy = y < band ? band - y : y >= FH - band ? y - (FH - band) + 1 : 0;
            for (int x = 0; x < FW; x++) {
                int dx = x < band ? band - x : x >= FW - band ? x - (FW - band) + 1 : 0;
                if (!dx && !dy) { x = FW - band - 1; continue; }   /* the middle: on to the right band */
                double e = fmax(dx, dy) / (double)band, w = red * e * e * 0.8;
                u32 *o = xrgb + (size_t)y * FW + x;
                u32 c = *o;
                int r = (int)(((c >> 16) & 0xFF) * (1 - w) + 200 * w), g = (int)(((c >> 8) & 0xFF) * (1 - w)), b = (int)((c & 0xFF) * (1 - w));
                *o = (u32)r << 16 | (u32)g << 8 | (u32)b;
            }
        }
    }
    /* where the hit came from: a chevron round the centre */
    if (since_hurt < 1.2) {
        double a = hurt_bear - v->heading * A2RAD, w = 1 - since_hurt / 1.2;
        double cx = (em + 160) * S, cy = 100.0 * S, rr = 62.0 * S, sa = sin(a), ca = cos(a);
        double tx = cx + sa * rr, ty = cy - ca * rr * 0.8;
        double bx = cx + sa * (rr - 9 * S), by = cy - ca * (rr - 9 * S) * 0.8;
        double px = -ca * 7 * S, py = -sa * 7 * S * 0.8;
        enh_line(xrgb, em, bx + px, by + py, tx, ty, 2.2 * S, 0xFF2010, w);
        enh_line(xrgb, em, bx - px, by - py, tx, ty, 2.2 * S, 0xFF2010, w);
    }
    /* health: on foot bottom left, in the car under the left panel, when it is not full or in an ambush */
    bool show = s->foot || health < 99.5 || amb.on;
    if (show) {
        int x0 = s->foot ? em + 8 : em + 6, y0 = s->foot ? 186 : 19, wd = 62, hg = 5;
        for (int y = (y0 - 1) * S; y < (y0 + hg + 1) * S; y++)
            for (int x = (x0 - 1) * S; x < (x0 + wd + 1) * S; x++) {
                if (x < 0 || x >= FW || y < 0 || y >= FH) continue;
                u32 *o = xrgb + (size_t)y * FW + x;
                bool in = x >= x0 * S && x < (x0 + wd) * S && y >= y0 * S && y < (y0 + hg) * S;
                double f = (x - x0 * S) / (double)(wd * S);
                if (!in) { *o = 0x101010; continue; }
                u32 col = health > 60 ? 0x50D050 : health > 30 ? 0xE0C040 : 0xE04030;
                *o = f * 100 < health ? col : 0x303030;
            }
    }
    /* POLICE.md: the wanted stars (flashing while they search), the police's messages, the way to the car */
    if (wnt.stars > 0) {
        bool on = wnt.seen_t < 1.5 || fmod(wnt.seen_t, 0.6) < 0.35;
        for (int i = 0; i < 3; i++)
            enh_text(xrgb, FW, FH, S, "*", em + 160 - 26 + i * 18, 20, 2, i < wnt.stars ? (on ? 0xFFD030 : 0x806818) : 0x303030, false);
    }
    if (wnt.msg_t > 0) {
        static const char *m[] = { "", "WANTED!", "LOST THEM", "BUSTED!", "YOUR CAR IS BEING STOLEN!" };
        const char *t = m[wnt.msg];
        enh_text(xrgb, FW, FH, S, t, em + 160 - enh_text_width(t, wnt.msg == 3 ? 2 : 1) / 2, wnt.msg == 3 ? 80 : 34,
                 wnt.msg == 3 ? 2 : 1, wnt.msg == 2 ? 0x80FF80 : 0xFF6040, true);
    }
    {
        double cx, cz, cy;
        u16 ch;
        if (s->foot && foot_car_pos(&cx, &cz, &cy, &ch)) {
            double dx = wraps(cx - v->cam_x4), dz = wraps(cz - v->cam_z4), d = sqrt(dx * dx + dz * dz);
            if (d > 1500) {                                              /* a chevron to the car, and how far */
                double a = atan2(dx, dz) - v->heading * A2RAD, sa = sin(a), ca = cos(a);
                double qx = (em + 160) * S, qy = 100.0 * S, rr = 74.0 * S;
                double tx = qx + sa * rr, ty = qy - ca * rr * 0.8, bx = qx + sa * (rr - 8 * S), by = qy - ca * (rr - 8 * S) * 0.8;
                double ox = -ca * 6 * S, oy = -sa * 6 * S * 0.8;
                enh_line(xrgb, em, bx + ox, by + oy, tx, ty, 2.0 * S, 0xFFE060, 0.85);
                enh_line(xrgb, em, bx - ox, by - oy, tx, ty, 2.0 * S, 0xFFE060, 0.85);
                char buf[24];
                snprintf(buf, sizeof buf, "CAR %dM", (int)(d / 100));
                int lx = (int)(tx / S) - enh_text_width(buf, 1) / 2, ly = (int)(ty / S) + (ca > 0 ? 6 : -12);
                enh_text(xrgb, FW, FH, S, buf, lx, ly, 1, 0xFFE060, false);
            }
        }
    }
    /* messages */
    if (amb.msg_t > 0) {
        const char *t = amb.msg == 1 ? "AMBUSH!" : "AMBUSH CLEARED  +1 LIFE";
        bool on = amb.msg != 1 || fmod(amb.msg_t, 0.5) > 0.2;
        if (on) enh_text(xrgb, FW, FH, S, t, em + 160 - enh_text_width(t, 1) / 2, 30, 1, amb.msg == 1 ? 0xFF6040 : 0x80FF80, true);
    }
}
