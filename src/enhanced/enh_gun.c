/* OG: the pistol (WEAPON.md): the walker's gun in the enhanced view.
 *
 * Everything runs at the display rate and works on what the view shows: a shot is a line of sight from the view's
 * eye through the crosshair, met with the faces of the newest snapshot at their places in the displayed frame
 * (moving vehicles as they are drawn) and with the trees; what it hits takes the hit (game/weapon.c) and shows it
 * (sparks, dust, splashes; a vehicle stops after WEAPON_CAR_HITS and burns out). The gun in the hand is a small
 * flat-shaded model (boxes, as the game's own objects) drawn over the picture in true colour, with recoil, the
 * slide, a muzzle flash, walking bob, sway, reloading and aiming down the sights (a narrower view). */
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
#include "platform/vga.h"

#define RAD2PX (1024.0 / M_PI)
#define A2RAD  (2.0 * M_PI / 65536.0)

#define RANGE      9000.0      /* x4 units (about 90 m) */
#define ZOOM_ADS   1.05        /* the view's zoom aiming down the sights (the original's field of view and a bit) */

/* the gun's pose: where its origin is in the eye's frame (cm: x right, y up, z ahead) and its turn (radians) */
typedef struct { double x, y, z, yaw, pitch, roll; } Pose;

/* the weapons (WEAPON.md): the pistol, picked-up uzis and shotguns */
enum { W_PISTOL, W_UZI, W_SHOTGUN, W_COUNT };
typedef struct {
    const char *name;
    int mag, start_reserve, max_reserve;
    double gap;                 /* s from shot to shot at least */
    bool autofire;
    int pellets;
    double spread, kick;        /* spread from the hip (radians), added by each shot */
    double recoil_hip, recoil_ads, recoil_back, recoil_tau;
    int dmg_body, dmg_head;     /* a gunman's, per pellet (at most 100 hp) */
    double car;                 /* a vehicle's damage per pellet (WEAPON_CAR_HITS stops it) */
    int sound;
    double reload;              /* s (the shotgun: a shell) */
    Pose hip;
    double ads_y, ads_z;        /* the sights on the crosshair */
    double muzzle[3];
    double slide;               /* the moving part's travel (slide, bolt, pump), cm */
} WDef;
static const WDef wdef[W_COUNT] = {
    { "PISTOL", 12, 24, 96, 0.11, false, 1, 0.004, 0.010, 0.16, 0.07, 3.2, 0.075, 45, 150, 1.0, PCM_SHOT, 1.30,
      { 12.5, -14.5, 42, -0.20, 0.05, -0.12 }, -4.32, 50, { 0, 1.8, 17.2 }, 3.0 },
    { "UZI", 32, 0, 192, 0.085, true, 1, 0.012, 0.006, 0.06, 0.035, 1.6, 0.05, 26, 100, 0.55, PCM_UZI, 1.60,
      { 11.5, -13.5, 37, -0.18, 0.04, -0.10 }, -6.0, 46, { 0, 2.2, 22.8 }, 2.0 },
    { "SHOTGUN", 8, 0, 40, 0.85, false, 9, 0.055, 0.012, 0.30, 0.14, 6.0, 0.11, 24, 60, 0.36, PCM_SHOTGUN, 0.42,
      { 10.5, -12.5, 30, -0.08, 0.04, -0.06 }, -4.6, 27, { 0, 3.15, 62.6 }, 8.0 },
};

static int cur = W_PISTOL;
static bool have[W_COUNT] = { true, false, false };
static int mag[W_COUNT] = { 12, 0, 0 }, reserve[W_COUNT] = { 24, 0, 0 };
static bool loaded;                                             /* the loadout given once at the start */
static double reload_left = -1, since_shot = 9, since_draw = 9, since_hit = 9, aim, zoom_now = -1;
static bool fire_prev, reload_prev, was_foot, slide_locked;
static double bob_phase, bob_amp, sway_x, sway_y, prev_yaw, prev_look, spread;
static bool have_prev;
static u32 deaths_seen;
static char pick_msg[32];
static double pick_t = 9;
static u32 rng = 0x2545F491u;

static void glow(double cx, double cy, double r, u32 rgb, double w);
static void line(double ax, double ay, double bx, double by, double th, u32 rgb, double w, bool additive);

static double frand(void)                       /* 0 .. 1 */
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xFFFFFF) / 16777216.0;
}
static double srand1(void) { return frand() * 2 - 1; }

static inline double wraps(double a) { a = fmod(a, 65536.0); if (a < -32768) a += 65536; if (a >= 32768) a -= 65536; return a; }

/* the hip-fire zoom (TD3_FOOT_ZOOM to try others) */
static double hip_zoom(void)
{
    static double z = -1;
    if (z < 0) {
        const char *e = SDL_getenv("TD3_FOOT_ZOOM");
        z = e ? SDL_atof(e) : ENH_FOOT_ZOOM;
        if (z < 0.3 || z > 1.0) z = ENH_FOOT_ZOOM;
    }
    return z;
}

double enh_foot_zoom(void) { return zoom_now > 0 ? zoom_now : hip_zoom(); }

/* ------------------------------------------------------------------------------------------------------
 * Particles: sparks, dust, splashes, smoke and flames, in the world
 * ------------------------------------------------------------------------------------------------------ */

typedef struct {
    double x, y, z, vx, vy, vz;                 /* x4 / height units, per second */
    float age, life, s0, s1, a0;                /* sizes (units) at birth and death, opacity at birth */
    u32 rgb;
    bool add, grav;                             /* added light (sparks, flames, flashes); falls */
} Part;
#define NPART 1024
static Part parts[NPART];
static int npart;

static void part(double x, double y, double z, double vx, double vy, double vz, double life, double s0, double s1,
                 double a0, u32 rgb, bool add, bool grav)
{
    if (npart >= NPART) return;
    Part *p = &parts[npart++];
    p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
    p->age = 0; p->life = (float)life; p->s0 = (float)s0; p->s1 = (float)s1; p->a0 = (float)a0;
    p->rgb = rgb; p->add = add; p->grav = grav;
}

/* (bx, by, bz): back towards the shooter (unit) */
static void fx_sparks(double x, double y, double z, double bx, double by, double bz, int n)
{
    part(x, y, z, 0, 0, 0, 0.07, 10, 18, 1.0, 0xFFF0C8, true, false);
    for (int i = 0; i < n; i++) {
        double s = 250 + 450 * frand();
        part(x, y, z, (bx + srand1() * 0.9) * s, (by + 0.4 + srand1() * 0.6) * s, (bz + srand1() * 0.9) * s,
             0.12 + 0.2 * frand(), 2.2, 1.0, 1.0, 0xFFC860, true, true);
    }
}

static void fx_dust(double x, double y, double z, double bx, double bz, u32 rgb, int n)
{
    for (int i = 0; i < n; i++)
        part(x + srand1() * 6, y + 3, z + srand1() * 6, bx * 40 + srand1() * 30, 50 + 40 * frand(), bz * 40 + srand1() * 30,
             0.45 + 0.35 * frand(), 7, 34, 0.65, rgb, false, false);
    for (int i = 0; i < n; i++)
        part(x, y + 2, z, bx * 120 + srand1() * 120, 150 + 150 * frand(), bz * 120 + srand1() * 120,
             0.3 + 0.25 * frand(), 2.5, 2.0, 1.0, (rgb >> 1) & 0x7F7F7F, false, true);
}

static void fx_splash(double x, double y, double z)
{
    for (int i = 0; i < 8; i++)
        part(x + srand1() * 8, y + 2, z + srand1() * 8, srand1() * 60, 260 + 200 * frand(), srand1() * 60,
             0.5 + 0.3 * frand(), 4, 10, 0.85, 0xE6F0FF, false, true);
    part(x, y + 2, z, 0, 15, 0, 0.5, 10, 40, 0.5, 0xD0E0F0, false, false);
}

static void fx_wreck(double x, double y, double z)
{
    part(x, y + 30, z, 0, 0, 0, 0.25, 60, 180, 1.0, 0xFFD090, true, false);
    for (int i = 0; i < 30; i++) {
        double s = 300 + 600 * frand();
        part(x, y + 30, z, srand1() * s, frand() * s, srand1() * s, 0.3 + 0.4 * frand(), 3, 1.5, 1.0, 0xFFB050, true, true);
    }
    for (int i = 0; i < 8; i++)
        part(x + srand1() * 80, y + 30, z + srand1() * 80, srand1() * 60, 80 + 60 * frand(), srand1() * 60,
             1.8 + frand(), 40, 160, 0.75, 0x282828, false, false);
}

