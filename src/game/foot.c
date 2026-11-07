/* OG: on foot (FOOT.md). Getting out of the stopped car, walking about in the first person, getting back in.
 *
 * The walker takes over the player's position: DS:pos_x/z/y (1/128 of a sprite unit; the feet), the sprite slot 0
 * that the camera follows (sprite_x/z/y = the eye) and the headings, so the camera, the world build, the ground
 * under the camera and the far ring follow it as they follow the car. car_physics does not run; the car's own
 * state is put aside at the exit and back at the entry. The car stays as object 0 (obj_x/z/y8/heading are not
 * touched), which moving_objects_update then builds like any other vehicle instead of skipping it.
 *
 * Units: x / z in the vertices' x4 units (4 a sprite unit, about a centimetre: a car is about 460 long), heights in
 * the vertices' y units, which are about 2.3 times larger: a car is about 50 high, the driver's eye 46 over the
 * ground (the world is drawn flatter than it is). The walker meets the world through the game's own face and vertex arrays
 * (the last frame's): the ground is the highest walkable polygon under the feet, walls are the steep polygons
 * (vehicles included), trees and rocks are the sprites of collision class 60h. */
#include "game/game.h"
#include "game/foot.h"
#include "enhanced/enhanced.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define F_EYE         70.0     /* eye over the feet (1.4 times a car's roof, as a person's eye) */
#define F_HEIGHT      78.0     /* the head: walls lower than F_STEP over the feet are stepped onto */
#define F_STEP        18.0
#define F_RADIUS      28.0     /* the body against walls and vehicles */
#define F_TREE        36.0     /* a trunk or a rock, on top of the body */
#define F_WALK       320.0     /* units a second at full stick */
#define F_RUN        700.0
#define F_ACCEL_TAU    0.09    /* s: the speed follows the stick */
#define F_AIR_TAU      0.80
#define F_GRAVITY    900.0     /* height units / s^2 (about twice the earth's: a game's jump) */
#define F_JUMP       230.0     /* take-off speed: about 29 units (65 cm) high */
#define F_TURN     34000.0     /* heading units a second at full stick (about 190 degrees) */
#define F_LOOK       170.0     /* rows (of the projection at zoom 1: 5.7 a degree) a second at full stick */
#define F_LOOK_UP    150.0
#define F_LOOK_DOWN  260.0
#define F_MOUSE_TURN  20.0     /* heading units a mouse pixel (0.11 degrees) */
#define F_MOUSE_LOOK   0.62    /* rows a mouse pixel */
#define F_ENTER      440.0     /* from the car's centre (its length is about 460) */
#define F_SPAWN      135.0     /* beside the car's centre line, then pushed clear */
/* the leg's map: 32 x 16 cells of 4096 x4 units; its coordinates wrap at the edges (z at exactly the map's height),
 * which no car ever reaches, but a walker did and came out at the far side with no way back: kept inside */
#define F_MAP_X      131072.0
#define F_MAP_Z      65536.0
#define F_EDGE       1024.0    /* from the map's edge */
static bool keep_on_map(double *x, double *z);

#define NOGROUND -1e9

static bool active;
static double vx, vz, vy;               /* units a second */
static bool grounded, jump_held;
static double live_yaw, look_rows;      /* turned at the display rate (foot_view_update) */
static bool view_live;                  /* the enhanced view turns it (else foot_physics does) */
static FILE *flog;

/* the car's state while the walker has the player's position */
static struct {
    u8 pos[48];                         /* DS:125E pos_x/z/y, then pos_hist1..3 */
    u16 sprite_x, sprite_z, sprite_y;
    u8 frac_x, frac_z;
    u16 view_heading, body_heading, course;
    s16 pitch;
    u16 eye_height, near_depth, near_dy;
    u16 ground_height, ground_y_prev;
    u8 engine_off;
    double fine_heading, view_fine;
} car;

bool foot_active(void) { return active; }

static double x4_of(u32 pos) { return (double)pos / 32.0; }        /* pos (1/128 sprite unit) -> x4 units */

static void log_open(void)
{
    static bool done;
    if (done) return;
    done = true;
    const char *p = SDL_getenv("TD3_FOOT_LOG");
    if (p) flog = fopen(p, "w");
}