static double smoke_acc[ENH_MAX_OBJS], fire_acc[ENH_MAX_OBJS];

static void parts_update(const EnhSnap *c, const EnhView *v, double dt)
{
    int k = 0;
    for (int i = 0; i < npart; i++) {
        Part *p = &parts[i];
        p->age += (float)dt;
        if (p->age >= p->life) continue;
        if (p->grav) p->vy -= 900 * dt;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if (!p->grav && !p->add) { p->vx *= exp(-dt * 0.8); p->vz *= exp(-dt * 0.8); }
        parts[k++] = *p;
    }
    npart = k;
    /* the wrecks smoke and burn (the nearer ones, while they are drawn) */
    for (int o = 1; o < c->nobj; o++) {
        if (!weapon_wrecked(o)) { smoke_acc[o] = fire_acc[o] = 0; continue; }
        const EnhObj *e = &c->obj[o];
        double x = (u16)(e->x << 2), z = (u16)(e->z << 2), y = (s16)e->y8 / 8.0;
        double dx = wraps(x - v->cam_x4), dz = wraps(z - v->cam_z4);
        if (dx * dx + dz * dz > 7000.0 * 7000.0) continue;
        x = v->cam_x4 + dx; z = v->cam_z4 + dz;
        for (smoke_acc[o] += dt * 4.5; smoke_acc[o] >= 1; smoke_acc[o] -= 1)
            part(x + srand1() * 70, y + 35, z + srand1() * 70, 14 + srand1() * 8, 55 + 20 * frand(), 6 + srand1() * 8,
                 3.0 + frand(), 30, 170, 0.5, 0x3C3C3E, false, false);
        for (fire_acc[o] += dt * 9; fire_acc[o] >= 1; fire_acc[o] -= 1)
            part(x + srand1() * 80, y + 22, z + srand1() * 80, srand1() * 10, 70 + 40 * frand(), srand1() * 10,
                 0.3 + 0.2 * frand(), 16, 4, 0.9, frand() < 0.5 ? 0xFF8020 : 0xFFB040, true, false);
    }
}

/* ------------------------------------------------------------------------------------------------------
 * Shooting: the line of sight met with what the view shows
 * ------------------------------------------------------------------------------------------------------ */

typedef struct { double t; int face, obj; u8 type; bool tree; } Hit;

static bool tri_hit(const double *a, const double *b, const double *c, const double *d, double *t)
{
    double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    double p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
    double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabs(det) < 1e-9) return false;
    double inv = 1 / det, s[3] = { -a[0], -a[1], -a[2] };
    double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0 || u > 1) return false;
    double q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    double w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0 || u + w > 1) return false;
    *t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return *t > 1;
}

/* the line from (ox, oy, oz) along d met with the faces and the trees (the faces [skip0, skip1) left out) */
static Hit cast_from(const EnhSnap *c, const EnhView *v, double ox, double oy, double oz, const double *d, double tmax,
                     int skip0, int skip1)
{
    Hit h = { tmax, -1, -1, 0, false };
    for (int f = 0; f < c->nf; f++) {
        if (f >= skip0 && f < skip1) continue;
        const u16 *r = c->face[f];
        int n = (r[0] >> 14) + 1;
        u8 type = (u8)(r[3] >> 11);
        if (n < 3 || type == 0) continue;
        double p[4][3];
        bool ok = true;
        for (int k = 0; k < n; k++) {
            int i = r[k] & 0x7FF;
            if (i >= c->nv) { ok = false; break; }
            p[k][0] = wraps(v->vx[i] - ox);
            p[k][1] = v->vy[i] - oy;
            p[k][2] = wraps(v->vz[i] - oz);
        }
        if (!ok) continue;
        for (int t = 0; t + 2 < n; t++) {
            double tt;
            if (tri_hit(p[0], p[1 + t], p[2 + t], d, &tt) && tt < h.t) { h.t = tt; h.face = f; h.type = type; }
        }
    }
    /* trees, rocks and the barrier: upright cylinders */
    for (int k = 0; k < c->nspr_game; k++) {
        const EnhSpr *e = &c->spr[k];
        if ((u8)e->id == 0 || ((e->id >> 8) & 0xE0) != 0x60) continue;
        double cx = wraps(4.0 * e->x - ox), cz = wraps(4.0 * e->z - oz), rr = 26;
        double hgt = (double)((c->sprite_kind[e->id & 0x3F] & 0xF8) << 3);
        if (hgt < 40) hgt = 120;
        double a = d[0] * d[0] + d[2] * d[2], b = -2 * (cx * d[0] + cz * d[2]), cc = cx * cx + cz * cz - rr * rr;
        double disc = b * b - 4 * a * cc;
        if (a < 1e-9 || disc < 0) continue;
        double t = (-b - sqrt(disc)) / (2 * a);
        if (t < 1 || t >= h.t) continue;
        double y = oy + d[1] * t;
        if (y < e->y - 5 || y > e->y + hgt) continue;
        h.t = t; h.face = -1; h.tree = true; h.type = 0;
    }
    if (h.face >= 0)
        for (int o = 0; o < c->nobj; o++) {
            const EnhObj *e = &c->obj[o];
            if (!(e->flags & 0x2000) || !e->fcount || e->fbase + e->fcount > c->nf) continue;
            if (h.face >= e->fbase && h.face < e->fbase + e->fcount) { h.obj = o; break; }
        }
    return h;
}

static Hit cast(const EnhSnap *c, const EnhView *v, const double *d)
{
    return cast_from(c, v, v->cam_x4, v->cam_y, v->cam_z4, d, RANGE, c->car_f0, c->car_f0 + c->car_nf);
}

double enh_ray_world(const EnhSnap *c, const EnhView *v, double ox, double oy, double oz, const double *d, double tmax)
{
    return cast_from(c, v, ox, oy, oz, d, tmax, c->car_f0, c->car_f0 + c->car_nf).t;
}

/* one pellet (a pistol or uzi round is one): what it hits takes it; the effects and sounds only once a shot */
typedef struct { bool flesh, metal, dirt, water, wreck; } ShotFx;

static void pellet(const EnhSnap *c, const EnhView *v, const WDef *w, double e, double yaw, ShotFx *fx, bool first)
{
    double d[3] = { sin(yaw) * cos(e), sin(e), cos(yaw) * cos(e) };
    Hit h = cast(c, v, d);
    if (first && SDL_getenv("TD3_GUN_DEBUG"))
        fprintf(stderr, "shot: t %.0f face %d obj %d type %02X tree %d flags %04X\n", h.t, h.face, h.obj, h.type, h.tree,
                h.obj >= 0 ? c->obj[h.obj].flags : 0);
    {
        double ft;
        bool head = false;
        int fi = enh_foes_ray(v->cam_x4, v->cam_y, v->cam_z4, d, h.t, &ft, &head);
        if (fi >= 0) {                                         /* a gunman before anything else */
            double fall = w->pellets > 1 ? fmax(0.25, 1 - ft / 3500.0) : 1;   /* shot spreads and slows */
            enh_foes_damage(fi, (int)((head ? w->dmg_head : w->dmg_body) * fall + 0.5));
            enh_part_blood(v->cam_x4 + d[0] * (ft - 2), v->cam_y + d[1] * (ft - 2), v->cam_z4 + d[2] * (ft - 2), -d[0], -d[2]);
            fx->flesh = true;
            since_hit = 0;
            return;
        }
    }
    if (h.t >= RANGE) return;
    double t = h.t - 2;
    double x = v->cam_x4 + d[0] * t, y = v->cam_y + d[1] * t, z = v->cam_z4 + d[2] * t;
    double b[3] = { -d[0], -d[1], -d[2] };
    bool few = w->pellets > 1;                                  /* the shotgun's pellets: smaller effects */
    if (h.tree) { fx_dust(x, y, z, b[0], b[2], 0x7A5A32, few ? 1 : 4); fx->dirt = true; return; }
    if (h.obj >= 0) {
        int r = weapon_hit(h.obj, w->car);
        if (r && weapon_is_police(h.obj)) enh_police_crime(r == 2 ? 3 : 2);   /* POLICE.md */
        else if (r == 2) enh_police_crime(1);
        fx_sparks(x, y, z, b[0], b[1], b[2], few ? 2 : 7);
        fx->metal = true;
        if (r) since_hit = 0;
        if (r == 2) {
            const EnhObj *o = &c->obj[h.obj];
            double ox = v->cam_x4 + wraps((u16)(o->x << 2) - v->cam_x4), oz = v->cam_z4 + wraps((u16)(o->z << 2) - v->cam_z4);
            fx_wreck(ox, (s16)o->y8 / 8.0, oz);
            fx->wreck = true;
        }
        return;
    }
    if (h.type == 0x0E) { if (!few || first) fx_splash(x, y, z); fx->water = true; return; }
    if (h.type >= 0x10 && h.type <= 0x14) {                    /* the road: grit and a spark */
        fx_dust(x, y, z, b[0], b[2], 0x8C8C88, few ? 1 : 3);
        fx_sparks(x, y, z, b[0], b[1], b[2], few ? 1 : 2);
        fx->dirt = true;
        return;
    }
    if (h.type == 1 || h.type == 2) {                          /* walls, rails */
        fx_sparks(x, y, z, b[0], b[1], b[2], few ? 1 : 4);
        fx_dust(x, y, z, b[0], b[2], 0x9A968C, few ? 1 : 2);
        fx->metal = true;
        return;
    }
    fx_dust(x, y, z, b[0], b[2], 0x6E5A3C, few ? 1 : 4);        /* the ground, everything else */
    fx->dirt = true;
}

static void shoot(const EnhSnap *c, const EnhView *v)
{
    const WDef *w = &wdef[cur];
    mag[cur]--;
    since_shot = 0;
    if (mag[cur] == 0 && cur == W_PISTOL) slide_locked = true;
    pcm_sfx_play(w->sound, 1.0, 0.0);
    double zoom = enh_foot_zoom();
    /* through the crosshair (row 100), spread when firing from the hip and moving */
    double e0 = (v->cam_row - 100.0 / zoom) / RAD2PX, yaw0 = v->heading * A2RAD;
    double sp = w->pellets > 1 ? w->spread * (1 - 0.3 * aim) : spread * (1 - 0.85 * aim);
    ShotFx fx = { false, false, false, false, false };
    for (int i = 0; i < w->pellets; i++) {
        double a = frand() * 2 * M_PI, r = sqrt(frand()) * sp;  /* evenly over a disc */
        pellet(c, v, w, e0 + sin(a) * r, yaw0 + cos(a) * r, &fx, i == 0);
    }
    spread += w->kick;
    enh_foes_heard(v->cam_x4, v->cam_z4);                       /* OG (FOES.md): the gunmen hear it */
    if (fx.flesh) pcm_sfx_play(PCM_FLESH, 0.9, 0.0);
    if (fx.metal) pcm_sfx_play(PCM_HIT_METAL, 0.8, 0.0);
    if (fx.dirt) pcm_sfx_play(PCM_HIT_DIRT, 0.7, 0.0);
    if (fx.water) pcm_sfx_play(PCM_HIT_WATER, 0.7, 0.0);
    if (fx.wreck) pcm_sfx_play(PCM_WRECK, 1.0, 0.0);
}

/* ------------------------------------------------------------------------------------------------------
 * Once per displayed frame, from the walker's view (enhanced.c foot_view)
 * ------------------------------------------------------------------------------------------------------ */

double enh_gun_aim(const HostFootInput *in, bool foot, double dt)
{
    if (!foot) { aim = 0; zoom_now = hip_zoom(); return 1.0; }
    double target = in->aim && reload_left < 0 && since_draw > 0.3 ? 1.0 : 0.0;
    aim += (target - aim) * (1 - exp(-dt / 0.07));
    if (fabs(aim - target) < 0.002) aim = target;
    zoom_now = hip_zoom() + (ZOOM_ADS - hip_zoom()) * aim;
    return 1.0 - 0.55 * aim;                                   /* turning slows while aiming */
}

/* the start loadout: the pistol, 12 + 24 rounds (a new game, and after every death) */
static void loadout(void)
{
    cur = W_PISTOL;
    for (int i = 0; i < W_COUNT; i++) { have[i] = i == W_PISTOL; mag[i] = 0; reserve[i] = 0; }
    mag[W_PISTOL] = wdef[W_PISTOL].mag;
    reserve[W_PISTOL] = wdef[W_PISTOL].start_reserve;
    reload_left = -1;
    slide_locked = false;
    const char *g = SDL_getenv("TD3_GIVE");                  /* developer aid: every weapon, full */
    if (g)
        for (int i = 0; i < W_COUNT; i++) { have[i] = true; mag[i] = wdef[i].mag; reserve[i] = wdef[i].max_reserve; }
    if (g && SDL_atoi(g) >= 1 && SDL_atoi(g) <= W_COUNT) cur = SDL_atoi(g) - 1;
}

static void take_out(int w)
{
    if (w == cur || !have[w]) return;
    cur = w;
    since_draw = 0;                                             /* the new one comes up */
    reload_left = -1;
    slide_locked = cur == W_PISTOL && mag[cur] == 0;
}

/* the next weapon with rounds (dir +1 / -1); with none, the next one at all */
static void cycle_weapon(int dir)
{
    for (int pass = 0; pass < 2; pass++)
        for (int k = 1; k < W_COUNT; k++) {
            int w = ((cur + dir * k) % W_COUNT + W_COUNT) % W_COUNT;
            if (have[w] && (pass || mag[w] + reserve[w] > 0)) { take_out(w); return; }
        }
}

bool enh_gun_pickup(int w, int rounds)
{
    if (w < 0 || w >= W_COUNT) return false;
    const WDef *d = &wdef[w];
    bool fresh = !have[w];
    if (!fresh && reserve[w] >= d->max_reserve) return false;   /* full: it stays there */
    if (fresh) {
        have[w] = true;
        int m = rounds < d->mag ? rounds : d->mag;
        mag[w] = m;
        rounds -= m;
    }
    reserve[w] += rounds;
    if (reserve[w] > d->max_reserve) reserve[w] = d->max_reserve;
    snprintf(pick_msg, sizeof pick_msg, fresh ? "+ %s" : "%s AMMO", d->name);
    pick_t = 0;
    pcm_sfx_play(PCM_PICKUP, 0.9, 0.0);
    if (fresh && (w > cur || mag[cur] + reserve[cur] == 0)) take_out(w);   /* a better one in the hand */
    return true;
}