/* ---------------------------------------------------------------------------------------------------------
 * The world around the walker, from the game's arrays
 * --------------------------------------------------------------------------------------------------------- */

typedef struct { int n; u8 type; double x[4], y[4], z[4]; } Poly;   /* relative to the walker (x, z) */

static bool face_get(int f, double px, double pz, Poly *p)
{
    u16 fseg = DSW(DS_face_block + 2), fbase = DSW(DS_face_block);
    u16 r[4];
    for (int k = 0; k < 4; k++) r[k] = rd16(fseg, (u16)(fbase + 10 * f + 2 * k));
    p->n = (r[0] >> 14) + 1;
    p->type = (u8)(r[3] >> 11);
    u16 nv = DSW(DS_vert_count);
    u16 ux = (u16)(s32)floor(px), uz = (u16)(s32)floor(pz);
    double fx = px - floor(px), fz = pz - floor(pz);
    for (int k = 0; k < p->n; k++) {
        u16 v = r[k] & 0x7FF;
        if (v >= nv || v >= 0x640) return false;
        p->x[k] = (s16)(u16)(DSW(DS_vert_x + 2 * v) - ux) - fx;
        p->z[k] = (s16)(u16)(DSW(DS_vert_z + 2 * v) - uz) - fz;
        p->y[k] = DSS(DS_vert_y + 2 * v);
    }
    return true;
}

/* a polygon's normal (unnormalised, y up) */
static void poly_normal(const Poly *p, double *nx, double *ny, double *nz)
{
    double ux = p->x[1] - p->x[0], uy = p->y[1] - p->y[0], uz = p->z[1] - p->z[0];
    double wx = p->x[2] - p->x[0], wy = p->y[2] - p->y[0], wz = p->z[2] - p->z[0];
    *nx = uy * wz - uz * wy;
    *ny = uz * wx - ux * wz;
    *nz = ux * wy - uy * wx;
}

static bool steep(const Poly *p)
{
    double nx, ny, nz;
    poly_normal(p, &nx, &ny, &nz);
    double h = sqrt(nx * nx + nz * nz);
    return h > 1.5 * fabs(ny) && h > 1e-6;
}

/* The highest walkable surface under (px, pz) no higher than `top`; its type in *type (0xFF none). */
static double ground_at(double px, double pz, double top, u8 *type)
{
    double best = NOGROUND;
    *type = 0xFF;
    int nf = DSW(DS_face_count);
    if (nf > 0x640) nf = 0x640;
    for (int f = 0; f < nf; f++) {
        Poly p;
        if (!face_get(f, px, pz, &p) || p.n < 3 || p.type == 0) continue;
        if (steep(&p)) continue;
        for (int t = 0; t + 2 < p.n; t++) {                       /* the fan (0, 1 + t, 2 + t) */
            int a = 0, b = 1 + t, c = 2 + t;
            double c0 = p.x[a] * p.z[b] - p.z[a] * p.x[b], c1 = p.x[b] * p.z[c] - p.z[b] * p.x[c];
            double c2 = p.x[c] * p.z[a] - p.z[c] * p.x[a];
            if (!((c0 >= 0 && c1 >= 0 && c2 >= 0) || (c0 <= 0 && c1 <= 0 && c2 <= 0))) continue;
            double ux = p.x[b] - p.x[a], uy = p.y[b] - p.y[a], uz = p.z[b] - p.z[a];
            double wx = p.x[c] - p.x[a], wy = p.y[c] - p.y[a], wz = p.z[c] - p.z[a];
            double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            if (fabs(ny) < 1e-6) continue;
            double h = p.y[a] - (nx * -p.x[a] + nz * -p.z[a]) / ny;
            if (h <= top && h > best) { best = h; *type = p.type; }
        }
    }
    return best;
}

/* closest point to the origin on the segment a-b (x, z) */
static void seg_closest(double ax, double az, double bx, double bz, double *qx, double *qz)
{
    double dx = bx - ax, dz = bz - az, l = dx * dx + dz * dz, t = 0;
    if (l > 1e-9) t = -(ax * dx + az * dz) / l;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    *qx = ax + t * dx;
    *qz = az + t * dz;
}