void enh_gun_frame(const EnhSnap *c, const EnhView *v, const HostFootInput *in, double dt)
{
    if (dt < 0 || dt > 0.25) dt = 0.25;
    u32 dn = weapon_deaths();
    if (dn != deaths_seen || !loaded) { deaths_seen = dn; loaded = true; loadout(); }   /* shot dead: the pickups are lost */
    pick_t += dt;
    if (!c->foot) { was_foot = false; return; }
    const WDef *w = &wdef[cur];
    if (!was_foot) {                                           /* just out of the car: the gun comes up */
        was_foot = true;
        since_draw = 0;
        reload_left = -1;
        fire_prev = in->fire;
        reload_prev = in->reload;
        have_prev = false;
    }
    int sel, cyc;
    host_foot_weapon(&sel, &cyc);
    if (sel >= 1 && sel <= W_COUNT) take_out(sel - 1);
    if (cyc) cycle_weapon(cyc > 0 ? 1 : -1);
    w = &wdef[cur];

    since_shot += dt; since_draw += dt; since_hit += dt;
    double rest = w->pellets > 1 ? 0.004 : w->spread;
    spread += (rest - spread) * (1 - exp(-dt / 0.25));
    bool on_ground;
    double speed = foot_speed(&on_ground);
    spread = fmax(spread, rest + speed / 700.0 * 0.020);

    if (reload_left >= 0) {
        reload_left -= dt;
        if (reload_left < 0) {
            if (cur == W_SHOTGUN) {                            /* a shell in; on while there is room and shells */
                if (reserve[cur] > 0 && mag[cur] < w->mag) {
                    mag[cur]++; reserve[cur]--;
                    pcm_sfx_play(PCM_SHELL, 0.8, 0.2);
                }
                if (reserve[cur] > 0 && mag[cur] < w->mag && !in->fire) reload_left = w->reload;
            } else {
                int take = w->mag - mag[cur];
                if (take > reserve[cur]) take = reserve[cur];
                mag[cur] += take; reserve[cur] -= take;
                slide_locked = false;
            }
        }
    }
    bool can = since_draw > 0.3;
    if (cur == W_SHOTGUN && reload_left >= 0 && in->fire && !fire_prev && mag[cur] > 0) reload_left = -1;  /* fire stops it */
    if (reload_left >= 0) can = false;
    if (in->reload && !reload_prev && can && mag[cur] < w->mag && reserve[cur] > 0) {
        reload_left = cur == W_SHOTGUN ? 0.30 : w->reload;     /* the shotgun: into position, then shell by shell */
        if (cur != W_SHOTGUN) pcm_sfx_play(PCM_RELOAD, 0.8, 0.2);
    }
    bool trigger = in->fire && (w->autofire || !fire_prev);
    if (trigger && can && since_shot >= w->gap) {
        if (mag[cur] > 0) shoot(c, v);
        else if (!fire_prev) {
            pcm_sfx_play(PCM_EMPTY, 0.8, 0.2);
            if (reserve[cur] > 0) {                            /* empty: reload */
                reload_left = cur == W_SHOTGUN ? 0.30 : w->reload;
                if (cur != W_SHOTGUN) pcm_sfx_play(PCM_RELOAD, 0.8, 0.2);
            } else cycle_weapon(1);                            /* nothing left: the next one with rounds */
        }
    }
    if (cur == W_SHOTGUN && since_shot - dt < 0.22 && since_shot >= 0.22) pcm_sfx_play(PCM_PUMP, 0.9, 0.15);
    fire_prev = in->fire;
    reload_prev = in->reload;

    /* the hand's motion: walking bob, sway against turning */
    double a = on_ground ? fmin(speed / 320.0, 2.0) : 0;
    bob_amp += (a - bob_amp) * (1 - exp(-dt / 0.15));
    bob_phase += dt * (5.5 + 3.0 * fmin(speed / 700.0, 1.0)) * (speed > 20 ? 1 : 0);
    double dyaw = have_prev ? wraps(v->heading - prev_yaw) : 0, dlook = have_prev ? v->cam_row - prev_look : 0;
    prev_yaw = v->heading; prev_look = v->cam_row; have_prev = true;
    double tx = -fmax(-1.5, fmin(1.5, dyaw / dt / 20000.0)), ty = fmax(-1.0, fmin(1.0, dlook / dt / 300.0));
    sway_x += (tx - sway_x) * (1 - exp(-dt / 0.12));
    sway_y += (ty - sway_y) * (1 - exp(-dt / 0.12));
}

void enh_parts_frame(const EnhSnap *c, const EnhView *v, double dt)
{
    if (dt < 0 || dt > 0.25) dt = 0.25;
    if (c->menu_preview) { npart = 0; return; }
    parts_update(c, v, dt);
}

void enh_gun_reset(void)
{
    loadout();
    npart = 0;
}

/* ------------------------------------------------------------------------------------------------------
 * Drawing: the particles, the gun, the crosshair and the rounds left
 * ------------------------------------------------------------------------------------------------------ */

static inline u32 mix(u32 a, u32 b, double w)       /* w of b */
{
    if (w <= 0) return a;
    if (w >= 1) return b;
    int iw = (int)(w * 256);
    u32 r = (((a >> 16) & 0xFF) * (256 - iw) + ((b >> 16) & 0xFF) * iw) >> 8;
    u32 g = (((a >> 8) & 0xFF) * (256 - iw) + ((b >> 8) & 0xFF) * iw) >> 8;
    u32 bl = ((a & 0xFF) * (256 - iw) + (b & 0xFF) * iw) >> 8;
    return r << 16 | g << 8 | bl;
}
static inline u32 add(u32 a, u32 b, double w)
{
    int r = (int)((a >> 16) & 0xFF) + (int)(((b >> 16) & 0xFF) * w);
    int g = (int)((a >> 8) & 0xFF) + (int)(((b >> 8) & 0xFF) * w);
    int bl = (int)(a & 0xFF) + (int)((b & 0xFF) * w);
    return (u32)(r > 255 ? 255 : r) << 16 | (u32)(g > 255 ? 255 : g) << 8 | (u32)(bl > 255 ? 255 : bl);
}

static int FW, FH, S, A;                                /* the frame (output pixels), scale, samples per output pixel */
/* OG (FOES.md): where the frame shows the 3D view (VGA pixels of the frame, 1 = it does); NULL = everywhere. The
 * world's things (particles, gunmen) are not drawn over a message box, the dashboard or the mirror's rim. */
static const u8 *MASK;
static int MASK_W;
void enh_draw_mask(const u8 *m, int w) { MASK = m; MASK_W = w; }
static inline bool masked(int x, int y) { return MASK && !MASK[(size_t)(y / S) * MASK_W + x / S]; }
static u32 *OUT;

/* the particles over the frame: the front target's column 0 / row 0 at frame VGA pixel (offx, offy), drawn in the
 * frame rows [y0, y1) only, not in the mirror's hole (hole: target rows 0..13 at screen x 168..255) */
static void draw_parts(int offx, int offy, int y0c, int y1c, bool hole, int em)
{
    const EnhTarget *t = &enh_front;
    double zoom = enh_scene_zoom();
    for (int i = 0; i < npart; i++) {
        const Part *p = &parts[i];
        double tx, ty, dist;
        if (!enh_front_point(p->x, p->y, p->z, &tx, &ty, &dist) || dist < 5) continue;
        double f = p->age / p->life, size = p->s0 + (p->s1 - p->s0) * f;
        double alpha = p->a0 * (1 - f) * (p->add ? 1 : fmin(1.0, f * 8 + 0.3));
        double r = atan2(size, dist) * RAD2PX * zoom * S, cx = (tx + offx) * S, cy = (ty + offy) * S;
        if (r < 0.7) { alpha *= r / 0.7; r = 0.7; }
        int x0 = (int)floor(cx - r), x1 = (int)ceil(cx + r), y0 = (int)floor(cy - r), y1 = (int)ceil(cy + r);
        if (x1 < 0 || y1 < 0 || x0 >= FW || y0 >= FH) continue;
        if (x0 < 0) x0 = 0;
        if (y0 < y0c * S) y0 = y0c * S;
        if (x1 > FW) x1 = FW;
        if (y1 > y1c * S) y1 = y1c * S;
        double zlim = dist - size * 0.6 - 8;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                double dx = (x + 0.5 - cx) / r, dy = (y + 0.5 - cy) / r, q = dx * dx + dy * dy;
                if (q >= 1) continue;
                if (hole && y < (offy + 14) * S && x >= (em + 168) * S && x < (em + 256) * S) continue;
                int sx = (x - offx * S) * A + A / 2, sy = (y - offy * S) * A + A / 2;
                if (sx < 0 || sy < 0) continue;
                if (sx < t->w && sy < t->h && t->z[(size_t)sy * t->w + sx] < zlim) continue;   /* behind something */
                if (masked(x, y)) continue;
                double w = alpha * (1 - q) * (1 - q);
                u32 *o = OUT + (size_t)y * FW + x;
                *o = p->add ? add(*o, p->rgb, w) : mix(*o, p->rgb, w);
            }
    }
}

void enh_glow(u32 *xrgb, int em, double cx, double cy, double r, u32 rgb, double w)
{
    S = enh_scale; A = enh_aa; FW = (320 + 2 * em) * S; FH = 200 * S; OUT = xrgb;
    glow(cx, cy, r, rgb, w);
}

void enh_line(u32 *xrgb, int em, double ax, double ay, double bx, double by, double th, u32 rgb, double w)
{
    S = enh_scale; A = enh_aa; FW = (320 + 2 * em) * S; FH = 200 * S; OUT = xrgb;
    line(ax, ay, bx, by, th, rgb, w, false);
}

void enh_part_blood(double x, double y, double z, double bx, double bz)
{
    for (int i = 0; i < 6; i++)
        part(x + srand1() * 4, y, z + srand1() * 4, bx * 60 + srand1() * 50, 40 + 60 * frand(), bz * 60 + srand1() * 50,
             0.35 + 0.2 * frand(), 4, 14, 0.8, 0x6A1010, false, true);
}

void enh_parts_draw(u32 *xrgb, int em, int offx, int offy, int y0, int y1, bool hole)
{
    if (!npart) return;
    S = enh_scale; A = enh_aa; FW = (320 + 2 * em) * S; FH = 200 * S; OUT = xrgb;
    draw_parts(offx, offy, y0, y1, hole, em);
}