/* Pushes (px, pz) out of the walls (steep polygons and the lines of solid faces whose height range meets the
 * body) and the trees and rocks. Returns true when something was in the way. */
static bool resolve(double *px, double *pz, double feet)
{
    bool hit = false;
    double lo = feet + F_STEP, hi = feet + F_HEIGHT;
    int nf = DSW(DS_face_count);
    if (nf > 0x640) nf = 0x640;
    for (int it = 0; it < 3; it++) {
        bool moved = false;
        for (int f = 0; f < nf; f++) {
            Poly p;
            if (!face_get(f, *px, *pz, &p) || p.type == 0 || p.n < 2) continue;
            if (p.type >= 0x1C && p.type <= 0x1F) continue;             /* zones (weather, the finish) */
            if (p.n == 2 ? p.type != 1 : !steep(&p)) continue;
            double ylo = 1e9, yhi = -1e9, near = 1e9;
            for (int k = 0; k < p.n; k++) {
                if (p.y[k] < ylo) ylo = p.y[k];
                if (p.y[k] > yhi) yhi = p.y[k];
                double d = fabs(p.x[k]) + fabs(p.z[k]);
                if (d < near) near = d;
            }
            if (yhi < lo || ylo > hi) continue;
            if (near > 4000) continue;                                   /* far away (a whole cell) */
            double bq = 1e9, bx = 0, bz = 0;
            for (int k = 0; k < p.n; k++) {
                int k2 = (k + 1) % p.n;
                if (p.n == 2 && k == 1) break;
                double qx, qz;
                seg_closest(p.x[k], p.z[k], p.x[k2], p.z[k2], &qx, &qz);
                double d = sqrt(qx * qx + qz * qz);
                if (d < bq) { bq = d; bx = qx; bz = qz; }
            }
            if (bq >= F_RADIUS) continue;
            double ox, oz;
            if (bq > 1e-3) { ox = -bx / bq; oz = -bz / bq; }
            else {                                                       /* on the line: along its normal */
                double nx, ny, nz;
                if (p.n >= 3) poly_normal(&p, &nx, &ny, &nz); else { nx = -(p.z[1] - p.z[0]); nz = p.x[1] - p.x[0]; }
                double l = sqrt(nx * nx + nz * nz);
                if (l < 1e-9) continue;
                ox = nx / l; oz = nz / l;
            }
            *px += ox * (F_RADIUS - bq + 0.5);
            *pz += oz * (F_RADIUS - bq + 0.5);
            moved = hit = true;
        }
        /* trees, rocks and the crossing barrier (sprite collision class 60h) */
        int n = DSW(DS_sprite_count) + DSB(DS_sprite_children);
        if (n > 320) n = 320;
        u16 ux = (u16)(s32)floor(*px), uz = (u16)(s32)floor(*pz);
        for (int i = 9; i < n; i++) {
            u16 id = DSW(DS_sprite_id + 2 * i);
            if ((u8)id == 0 || ((id >> 8) & 0xE0) != 0x60) continue;
            double sx = (s16)(u16)((u16)(DSW(DS_sprite_x + 2 * i) << 2) - ux) - (*px - floor(*px));
            double sz = (s16)(u16)((u16)(DSW(DS_sprite_z + 2 * i) << 2) - uz) - (*pz - floor(*pz));
            double sy = DSS(DS_sprite_y + 2 * i);
            if (fabs(sy - feet) > 400) continue;
            double d = sqrt(sx * sx + sz * sz), r = F_RADIUS + F_TREE;
            if (d >= r || d < 1e-3) continue;
            *px -= sx / d * (r - d + 0.5);
            *pz -= sz / d * (r - d + 0.5);
            moved = hit = true;
        }
        if (!moved) break;
    }
    return hit;
}

/* ---------------------------------------------------------------------------------------------------------
 * Getting out and in
 * --------------------------------------------------------------------------------------------------------- */

static void publish(void)
{
    DSW(DS_sprite_x) = (u16)(DSL(DS_pos_x) >> 7) & 0x7FFF;
    DSB(DS_pos_frac_x) = (u8)((DSB(DS_pos_x) & 0x7F) << 1);
    DSW(DS_sprite_z) = (u16)(DSL(DS_pos_z) >> 7) & 0x3FFF;
    DSB(DS_pos_frac_z) = (u8)((DSB(DS_pos_z) & 0x7F) << 1);
    DSW(DS_sprite_y) = (u16)((u16)(DSL(DS_pos_y) >> 7) + DSW(DS_car_eye_height));
    live_yaw -= floor(live_yaw / 65536.0) * 65536.0;
    u16 h = (u16)floor(live_yaw);
    DSW(DS_body_heading) = h;
    DSW(DS_course) = h;
    DSW(DS_view_heading) = h & 0xFFC0;
    sim_fine_heading = live_yaw;
    sim_view_heading = live_yaw;
    DSW(DS_pitch) = 0;
    DSW(DS_car_speed) = 0;
}

static void car_save(void)
{
    memcpy(car.pos, mp(DGROUP, DS_pos_x), sizeof car.pos);
    car.sprite_x = DSW(DS_sprite_x); car.sprite_z = DSW(DS_sprite_z); car.sprite_y = DSW(DS_sprite_y);
    car.frac_x = DSB(DS_pos_frac_x); car.frac_z = DSB(DS_pos_frac_z);
    car.view_heading = DSW(DS_view_heading); car.body_heading = DSW(DS_body_heading); car.course = DSW(DS_course);
    car.pitch = DSS(DS_pitch);
    car.eye_height = DSW(DS_car_eye_height); car.near_depth = DSW(DS_near_depth); car.near_dy = DSW(DS_near_dy);
    car.ground_height = DSW(DS_ground_height); car.ground_y_prev = DSW(DS_ground_y_prev);
    car.engine_off = DSB(DS_engine_off);
    car.fine_heading = sim_fine_heading; car.view_fine = sim_view_heading;
}

static void car_restore(void)
{
    memcpy(mp(DGROUP, DS_pos_x), car.pos, sizeof car.pos);
    DSW(DS_sprite_x) = car.sprite_x; DSW(DS_sprite_z) = car.sprite_z; DSW(DS_sprite_y) = car.sprite_y;
    DSB(DS_pos_frac_x) = car.frac_x; DSB(DS_pos_frac_z) = car.frac_z;
    DSW(DS_view_heading) = car.view_heading; DSW(DS_body_heading) = car.body_heading; DSW(DS_course) = car.course;
    DSS(DS_pitch) = car.pitch;
    DSW(DS_car_eye_height) = car.eye_height; DSW(DS_near_depth) = car.near_depth; DSW(DS_near_dy) = car.near_dy;
    DSW(DS_ground_height) = car.ground_height; DSW(DS_ground_y_prev) = car.ground_y_prev;
    DSB(DS_engine_off) = car.engine_off;
    sim_fine_heading = car.fine_heading; sim_view_heading = car.view_fine;
    DSW(DS_car_speed) = 0;
}

static bool in_race(void)
{
    return DSB(DS_menu_preview) == 0 && DSW(DS_race_state) == 1 && DSB(DS_crashed) == 0 &&
           DSB(DS_ext_view) == 0 && DSB(DS_replay_state) == 0 && DSB(DS_frozen) == 0;
}

/* OG: getting out at speed. Up to F_OUT_SPEED the car stops where the walker gets out; faster, the request waits
 * while the car brakes itself hard (the game's hard brake and F_BAIL_DECEL on top, foot_bail_controls) and the
 * walker is out once it is that slow and on the ground. The gas, pressed anew, drives on instead. Before, the car
 * had to stand (car_speed <= 2) and a brake held too long at a standstill put the reverse in (REVERSE.md). */
#define F_OUT_SPEED     20     /* car_speed (about 0.77 km/h a unit, 2048 of speed_long): 15 km/h */
#define F_BAIL_DECEL 117000.0  /* speed_long a second: about 12 m/s^2 */
static bool bail, bail_gas_free;
static u64 bail_at;

static bool can_out(void) { return DSW(DS_car_speed) <= F_OUT_SPEED && DSB(DS_on_ground) != 0; }