/* ---- the gun: hexahedra (8 corners: bit 0 x, bit 1 y, bit 2 z) in the gun's frame, centimetres: x right, y up,
 * z along the barrel; the origin under the back of the slide. */
enum { P_GUN, P_SLIDE, P_HAND, P_SLEEVE };                   /* P_SLIDE: the moving part (slide, bolt, pump) */
typedef struct { double v[8][3]; u32 rgb; int part; } Hex;
#define NHEX 40
static Hex hexw[W_COUNT][NHEX];
static int nhexw[W_COUNT];
static Hex *hex;                                                /* the model being built */
static int nhex;

static void hx_box(double x0, double x1, double y0, double y1, double z0, double z1, u32 rgb, int part)
{
    if (nhex >= NHEX) return;
    Hex *h = &hex[nhex++];
    for (int i = 0; i < 8; i++) {
        h->v[i][0] = i & 1 ? x1 : x0;
        h->v[i][1] = i & 2 ? y1 : y0;
        h->v[i][2] = i & 4 ? z1 : z0;
    }
    h->rgb = rgb;
    h->part = part;
}

/* a slanted block: x0..x1 wide, its top at yt from zt0 to zt1, its bottom at yb from zb0 to zb1 */
static void hx_slant(double x0, double x1, double yt, double zt0, double zt1, double yb, double zb0, double zb1,
                     u32 rgb, int part)
{
    if (nhex >= NHEX) return;
    Hex *h = &hex[nhex++];
    for (int i = 0; i < 8; i++) {
        bool top = i & 2, back = !(i & 4);
        h->v[i][0] = i & 1 ? x1 : x0;
        h->v[i][1] = top ? yt : yb;
        h->v[i][2] = top ? (back ? zt0 : zt1) : (back ? zb0 : zb1);
    }
    h->rgb = rgb;
    h->part = part;
}

/* a block from a front rectangle (z = zf: x0..x1, y0..y1) to a back one (z = zb) */
static void hx_taper(double zf, double fx0, double fx1, double fy0, double fy1,
                     double zb, double bx0, double bx1, double by0, double by1, u32 rgb, int part)
{
    if (nhex >= NHEX) return;
    Hex *h = &hex[nhex++];
    for (int i = 0; i < 8; i++) {
        bool front = i & 4;
        h->v[i][0] = i & 1 ? (front ? fx1 : bx1) : (front ? fx0 : bx0);
        h->v[i][1] = i & 2 ? (front ? fy1 : by1) : (front ? fy0 : by0);
        h->v[i][2] = front ? zf : zb;
    }
    h->rgb = rgb;
    h->part = part;
}

#define C_METAL   0x3C3E42
#define C_POLY    0x252527
#define C_DARK    0x141414
#define C_SKIN    0xBE8E70
#define C_SLEEVE  0x4E382A
#define C_CUFF    0x3A2A20
#define C_WOOD    0x6E4626
#define C_DOT     0xE8E4D0

/* the right hand round a grip shaped as the pistol's, moved by (dy, dz): the fingers in front, the thumb along the
 * left side, the palm behind, the wrist, the cuff and the sleeve */
static void right_hand(double dy, double dz)
{
    for (int i = 0; i < 3; i++) {
        double top = -4.0 - i * 2.55, bot = top - 2.3, ym = (top + bot) / 2, zf = 1.4 + (ym + 1.3) * (3.0 / 11.0);
        hx_box(-2.0, 1.9, bot + dy, top + dy, zf - 0.5 + dz, zf + 1.7 + dz, C_SKIN, P_HAND);
    }
    hx_box(-0.9, 1.9, -2.9 + dy, -2.0 + dy, 1.4 + dz, 3.9 + dz, C_SKIN, P_HAND);               /* the trigger finger */
    hx_box(-2.3, -1.4, -1.5 + dy, -0.2 + dy, -3.2 + dz, 3.4 + dz, C_SKIN, P_HAND);             /* the thumb */
    hx_slant(-2.4, -1.3, -1.6 + dy, -4.6 + dz, -1.2 + dz, -7.6 + dy, -5.6 + dz, -2.8 + dz, C_SKIN, P_HAND);
    hx_slant(-1.6, 2.7, -1.2 + dy, -5.4 + dz, -3.2 + dz, -9.8 + dy, -7.9 + dz, -5.5 + dz, C_SKIN, P_HAND);  /* the palm */
    hx_taper(-6.0 + dz, -1.8, 2.6, -10.2 + dy, -3.6 + dy, -13.0 + dz, -1.6, 2.8, -13.0 + dy, -7.0 + dy, C_SKIN, P_HAND);
    hx_taper(-12.4 + dz, -2.7, 3.7, -13.8 + dy, -5.6 + dy, -14.6 + dz, -2.9, 3.9, -15.0 + dy, -6.4 + dy, C_CUFF, P_SLEEVE);
    hx_taper(-14.4 + dz, -2.8, 3.8, -14.8 + dy, -6.2 + dy, -30.0 + dz, -4.0, 6.0, -32.0 + dy, -12.0 + dy, C_SLEEVE, P_SLEEVE);
}

static void gun_model(void)
{
    if (nhexw[W_PISTOL]) return;
    hex = hexw[W_PISTOL]; nhex = 0;                             /* the pistol */
    hx_box(-1.35, 1.35, 0.2, 3.5, -2.6, 16.4, C_METAL, P_SLIDE);             /* the slide */
    hx_box(-0.6, 0.6, 1.1, 2.5, 16.4, 16.6, C_DARK, P_SLIDE);                /* the muzzle */
    hx_box(-1.1, -0.35, 3.5, 4.3, -2.3, -1.2, C_DARK, P_SLIDE);              /* the rear sight */
    hx_box(0.35, 1.1, 3.5, 4.3, -2.3, -1.2, C_DARK, P_SLIDE);
    hx_box(-0.28, 0.28, 3.5, 4.35, 14.9, 15.7, C_DARK, P_SLIDE);             /* the front sight */
    hx_box(-0.16, 0.16, 3.95, 4.22, 14.87, 14.9, C_DOT, P_SLIDE);            /* and its dot */
    hx_box(-1.25, 1.25, -1.3, 0.2, 0.5, 15.4, C_POLY, P_GUN);                /* the frame */
    hx_box(-0.45, 0.45, -4.2, -1.3, 6.0, 6.9, C_POLY, P_GUN);                /* the trigger guard */
    hx_box(-0.45, 0.45, -4.4, -3.6, 0.0, 6.9, C_POLY, P_GUN);
    hx_box(-0.25, 0.25, -3.1, -1.3, 2.6, 3.3, C_DARK, P_GUN);                /* the trigger */
    hx_slant(-1.45, 1.45, -1.3, -3.4, 1.4, -12.3, -6.4, -1.6, C_POLY, P_GUN);   /* the grip */
    hx_slant(-1.6, 1.6, -12.3, -6.5, -1.5, -12.9, -6.7, -1.4, C_DARK, P_GUN);   /* the magazine's base */
    right_hand(0, 0);
    nhexw[W_PISTOL] = nhex;

    hex = hexw[W_UZI]; nhex = 0;                                /* the uzi: a box with the magazine in the grip */
    hx_box(-1.6, 1.6, 0.0, 4.2, -6.0, 17.0, C_METAL, P_GUN);                 /* the receiver */
    hx_box(-1.7, 1.7, 3.6, 4.2, -6.0, 17.0, C_DARK, P_GUN);                  /* its top cover */
    hx_box(-0.65, 0.65, 1.5, 2.9, 17.0, 22.6, C_DARK, P_GUN);                /* the barrel */
    hx_box(-0.55, 0.55, 4.2, 5.0, 3.0, 6.5, C_DARK, P_SLIDE);                /* the cocking knob */
    hx_box(-1.1, -0.35, 4.2, 6.0, -5.6, -4.6, C_DARK, P_GUN);                /* the rear sight */
    hx_box(0.35, 1.1, 4.2, 6.0, -5.6, -4.6, C_DARK, P_GUN);
    hx_box(-0.25, 0.25, 4.2, 6.0, 15.5, 16.3, C_DARK, P_GUN);                /* the front post */
    hx_box(-1.2, -0.85, 4.2, 6.2, 15.3, 16.5, C_DARK, P_GUN);                /* and its ears */
    hx_box(0.85, 1.2, 4.2, 6.2, 15.3, 16.5, C_DARK, P_GUN);
    hx_box(-0.15, 0.15, 5.6, 5.88, 15.47, 15.5, C_DOT, P_GUN);
    hx_slant(-1.5, 1.5, 0.0, -1.5, 2.6, -11.0, -3.6, 0.5, C_POLY, P_GUN);       /* the grip */
    hx_slant(-1.25, 1.25, -11.0, -3.4, 0.3, -17.5, -4.4, -0.7, C_METAL, P_GUN); /* the magazine below it */
    hx_box(-0.5, 0.5, -4.6, 0.0, 7.5, 8.4, C_POLY, P_GUN);                   /* the trigger guard */
    hx_box(-0.5, 0.5, -4.8, -4.0, 2.4, 8.4, C_POLY, P_GUN);
    hx_box(-0.25, 0.25, -3.2, 0.0, 3.6, 4.3, C_DARK, P_GUN);                 /* the trigger */
    hx_box(-1.9, 1.9, -1.0, 4.0, -7.6, -6.0, C_DARK, P_GUN);                 /* the folded stock's butt */
    right_hand(1.3, 1.1);
    nhexw[W_UZI] = nhex;

    hex = hexw[W_SHOTGUN]; nhex = 0;                            /* the pump shotgun: a long barrel, wood */
    hx_box(-1.6, 1.6, 0.0, 4.0, -4.0, 14.0, C_METAL, P_GUN);                 /* the receiver */
    hx_box(-0.9, 0.9, 2.3, 4.0, 14.0, 62.0, C_METAL, P_GUN);                 /* the barrel */
    hx_box(-0.22, 0.22, 4.0, 4.25, 14.0, 60.0, C_DARK, P_GUN);               /* the rib */
    hx_box(-0.85, 0.85, 0.3, 2.2, 14.0, 54.0, C_DARK, P_GUN);                /* the magazine tube */
    hx_box(-0.35, 0.35, 4.0, 4.6, 60.4, 61.2, 0xD8C070, P_GUN);              /* the bead */
    hx_box(-0.6, 0.6, 2.6, 3.7, 62.0, 62.3, C_DARK, P_GUN);                  /* the muzzle */
    hx_box(-1.9, 1.9, -0.6, 2.5, 28.0, 46.0, C_WOOD, P_SLIDE);               /* the pump */
    hx_box(-0.5, 0.5, -4.2, 0.0, 5.0, 6.0, C_POLY, P_GUN);                   /* the trigger guard */
    hx_box(-0.5, 0.5, -4.4, -3.6, -0.5, 6.0, C_POLY, P_GUN);
    hx_box(-0.25, 0.25, -3.0, 0.0, 2.0, 2.7, C_DARK, P_GUN);                 /* the trigger */
    hx_slant(-1.5, 1.5, 0.0, -6.5, -1.5, -9.0, -10.0, -5.0, C_WOOD, P_GUN);     /* the grip */
    hx_taper(-4.0, -1.8, 1.8, -2.5, 1.2, -30.0, -2.2, 2.2, -13.0, -6.5, C_WOOD, P_GUN);   /* the stock */
    right_hand(1.3, -2.9);
    hx_box(-2.2, 2.2, -2.8, -0.6, 30.0, 40.0, C_SKIN, P_SLIDE);              /* the left hand under the pump */
    hx_box(-2.6, -1.8, -0.8, 2.2, 31.0, 39.0, C_SKIN, P_SLIDE);              /* its fingers on the left */
    hx_box(1.8, 2.5, -0.8, 1.6, 32.0, 38.0, C_SKIN, P_SLIDE);                /* its thumb on the right */
    hx_taper(30.5, -2.6, 1.6, -4.6, -0.8, 22.0, -4.5, 0.5, -7.5, -2.5, C_SKIN, P_SLIDE);       /* the wrist */
    hx_taper(22.5, -4.8, 0.8, -8.0, -2.0, 2.0, -12.0, -4.0, -18.0, -8.0, C_SLEEVE, P_SLIDE);  /* the left sleeve */
    nhexw[W_SHOTGUN] = nhex;
}

static EnhPoly faces[NHEX * 6];
static int nfaces;

static void rot(const Pose *ps, const double *in, double *out)
{
    double x = in[0], y = in[1], z = in[2], c, s, t;
    c = cos(ps->roll); s = sin(ps->roll); t = x * c - y * s; y = x * s + y * c; x = t;      /* about z */
    c = cos(ps->pitch); s = sin(ps->pitch); t = y * c + z * s; z = -y * s + z * c; y = t;   /* about x: + = up */
    c = cos(ps->yaw); s = sin(ps->yaw); t = x * c + z * s; z = -x * s + z * c; x = t;       /* about y: + = right */
    out[0] = x + ps->x; out[1] = y + ps->y; out[2] = z + ps->z;
}