u8 foot_bail_controls(u8 cl)
{
    if (!bail) return cl;
    if (!(cl & 1)) bail_gas_free = true;
    else if (bail_gas_free) {                                        /* the gas pressed again: drive on */
        bail = false;
        if (flog) fprintf(flog, "out: cancelled by the gas\n");
        return cl;
    }
    u64 now = host_time_ns();
    double dt = (now - bail_at) / 1e9;
    bail_at = now;
    if (dt > 0.1) dt = 0.1;
    if (DSB(DS_on_ground)) {
        s32 v = DSSL(DS_speed_long), d = (s32)(F_BAIL_DECEL * dt);
        DSSL(DS_speed_long) = v > d ? v - d : v < -d ? v + d : 0;
    }
    DSB(DS_throttle) = 0;
    return (u8)((cl & ~3) | 0x20);                                   /* the hard brake, no pedal (and no reverse) */
}

static void get_out(void)
{
    log_open();
    if (!can_out()) {                                                /* brake first (above) */
        if (flog) fprintf(flog, "out? t %.2f speed %u on ground %u gear %u: braking\n", host_time_ns() / 1e9,
                          DSW(DS_car_speed), DSB(DS_on_ground), DSB(DS_gear));
        bail = true;
        bail_gas_free = false;
        bail_at = host_time_ns();
        return;
    }
    if (flog) fprintf(flog, "out at speed %u%s\n", DSW(DS_car_speed), bail ? " (braked)" : "");
    bail = false;
    car_save();
    DSSL(DS_speed_long) = 0;
    DSSL(DS_vel_x) = DSSL(DS_vel_z) = DSSL(DS_vel_y) = 0;
    DSB(DS_throttle) = 0;
    DSB(DS_brake) = 0;

    /* the eye of the walker; the car's own eye height (about a metre) told the feet where the ground is */
    double feet = (s32)DSL(DS_pos_y) / 128.0;
    DSW(DS_car_eye_height) = (u16)F_EYE;
    DSB(DS_engine_off) = 1;                                          /* the engine is switched off */
    double h = car.fine_heading * (2.0 * M_PI / 65536.0);
    double cx = x4_of(DSL(DS_pos_x)), cz = x4_of(DSL(DS_pos_z));
    /* the driver's side (left), else the other side, else behind; pushed clear of the car */
    static const double side[3][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 } };
    double px = cx, pz = cz;
    for (int s = 0; s < 3; s++) {
        double lx = side[s][0] * cos(h) + side[s][1] * sin(h);       /* right = (cos h, -sin h) */
        double lz = -side[s][0] * sin(h) + side[s][1] * cos(h);
        double d = s == 2 ? 330.0 : F_SPAWN;
        px = cx + lx * d;
        pz = cz + lz * d;
        double qx = px, qz = pz;
        resolve(&qx, &qz, feet);
        double mx = qx - px, mz = qz - pz;
        if (mx * mx + mz * mz < 60.0 * 60.0) { px = qx; pz = qz; break; }
    }
    keep_on_map(&px, &pz);                                           /* OG: not off the map's edge */
    DSL(DS_pos_x) = (u32)(s32)floor(px * 32.0 + 0.5);
    DSL(DS_pos_z) = (u32)(s32)floor(pz * 32.0 + 0.5);
    for (int i = 0; i < 3; i++) {
        DSL(DS_pos_hist1 + 4 * i) = DSL(DS_pos_x + 4 * i);
        DSL(DS_pos_hist2 + 4 * i) = DSL(DS_pos_x + 4 * i);
        DSL(DS_pos_hist3 + 4 * i) = DSL(DS_pos_x + 4 * i);
    }
    live_yaw = car.fine_heading;
    look_rows = 0;
    vx = vz = vy = 0;
    grounded = true;
    jump_held = true;
    active = true;
    host_set_foot(true);
    publish();
    log_open();
    if (flog) fprintf(flog, "out: t %.2f car x4 %.1f z4 %.1f feet %.1f heading %.0f eye %u -> walker %.1f %.1f\n",
                      host_time_ns() / 1e9, cx, cz, feet, car.fine_heading, car.eye_height, px, pz);
}

/* the walker's distance from the car (object 0), x4 units */
static double car_distance(void)
{
    double px = x4_of(DSL(DS_pos_x)), pz = x4_of(DSL(DS_pos_z));
    double dx = (s16)(u16)((u16)(DSW(DS_obj_x) << 2) - (u16)(s32)floor(px));
    double dz = (s16)(u16)((u16)(DSW(DS_obj_z) << 2) - (u16)(s32)floor(pz));
    if (fabs(DSW(DS_obj_x) * 4.0 - px) > 32000) return 1e9;          /* OG: 655 m east-west on, not beside it */
    return sqrt(dx * dx + dz * dz);
}

bool foot_near_car(void) { return active && car_distance() <= F_ENTER; }

static void leave_foot(void)
{
    active = false;
    host_set_foot(false);
    /* object 0's ranges from the walking frames (the cockpit view does not build it) */
    DSW(DS_obj_vert_base) = DSW(DS_obj_vert_count) = 0;
    DSW(DS_obj_face_base) = DSW(DS_obj_face_count) = 0;
}

static void get_in(void)
{
    double d = car_distance();
    if (flog) fprintf(flog, "in? distance %.1f\n", d);
    if (d > F_ENTER) return;
    car_restore();
    replay_clear();                                                  /* no replay of the walk */
    leave_foot();
}

void foot_frame_begin(void)
{
    bool req = host_foot_request();
    if (!in_race()) { bail = false; return; }
    if (bail && can_out()) { get_out(); return; }                    /* braked enough: out */
    if (!req) return;
    if (!active) { if (!bail) get_out(); } else get_in();
}

void foot_reset(void)
{
    bail = false;
    if (!active) return;
    car_restore();
    leave_foot();
}

/* ---------------------------------------------------------------------------------------------------------
 * Walking
 * --------------------------------------------------------------------------------------------------------- */

static void turn(const HostFootInput *in, double dt, double sens, double mdx, double mdy)
{
    double t = in->turn, l = in->look;
    live_yaw += (t * fabs(t) * F_TURN * dt + mdx * F_MOUSE_TURN) * sens;   /* fine near the centre */
    look_rows += (l * fabs(l) * F_LOOK * dt - mdy * F_MOUSE_LOOK) * sens;
    if (look_rows > F_LOOK_UP) look_rows = F_LOOK_UP;
    if (look_rows < -F_LOOK_DOWN) look_rows = -F_LOOK_DOWN;
    live_yaw -= floor(live_yaw / 65536.0) * 65536.0;
}

void foot_view_update(double dt, double sens, HostFootInput *in, double *yaw, double *look)
{
    if (dt < 0 || dt > 0.25) dt = 0.25;
    view_live = true;
    double mdx, mdy;
    host_foot_input(in);
    host_foot_mouse(&mdx, &mdy);
    turn(in, dt, sens, mdx, mdy);
    *yaw = live_yaw;
    *look = look_rows;
}

bool foot_car_pos(double *x4, double *z4, double *y, u16 *heading)
{
    if (!active) return false;
    u32 px, pz, py;
    memcpy(&px, car.pos, 4); memcpy(&pz, car.pos + 4, 4); memcpy(&py, car.pos + 8, 4);
    *x4 = px / 32.0; *z4 = pz / 32.0; *y = (s32)py / 128.0;
    *heading = car.body_heading;
    return true;
}

void foot_move_car(double x4, double z4, double y, u16 heading)
{
    if (!active) return;
    keep_on_map(&x4, &z4);
    u32 px = (u32)(s32)floor(x4 * 32.0 + 0.5), pz = (u32)(s32)floor(z4 * 32.0 + 0.5), py = (u32)(s32)floor(y * 128.0 + 0.5);
    for (int i = 0; i < 4; i++) {                                    /* the position and its history */
        memcpy(car.pos + 12 * i, &px, 4); memcpy(car.pos + 12 * i + 4, &pz, 4); memcpy(car.pos + 12 * i + 8, &py, 4);
    }
    car.sprite_x = (u16)(px >> 7) & 0x7FFF;
    car.sprite_z = (u16)(pz >> 7) & 0x3FFF;
    car.sprite_y = (u16)((u16)((s32)py >> 7) + car.eye_height);
    car.frac_x = (u8)((px & 0x7F) << 1);
    car.frac_z = (u8)((pz & 0x7F) << 1);
    car.view_heading = heading & 0xFFC0; car.body_heading = heading; car.course = heading;
    car.fine_heading = car.view_fine = heading;
    DSW(DS_obj_x) = car.sprite_x;
    DSW(DS_obj_z) = car.sprite_z;
    DSW(DS_obj_y8) = (u16)((u16)((s32)py >> 7) << 3);
    DSW(DS_obj_heading) = (u16)((((u16)((heading & 0xFFC0) - 0x4000) & 0xFF00) >> 8) * 0x101);
}