static void gun_faces(const Pose *ps, double slide, double light, double flash, double cx, double cy, double f)
{
    static const int quads[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
    const double L[3] = { -0.45, 0.75, 0.48 };                /* towards the light: above, left, ahead (the back in shade) */
    nfaces = 0;
    for (int b = 0; b < nhexw[cur]; b++) {
        const Hex *h = &hexw[cur][b];
        double w[8][3], ctr[3] = { 0, 0, 0 };
        for (int i = 0; i < 8; i++) {
            double l[3] = { h->v[i][0], h->v[i][1], h->v[i][2] - (h->part == P_SLIDE ? slide : 0) };
            rot(ps, l, w[i]);
            for (int k = 0; k < 3; k++) ctr[k] += w[i][k] / 8;
        }
        for (int q = 0; q < 6; q++) {
            const int *qi = quads[q];
            double *a = w[qi[0]], *bb = w[qi[1]], *c = w[qi[2]], *d = w[qi[3]];
            double u[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, vv[3] = { d[0] - bb[0], d[1] - bb[1], d[2] - bb[2] };
            double n[3] = { u[1] * vv[2] - u[2] * vv[1], u[2] * vv[0] - u[0] * vv[2], u[0] * vv[1] - u[1] * vv[0] };
            double fc[3] = { (a[0] + bb[0] + c[0] + d[0]) / 4, (a[1] + bb[1] + c[1] + d[1]) / 4, (a[2] + bb[2] + c[2] + d[2]) / 4 };
            if (n[0] * (fc[0] - ctr[0]) + n[1] * (fc[1] - ctr[1]) + n[2] * (fc[2] - ctr[2]) < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
            double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl < 1e-9) continue;
            if (n[0] * fc[0] + n[1] * fc[1] + n[2] * fc[2] >= 0) continue;    /* faces away from the eye */
            double sh = 0.36 + 0.72 * fmax(0, (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / nl) + 0.10 * (n[1] / nl);
            /* clipped to z >= 3 (the sleeve reaches past the eye) */
            double in[4][3], out[8][3];
            for (int k = 0; k < 4; k++) memcpy(in[k], w[qi[k]], sizeof in[k]);
            int m = 0;
            for (int k = 0; k < 4; k++) {
                const double *p = in[k], *q2 = in[(k + 1) % 4];
                bool pin = p[2] >= 3, qin = q2[2] >= 3;
                if (pin) memcpy(out[m++], p, sizeof out[0]);
                if (pin != qin) {
                    double tt = (3 - p[2]) / (q2[2] - p[2]);
                    for (int j = 0; j < 3; j++) out[m][j] = p[j] + (q2[j] - p[j]) * tt;
                    m++;
                }
            }
            if (m < 3) continue;
            EnhPoly *F = &faces[nfaces++];
            F->n = m;
            F->z = fc[2];
            F->zd = 0;
            for (int k = 0; k < m; k++) {
                F->x[k] = cx + f * out[k][0] / out[k][2];
                F->y[k] = cy - f * out[k][1] / out[k][2];
            }
            double br = sh * light;
            int r = (int)(((h->rgb >> 16) & 0xFF) * (br + flash * 0.3) + 36 * flash);   /* lit by the flash */
            int g = (int)(((h->rgb >> 8) & 0xFF) * (br + flash * 0.22) + 20 * flash);
            int bl = (int)((h->rgb & 0xFF) * (br + flash * 0.1) + 4 * flash);
            F->rgb = (u32)(r > 255 ? 255 : r) << 16 | (u32)(g > 255 ? 255 : g) << 8 | (u32)(bl > 255 ? 255 : bl);
        }
    }
}

static int cmp_face(const void *a, const void *b)
{
    double za = ((const EnhPoly *)a)->z, zb = ((const EnhPoly *)b)->z;
    return za < zb ? 1 : za > zb ? -1 : 0;                     /* farthest first */
}

/* the polygons into a supersampled patch of the frame (painter's order; with zt, only where they are nearer than
 * the front target's depth, its column 0 / row 0 at frame pixel (ox, oy)), then onto the frame by coverage, in the
 * frame rows [ry0, ry1) */
static u32 *ss_c;
static u8 *ss_m;
static size_t ss_cap;

static void raster_polys(EnhPoly *fs, int nf, bool zt, int ox, int oy, int ry0, int ry1)
{
    if (!nf) return;
    qsort(fs, (size_t)nf, sizeof fs[0], cmp_face);
    double bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
    for (int i = 0; i < nf; i++)
        for (int k = 0; k < fs[i].n; k++) {
            bx0 = fmin(bx0, fs[i].x[k]); bx1 = fmax(bx1, fs[i].x[k]);
            by0 = fmin(by0, fs[i].y[k]); by1 = fmax(by1, fs[i].y[k]);
        }
    int X0 = (int)fmax(0, floor(bx0)), Y0 = (int)fmax(ry0, floor(by0));
    int X1 = (int)fmin(FW, ceil(bx1)), Y1 = (int)fmin(ry1, ceil(by1));
    if (X1 <= X0 || Y1 <= Y0) return;
    const int SS = S >= 3 ? 2 : 3;
    int W = (X1 - X0) * SS, H = (Y1 - Y0) * SS;
    size_t n = (size_t)W * H;
    if (n > ss_cap) {
        free(ss_c); free(ss_m);
        ss_c = malloc(sizeof(u32) * n); ss_m = malloc(n);
        ss_cap = ss_c && ss_m ? n : 0;
        if (!ss_cap) return;
    }
    memset(ss_m, 0, n);
    const EnhTarget *t = &enh_front;
    for (int i = 0; i < nf; i++) {
        const EnhPoly *F = &fs[i];
        double px[8], py[8], ymin = 1e9, ymax = -1e9;
        for (int k = 0; k < F->n; k++) {
            px[k] = (F->x[k] - X0) * SS; py[k] = (F->y[k] - Y0) * SS;
            ymin = fmin(ymin, py[k]); ymax = fmax(ymax, py[k]);
        }
        int r0 = (int)fmax(0, ceil(ymin - 0.5)), r1 = (int)fmin(H - 1, floor(ymax - 0.5));
        for (int r = r0; r <= r1; r++) {
            double yc = r + 0.5, xl = 1e9, xr = -1e9;
            for (int k = 0; k < F->n; k++) {
                int k2 = (k + 1) % F->n;
                double ya = py[k], yb = py[k2];
                if ((yc < ya) == (yc < yb)) continue;
                double x = px[k] + (yc - ya) / (yb - ya) * (px[k2] - px[k]);
                xl = fmin(xl, x); xr = fmax(xr, x);
            }
            int c0 = (int)fmax(0, ceil(xl - 0.5)), c1 = (int)fmin(W - 1, floor(xr - 0.5));
            int ty = (Y0 + r / SS - oy) * A + A / 2;
            for (int c = c0; c <= c1; c++) {
                if (zt && F->zd > 0) {
                    int tx = (X0 + c / SS - ox) * A + A / 2;
                    if (tx >= 0 && ty >= 0 && tx < t->w && ty < t->h && t->z[(size_t)ty * t->w + tx] < F->zd) continue;
                }
                ss_c[(size_t)r * W + c] = F->rgb; ss_m[(size_t)r * W + c] = 1;
            }
        }
    }
    for (int y = Y0; y < Y1; y++)
        for (int x = X0; x < X1; x++) {
            u32 R = 0, G = 0, B = 0, cnt = 0;
            for (int j = 0; j < SS; j++)
                for (int i = 0; i < SS; i++) {
                    size_t at = (size_t)((y - Y0) * SS + j) * W + (size_t)(x - X0) * SS + i;
                    if (!ss_m[at]) continue;
                    u32 c = ss_c[at];
                    R += c >> 16 & 0xFF; G += c >> 8 & 0xFF; B += c & 0xFF; cnt++;
                }
            if (!cnt || (zt && masked(x, y))) continue;
            u32 c = (R / cnt) << 16 | (G / cnt) << 8 | (B / cnt);
            u32 *o = OUT + (size_t)y * FW + x;
            *o = mix(*o, c, (double)cnt / (SS * SS));
        }
}

static void raster_faces(void) { raster_polys(faces, nfaces, false, 0, 0, 0, FH); }

void enh_polys(u32 *xrgb, int em, EnhPoly *fs, int n, int offx, int offy, int y0, int y1)
{
    S = enh_scale; A = enh_aa; FW = (320 + 2 * em) * S; FH = 200 * S; OUT = xrgb;
    raster_polys(fs, n, true, offx * S, offy * S, y0 * S, y1 * S);
}

/* a soft glow (added light), radius in output pixels */
static void glow(double cx, double cy, double r, u32 rgb, double w)
{
    int x0 = (int)fmax(0, floor(cx - r)), x1 = (int)fmin(FW, ceil(cx + r));
    int y0 = (int)fmax(0, floor(cy - r)), y1 = (int)fmin(FH, ceil(cy + r));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double dx = (x + 0.5 - cx) / r, dy = (y + 0.5 - cy) / r, q = dx * dx + dy * dy;
            if (q >= 1) continue;
            u32 *o = OUT + (size_t)y * FW + x;
            *o = add(*o, rgb, w * (1 - q) * (1 - q));
        }
}

/* a line in output pixels, `th` thick, soft-edged (crosshair, flash rays) */
static void line(double ax, double ay, double bx, double by, double th, u32 rgb, double w, bool additive)
{
    int x0 = (int)fmax(0, floor(fmin(ax, bx) - th - 1)), x1 = (int)fmin(FW, ceil(fmax(ax, bx) + th + 1));
    int y0 = (int)fmax(0, floor(fmin(ay, by) - th - 1)), y1 = (int)fmin(FH, ceil(fmax(ay, by) + th + 1));
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double px = x + 0.5 - ax, py = y + 0.5 - ay, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0;
            t = fmax(0, fmin(1, t));
            double ex = px - t * dx, ey = py - t * dy, d = sqrt(ex * ex + ey * ey);
            double c = th / 2 + 0.5 - d;
            if (c <= 0) continue;
            if (c > 1) c = 1;
            u32 *o = OUT + (size_t)y * FW + x;
            *o = additive ? add(*o, rgb, w * c) : mix(*o, rgb, w * c);
        }
}

/* ---- text: a 5 x 7 font in VGA pixels of the frame (x from the frame's left, margins included) */
static const struct { char c; u8 r[7]; } font[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } }, { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } }, { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } }, { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } }, { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } }, { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } }, { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } }, { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } }, { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } }, { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } }, { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } }, { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } }, { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } }, { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } }, { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } }, { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } }, { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } }, { '*', { 0x04, 0x04, 0x1F, 0x0E, 0x0E, 0x1B, 0x11 } }, { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
};

int enh_text_width(const char *s, int scale) { return ((int)strlen(s) * 6 - 1) * scale; }

void enh_text(u32 *xrgb, int fw, int fh, int sc, const char *s, int x, int y, int scale, u32 rgb, bool band)
{
    int n = (int)strlen(s), tw = enh_text_width(s, scale);
    if (band)
        for (int yy = (y - 3) * sc; yy < (y + 7 * scale + 3) * sc; yy++)
            for (int xx = (x - 5) * sc; xx < (x + tw + 5) * sc; xx++)
                if (xx >= 0 && yy >= 0 && xx < fw && yy < fh) xrgb[(size_t)yy * fw + xx] = mix(xrgb[(size_t)yy * fw + xx], 0, 0.59);
    for (int i = 0; i < n; i++) {
        const u8 *g = NULL;
        for (size_t k = 0; k < sizeof font / sizeof font[0]; k++) if (font[k].c == s[i]) g = font[k].r;
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++) {
                if (!(g[r] & (0x10 >> c))) continue;
                for (int j = 0; j < scale * sc; j++)
                    for (int q = 0; q < scale * sc; q++) {
                        int xx = (x + (i * 6 + c) * scale) * sc + q, yy = (y + r * scale) * sc + j;
                        if (xx >= 0 && yy >= 0 && xx < fw && yy < fh) xrgb[(size_t)yy * fw + xx] = rgb;
                    }
            }
    }
}