void foot_teleport(double x4, double z4, double feet)
{
    if (!active) return;
    DSL(DS_pos_x) = (u32)(s32)floor(x4 * 32.0 + 0.5);
    DSL(DS_pos_z) = (u32)(s32)floor(z4 * 32.0 + 0.5);
    DSL(DS_pos_y) = (u32)(s32)floor(feet * 128.0 + 0.5);
    for (int i = 0; i < 3; i++) {
        DSL(DS_pos_hist1 + 4 * i) = DSL(DS_pos_x + 4 * i);
        DSL(DS_pos_hist2 + 4 * i) = DSL(DS_pos_x + 4 * i);
        DSL(DS_pos_hist3 + 4 * i) = DSL(DS_pos_x + 4 * i);
    }
    vx = vz = vy = 0;
    grounded = true;
}

/* inside the map, F_EDGE from its edges; true when it had to be moved */
static bool keep_on_map(double *x, double *z)
{
    double x0 = *x, z0 = *z;
    if (*x < F_EDGE) *x = F_EDGE;
    if (*x > F_MAP_X - F_EDGE) *x = F_MAP_X - F_EDGE;
    if (*z < F_EDGE) *z = F_EDGE;
    if (*z > F_MAP_Z - F_EDGE) *z = F_MAP_Z - F_EDGE;
    return *x != x0 || *z != z0;
}

void foot_set_view(double yaw, double look)
{
    live_yaw = yaw - floor(yaw / 65536.0) * 65536.0;
    look_rows = look;
}

double foot_speed(bool *on_ground)
{
    if (on_ground) *on_ground = grounded;
    return sqrt(vx * vx + vz * vz);
}