void enh_gun_draw(u32 *xrgb, const EnhSnap *s, int em)
{
    if (!s->foot) return;
    S = enh_scale; A = enh_aa; FW = (320 + 2 * em) * S; FH = 200 * S; OUT = xrgb;

    gun_model();
    const WDef *w = &wdef[cur];
    double zoom = enh_foot_zoom(), f = RAD2PX * zoom * S, cx = (em + 160) * S, cy = 100.0 * S;
    /* the pose: from the hip to the sights, with the recoil, the reload, the drawing, walking and turning */
    double hipw = 1 - aim;
    Pose ps = { w->hip.x * hipw, w->hip.y * hipw + w->ads_y * aim, w->hip.z * hipw + w->ads_z * aim,
                w->hip.yaw * hipw, w->hip.pitch * hipw, w->hip.roll * hipw };
    double k = since_shot < 0.6 ? exp(-since_shot / w->recoil_tau) * (1 - exp(-since_shot / 0.008)) : 0;
    ps.pitch += k * (w->recoil_hip * hipw + w->recoil_ads * aim);
    ps.z -= k * w->recoil_back;
    ps.y += k * 0.6;
    double slide = 0;
    if (cur == W_PISTOL) {
        slide = since_shot < 0.03 ? w->slide * since_shot / 0.03 : since_shot < 0.2 ? w->slide * exp(-(since_shot - 0.03) / 0.022) : 0;
        if (slide_locked) slide = w->slide;
    } else if (cur == W_UZI) {
        slide = since_shot < 0.02 ? w->slide * since_shot / 0.02 : since_shot < 0.08 ? w->slide * (1 - (since_shot - 0.02) / 0.06) : 0;
    } else if (since_shot >= 0.22 && since_shot < 0.62) {       /* the shotgun: pumped back and forth */
        slide = w->slide * sin(M_PI * (since_shot - 0.22) / 0.40);
        ps.y -= 0.8 * sin(M_PI * (since_shot - 0.22) / 0.40);
    }
    if (reload_left >= 0) {
        if (cur == W_SHOTGUN) {                                 /* turned to load shells into the tube */
            double p = 1 - reload_left / w->reload, bob = sin(M_PI * fmin(1, fmax(0, p)));
            ps.roll += 0.45; ps.pitch -= 0.12; ps.y -= 2.5 + 1.2 * bob; ps.x -= 1.0;
        } else {
            double p = 1 - reload_left / w->reload, dip = sin(M_PI * p);
            ps.y -= 6.5 * dip; ps.pitch -= 0.45 * dip; ps.roll += 0.55 * dip; ps.x -= 1.5 * dip;
            if (p > 0.72 && p < 0.86) slide = w->slide * sin(M_PI * (p - 0.72) / 0.14);   /* racked */
        }
    }
    if (since_draw < 0.35) {
        double d = 1 - since_draw / 0.35;
        ps.y -= 16 * d * d; ps.pitch -= 0.6 * d * d;
    }
    double ba = bob_amp * (1 - 0.8 * aim);
    ps.x += 0.55 * sin(bob_phase) * ba + 1.4 * sway_x * (1 - 0.6 * aim);
    ps.y += -0.38 * fabs(cos(bob_phase)) * ba + 0.8 * sway_y * (1 - 0.6 * aim);

    double flash = since_shot < 0.06 ? 1 - since_shot / 0.06 : 0;
    double light = s->day ? 1.0 : 0.5;
    gun_faces(&ps, slide, light, flash * 0.8, cx, cy, f);
    raster_faces();

    if (flash > 0) {                                           /* the muzzle flash */
        double m[3], mz[3] = { w->muzzle[0], w->muzzle[1], w->muzzle[2] + 0.6 };
        rot(&ps, mz, m);
        double big = cur == W_SHOTGUN ? 1.6 : cur == W_UZI ? 0.8 : 1.0;
        double mx = cx + f * m[0] / m[2], my = cy - f * m[1] / m[2], r = f * 5.5 * big / m[2] * (1 - 0.4 * aim);
        glow(mx, my, r * 1.6, 0xFF9030, 0.9 * flash);
        glow(mx, my, r * 0.8, 0xFFF0C0, flash);
        for (int i = 0; i < 5; i++) {
            double a = i * 2 * M_PI / 5 + since_shot * 40 + i * 0.7 + (cur == W_UZI ? frand() * 2 : 0);
            double l = r * (1.1 + 0.9 * ((i * 37) % 7) / 7.0);
            line(mx, my, mx + cos(a) * l, my + sin(a) * l * 0.8, r * 0.18, 0xFFD080, 0.8 * flash, true);
        }
    }

    /* the crosshair (from the hip): four ticks, opening with the spread (the shotgun: its pattern); red ticks
     * when a vehicle or a gunman was hit */
    double ch = 1 - aim;
    if (ch > 0.05) {
        double sp = cur == W_SHOTGUN ? fmax(spread, w->spread * 0.7) : spread;
        double g = (2.5 + sp * RAD2PX * zoom * 0.8) * S, l = 3.5 * S, th = fmax(1.6, S * 0.55);
        double qx = cx + 0.5, qy = cy + 0.5;                   /* on the pixels' centres, not between them */
        static const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int i = 0; i < 4; i++) {
            double ax = qx + dirs[i][0] * g, ay = qy + dirs[i][1] * g, bx = qx + dirs[i][0] * (g + l), by = qy + dirs[i][1] * (g + l);
            line(ax, ay, bx, by, th + 2, 0x000000, 0.5 * ch, false);
            line(ax, ay, bx, by, th, 0xF4F4F4, ch, false);
        }
    }
    if (since_hit < 0.2) {
        double hw = 1 - since_hit / 0.2, a = 3.0 * S, b = 6.5 * S, th = fmax(1.6, S * 0.6);
        static const int dg[4][2] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
        for (int i = 0; i < 4; i++)
            line(cx + dg[i][0] * a, cy + dg[i][1] * a, cx + dg[i][0] * b, cy + dg[i][1] * b, th, 0xFF3020, hw, false);
    }

    /* the weapon, the rounds in it and in reserve, bottom right; RELOAD while reloading */
    char buf[24];
    int x = em + 320 - 8;
    enh_text(xrgb, FW, FH, S, w->name, x - enh_text_width(w->name, 1), 168, 1, 0xC8C0A0, false);
    if (reload_left >= 0 && cur != W_SHOTGUN) {
        snprintf(buf, sizeof buf, "RELOAD");
        enh_text(xrgb, FW, FH, S, buf, x - enh_text_width(buf, 1) - 2, 184, 1, 0xE0D8B0, true);
    } else {
        snprintf(buf, sizeof buf, "%d", mag[cur]);
        u32 col = mag[cur] == 0 ? 0xFF5040 : mag[cur] * 4 <= w->mag ? 0xFFC060 : 0xF0E8D0;
        char rs[12];
        snprintf(rs, sizeof rs, "%d", reserve[cur]);
        int wr = enh_text_width(rs, 1), wm = enh_text_width(buf, 2);
        enh_text(xrgb, FW, FH, S, rs, x - wr, 186, 1, reserve[cur] ? 0xB0A890 : 0xFF5040, true);
        enh_text(xrgb, FW, FH, S, buf, x - wr - 8 - wm, 180, 2, col, true);
    }
    /* what was picked up */
    if (pick_t < 1.8) enh_text(xrgb, FW, FH, S, pick_msg, em + 160 - enh_text_width(pick_msg, 1) / 2, 150, 1, 0x90FF90, true);
}

/* ------------------------------------------------------------------------------------------------------
 * Burnt-out vehicles: their colours through the darkest greys of the current palette
 * ------------------------------------------------------------------------------------------------------ */

void enh_burnt_map(u8 map[256])
{
    int lum[256], greys[256], ng = 0;
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        vga_dac_read((u8)i, &r, &g, &b);
        lum[i] = r * 30 + g * 59 + b * 11;                      /* 0 .. 6300 */
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        if (mx - mn <= 4) greys[ng++] = i;
    }
    for (int i = 0; i < 256; i++) {
        int want = 250 + lum[i] / 5, best = i, bd = 1 << 30;
        for (int k = 0; k < ng; k++) {
            int d = abs(lum[greys[k]] - want);
            if (d < bd) { bd = d; best = greys[k]; }
        }
        map[i] = (u8)best;
    }
}