void foot_physics(void)
{
    log_open();
    for (int i = 0; i < 3; i++) {                                    /* the history bounce_back would use */
        DSL(DS_pos_hist3 + 4 * i) = DSL(DS_pos_hist2 + 4 * i);
        DSL(DS_pos_hist2 + 4 * i) = DSL(DS_pos_hist1 + 4 * i);
        DSL(DS_pos_hist1 + 4 * i) = DSL(DS_pos_x + 4 * i);
    }
    double nom = host_frame_ticks(), ft = DSB(DS_frame_ticks);
    if (ft > 2.0 * nom) ft = 2.0 * nom;
    if (ft < 1) ft = 1;
    double dt = ft * PIT_DIV_GAME / (double)PIT_HZ;

    HostFootInput in;
    host_foot_input(&in);
    if (!view_live || !enh_enabled()) {                              /* no enhanced view: turned here */
        double mdx, mdy;
        host_foot_mouse(&mdx, &mdy);
        turn(&in, dt, 1.0, mdx, mdy);
    }

    double mx = in.move_x, my = in.move_y, m = sqrt(mx * mx + my * my);
    if (m > 1) { mx /= m; my /= m; }
    double sp = in.run ? F_RUN : F_WALK, a = live_yaw * (2.0 * M_PI / 65536.0);
    double wx = (sin(a) * my + cos(a) * mx) * sp, wz = (cos(a) * my - sin(a) * mx) * sp;
    double k = 1.0 - exp(-dt / (grounded ? F_ACCEL_TAU : F_AIR_TAU));
    vx += (wx - vx) * k;
    vz += (wz - vz) * k;
    if (in.jump && !jump_held && grounded) { vy = F_JUMP; grounded = false; }
    jump_held = in.jump;

    double px = (s32)DSL(DS_pos_x) / 32.0, pz = (s32)DSL(DS_pos_z) / 32.0, feet = (s32)DSL(DS_pos_y) / 128.0;
    double nx = px + vx * dt, nz = pz + vz * dt;
    bool hit = resolve(&nx, &nz, feet);
    if (keep_on_map(&nx, &nz)) {                                     /* OG: the map's edge holds like a wall */
        hit = true;
        if (flog) fprintf(flog, "edge: held at %.0f %.0f\n", nx, nz);
    }
    u8 type;
    double g = ground_at(nx, nz, feet + F_STEP, &type);
    if (type == 0x0E) {                                              /* water: not into it */
        nx = px; nz = pz; vx = vz = 0;
        g = ground_at(nx, nz, feet + F_STEP, &type);
    }
    if (g == NOGROUND) g = (s16)DSW(DS_ground_height) ? (s16)DSW(DS_ground_height) : feet;
    if (hit) {                                                       /* what the walls took off */
        double ax = (nx - px) / dt, az = (nz - pz) / dt;
        if (fabs(ax) < fabs(vx)) vx = ax;
        if (fabs(az) < fabs(vz)) vz = az;
    }
    if (grounded) {
        if (g >= feet - F_STEP) feet = g;                            /* up and down slopes and steps */
        else { grounded = false; vy = 0; }
    }
    if (!grounded) {
        vy -= F_GRAVITY * dt;
        feet += vy * dt;
        if (feet <= g) { feet = g; vy = 0; grounded = true; }
    }
    DSL(DS_pos_x) = (u32)(s32)floor(nx * 32.0 + 0.5);
    DSL(DS_pos_z) = (u32)(s32)floor(nz * 32.0 + 0.5);
    DSL(DS_pos_y) = (u32)(s32)floor(feet * 128.0 + 0.5);
    DSB(DS_on_ground) = grounded;
    publish();
    if (flog) fprintf(flog, "frame %u x4 %.1f z4 %.1f feet %.1f ground %.1f type %02X yaw %.0f look %.0f v %.0f %.0f %.0f%s%s\n",
                      DSW(DS_frame_counter), nx, nz, feet, g, type, live_yaw, look_rows, vx, vz, vy,
                      hit ? " hit" : "", foot_near_car() ? " near" : "");
}

/* developer aid: TD3_FOOT_LOG with TD3_FOOT_DUMP=n: the faces within 400 units every n-th frame */
static void dump_near(void)
{
    static int every = -1;
    if (every < 0) { const char *e = SDL_getenv("TD3_FOOT_DUMP"); every = e ? SDL_atoi(e) : 0; }
    if (!flog || every <= 0 || DSW(DS_frame_counter) % every) return;
    double px = x4_of(DSL(DS_pos_x)), pz = x4_of(DSL(DS_pos_z)), feet = (s32)DSL(DS_pos_y) / 128.0;
    fprintf(flog, "dump frame %u at %.1f %.1f feet %.1f eye %u cam_y %d faces %u verts %u obj0 %u+%u f %u+%u flags %04X\n",
            DSW(DS_frame_counter), px, pz, feet, DSW(DS_car_eye_height), DSS(DS_cam_y_949E), DSW(DS_face_count),
            DSW(DS_vert_count), DSW(DS_obj_vert_base), DSW(DS_obj_vert_count), DSW(DS_obj_face_base),
            DSW(DS_obj_face_count), DSW(DS_obj_flags));
    int nf = DSW(DS_face_count);
    for (int f = 0; f < nf && f < 0x640; f++) {
        Poly p;
        if (!face_get(f, px, pz, &p)) continue;
        double near = 1e9;
        for (int k = 0; k < p.n; k++) { double d = sqrt(p.x[k] * p.x[k] + p.z[k] * p.z[k]); if (d < near) near = d; }
        if (near > 400) continue;
        fprintf(flog, "  f%d n%d t%02X", f, p.n, p.type);
        for (int k = 0; k < p.n; k++) fprintf(flog, " (%.0f %.0f %.0f)", p.x[k], p.y[k] - feet, p.z[k]);
        fprintf(flog, "\n");
    }
}

void foot_after_draw(void)
{
    dump_near();
    /* the car's responses to walls, vehicles and water (crash, bounce, shake) are not the walker's */
    DSB(DS_crash_flag) = 0;
    DSB(DS_shake) = 0;
}
