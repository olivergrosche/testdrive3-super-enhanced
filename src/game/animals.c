/* OG: (ANIMALS.md) the leg's cows and horses graze, walk about their pasture and run from a fast car, the walker and
 * gunfire; their legs swing as they go. See animals.h. */
#define RENDER3D_INTERNAL                                 /* model_emit_vertices / model_emit_faces */
#include "game/game.h"
#include "game/animals.h"
#include "game/village.h"
#include "enhanced/enhanced.h"

#include <math.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NANIM     40
#define NMODEL     4
#define MAXLEG     8
#define LEGV       6
#define WANDER   800.0                  /* x4 units from home a grazing animal walks about */
#define WALK_V    70.0                  /* x4 units a second (the game's houses are about half size: a calm walk) */
#define RUN_V    330.0
#define FLEE_CAR 2600.0                 /* a car faster than CAR_FAST this near: they run */
#define CAR_FAST  18                    /* car_speed (about 0.77 km/h a unit) */
#define FLEE_FOOT 380.0                 /* the walker this near: they step away */
#define FLEE_SHOT 5000.0

/* a model's legs: chains of line faces from the hip (pivot) down to the ground */
typedef struct {
    u8 model;
    bool parked;                        /* the vertex arrays at +8 (parked objects) or +4 (statics) */
    u16 es, arrays;
    int nv, nf;
    int nleg, pivot[MAXLEG], nmem[MAXLEG], mem[MAXLEG][LEGV], group[MAXLEG];
    s16 save_x[MAXLEG][LEGV], save_h[MAXLEG][LEGV];
    double headsign;                    /* +1: the head at +x of the model's frame, -1 at -x */
} AnimModel;

enum { GRAZE, WALK, RUN };

typedef struct {
    int slot;                           /* object slot */
    int mi;                             /* its AnimModel */
    bool parked;
    double hx, hz;                      /* home, true x4 */
    double x, z, y;                     /* now (y height units) */
    double head;                        /* the way it faces: radians, world (x, z) = (sin, cos) */
    int state;
    double t, tx, tz, speed, phase, amp;
    u32 rnd;
    bool emitted;
} Animal;

static AnimModel models[NMODEL];
static int nmodels;
static Animal an[NANIM];
static int n_anim;
static u32 an_key = 0xFFFFFFFFu;
static u64 last_ns;
static int posed = -1;                  /* the AnimModel whose legs are out of place now */

static u32 rnd_next(u32 *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static double frnd(u32 *s) { return (rnd_next(s) & 0xFFFFFF) / 16777216.0; }

/* the model's legs, or false (not an animal: fewer than four chains reaching the ground, or too big) */
static bool analyse(AnimModel *M, u8 model, bool parked)
{
    memset(M, 0, sizeof *M);
    u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
    if (!es) return false;
    u16 p = (u16)(rd16(es, (u16)(set + 2 * model)) + set);
    int nf = rd8(es, p), nv = rd8(es, (u16)(p + 1));
    if (nf <= 0 || nv <= 0 || nv > 200) return false;
    u16 a = (u16)(p + (parked ? 8 : 4)), stride = (u16)(nv * 2);
    s16 h[200], x[200], z[200];
    double hmax = -1e9, x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (int k = 0; k < nv; k++) {
        h[k] = (s16)rd16(es, (u16)(a + 2 * k));
        x[k] = (s16)rd16(es, (u16)(a + stride + 2 * k));
        z[k] = (s16)rd16(es, (u16)(a + 2 * stride + 2 * k));
        hmax = fmax(hmax, h[k]); x0 = fmin(x0, x[k]); x1 = fmax(x1, x[k]); z0 = fmin(z0, z[k]); z1 = fmax(z1, z[k]);
    }
    if (x1 - x0 > 260 || z1 - z0 > 260 || hmax > 140) return false;     /* an animal is small */
    /* the line faces low down, joined by their shared corners */
    int parent[200];
    for (int k = 0; k < nv; k++) parent[k] = k;
    bool inleg[200] = { false };
    double top = 0.45 * hmax;
    u16 f = (u16)(a + 3 * stride);
    for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
        u16 w0 = rd16(es, f), w1 = rd16(es, (u16)(f + 2));
        if ((w0 >> 14) != 1) continue;                                   /* two corners: a line */
        int v0 = w0 & 0x7FF, v1 = w1 & 0x7FF;
        if (v0 >= nv || v1 >= nv || h[v0] > top || h[v1] > top) continue;
        inleg[v0] = inleg[v1] = true;
        int r0 = v0, r1 = v1;
        while (parent[r0] != r0) r0 = parent[r0];
        while (parent[r1] != r1) r1 = parent[r1];
        parent[r0] = r1;
    }
    double xm = (x0 + x1) / 2, zm = (z0 + z1) / 2;
    for (int r = 0; r < nv && M->nleg < MAXLEG; r++) {
        if (!inleg[r]) continue;
        int root = r;
        while (parent[root] != root) root = parent[root];
        if (root != r) continue;
        /* the chain of root r: its highest corner is the hip, it must reach the ground */
        int mem[LEGV + 1], n = 0, piv = -1, lo = 9999;
        for (int k = 0; k < nv; k++) {
            if (!inleg[k]) continue;
            int q = k;
            while (parent[q] != q) q = parent[q];
            if (q != r) continue;
            if (n < LEGV + 1) mem[n++] = k;
            if (piv < 0 || h[k] > h[piv]) piv = k;
            if (h[k] < lo) lo = h[k];
        }
        if (n < 2 || lo > 0.2 * hmax) continue;                         /* to the ground (or a hoof lifted) */
        int L = M->nleg++;
        M->pivot[L] = piv;
        for (int i = 0; i < n; i++) if (mem[i] != piv && M->nmem[L] < LEGV) M->mem[L][M->nmem[L]++] = mem[i];
        M->group[L] = (x[piv] > xm) != (z[piv] > zm);                    /* diagonal pairs step together */
    }
    if (SDL_getenv("TD3_FOES_DEBUG") && M->nleg > 0) SDL_Log("animals: model %d (%s): %d legs found", model, parked ? "parked" : "static", M->nleg);
    if (M->nleg < 4) return false;
    /* the head: the end of the body with the higher corners */
    double hp = -1e9, hn = -1e9, w = x1 - x0;
    for (int k = 0; k < nv; k++) {
        if (x[k] > x1 - 0.15 * w) hp = fmax(hp, h[k]);
        if (x[k] < x0 + 0.15 * w) hn = fmax(hn, h[k]);
    }
    M->headsign = hp >= hn ? 1 : -1;
    M->model = model; M->parked = parked; M->es = es; M->arrays = a; M->nv = nv; M->nf = nf;
    return true;
}

static int model_index(u8 model, bool parked)
{
    for (int i = 0; i < nmodels; i++) if (models[i].model == model && models[i].parked == parked) return i;
    if (nmodels >= NMODEL) return -1;
    if (!analyse(&models[nmodels], model, parked)) return -1;
    return nmodels++;
}

/* the leg's animals, found again when the leg or its data changed */
static void prepare(void)
{
    u32 key = (u32)DSW(DS_scene_index) << 24 ^ (u32)DSB(DS_leg_index) << 16 ^ DSW(DS_objects_set) ^
              (u32)DSW(DS_objects_set + 2) << 8 ^ (u32)DSW(DS_obj_count) << 4;
    if (key == an_key) return;
    an_key = key;
    n_anim = nmodels = 0;
    posed = -1;
    static int off = -1;
    if (off < 0) off = SDL_getenv("TD3_NO_ANIMALS") != NULL;
    if (off) return;
    int first = DSW(DS_obj_first_static), n = DSW(DS_obj_count);
    bool bad[64] = { false };                                            /* models found not to be animals */
    for (int o = 1; o < n && o < 160 && n_anim < NANIM; o++) {
        u16 fl = DSW(DS_obj_flags + 2 * o);
        bool stat = o >= first, parked = !stat && (fl & 0x3000) == 0x1000;
        if (!stat && !parked) continue;
        if (stat && ((fl >> 8) & 0x30)) continue;
        u8 m = (u8)(fl & 0x3F);
        if (m <= 3 || bad[m]) continue;
        int mi = model_index(m, parked);
        if (mi < 0) { bad[m] = true; continue; }
        Animal *A = &an[n_anim++];
        memset(A, 0, sizeof *A);
        A->slot = o; A->mi = mi; A->parked = parked;
        A->hx = A->x = DSW(DS_obj_x + 2 * o) * 4.0;
        A->hz = A->z = DSW(DS_obj_z + 2 * o) * 4.0;
        A->y = DSW(DS_obj_y8 + 2 * o) >> 3;
        u8 rot = (u8)(DSW(DS_obj_heading + 2 * o) >> 8);
        if (stat) rot &= 0xC0;
        /* the way the head faces: the model's +x turned by rot is (cos, -sin) in (x, z) */
        double th = rot * (2.0 * M_PI / 256.0), fx = cos(th) * models[mi].headsign, fz = -sin(th) * models[mi].headsign;
        A->head = atan2(fx, fz);
        A->rnd = 0x9E3779B9u ^ (u32)o * 2654435761u ^ key;
        A->state = GRAZE;
        A->t = 2 + 8 * frnd(&A->rnd);
    }
    if (SDL_getenv("TD3_FOES_DEBUG"))
        for (int i = 0; i < nmodels; i++)
            SDL_Log("animals: model %d (%s): %d legs, head %+.0f", models[i].model, models[i].parked ? "parked" : "static",
                    models[i].nleg, models[i].headsign);
    if (SDL_getenv("TD3_FOES_DEBUG")) SDL_Log("animals: %d on the leg", n_anim);
}

/* where an animal may stand: ground (not water, road or a hole), not in a house, not too steep from (fx, fz) */
static bool pasture(double X, double Z, double fy, double *y)
{
    double g = village_ground(X, Z);
    int t = village_ground_type();
    if (g < -1e8 || t == 0x0E || (t >= 0x10 && t <= 0x14)) return false;
    if (fy > -1e8 && fabs(g - fy) > 40) return false;
    if (enh_town_house_at(X, Z, 150)) return false;
    *y = g;
    return true;
}

static double ang_diff(double a, double b)
{
    double d = fmod(a - b, 2 * M_PI);
    if (d > M_PI) d -= 2 * M_PI;
    if (d < -M_PI) d += 2 * M_PI;
    return d;
}

static void flee(Animal *A, double fx, double fz, double secs)
{
    double ax = A->x - fx, az = A->z - fz, d = hypot(ax, az);
    if (d < 1) { ax = sin(A->head); az = cos(A->head); d = 1; }
    if (SDL_getenv("TD3_FOES_DEBUG") && secs > 0) SDL_Log("animals: %d runs (%.0f away)", (int)(A - an), d);
    A->state = RUN;
    A->t = secs;
    A->tx = A->x + ax / d * 3000;
    A->tz = A->z + az / d * 3000;
}

void animals_heard(void)
{
    double px = (u32)DSL(DS_pos_x) / 32.0, pz = (u32)DSL(DS_pos_z) / 32.0;
    for (int i = 0; i < n_anim; i++)
        if (hypot(an[i].x - px, an[i].z - pz) < FLEE_SHOT) flee(&an[i], px, pz, 4 + 3 * frnd(&an[i].rnd));
}

void animals_update(void)
{
    if (DSB(DS_menu_preview)) return;
    prepare();
    u64 now = host_time_ns();
    double dt = last_ns ? (now - last_ns) / 1e9 : 0;
    last_ns = now;
    if (dt > 0.1) dt = 0.1;
    if (dt <= 0 || DSB(DS_replay_state)) return;
    double px = (u32)DSL(DS_pos_x) / 32.0, pz = (u32)DSL(DS_pos_z) / 32.0;
    bool foot = foot_active();
    int spd = DSW(DS_car_speed);
    for (int i = 0; i < n_anim; i++) {
        Animal *A = &an[i];
        double dp = hypot(A->x - px, A->z - pz);
        if (dp > 30000) continue;                                        /* far off: as they are */
        /* what frightens them */
        if (!foot && spd > CAR_FAST && dp < FLEE_CAR && A->state != RUN) flee(A, px, pz, 3 + 2 * frnd(&A->rnd));
        if (foot && dp < FLEE_FOOT && A->state == GRAZE) {
            flee(A, px, pz, 0);
            A->state = WALK;
            A->tx = A->x + (A->x - px) / fmax(dp, 1) * 500;
            A->tz = A->z + (A->z - pz) / fmax(dp, 1) * 500;
        }
        A->t -= dt;
        double want_v = 0, want_amp = 0;
        if (A->state == GRAZE) {
            if (A->t <= 0) {                                             /* a few steps somewhere on the pasture */
                double r = WANDER * sqrt(frnd(&A->rnd)), a = 2 * M_PI * frnd(&A->rnd), y;
                double tx = A->hx + r * sin(a), tz = A->hz + r * cos(a);
                if (pasture(tx, tz, -1e9, &y)) { A->state = WALK; A->tx = tx; A->tz = tz; A->t = 20; }
                else A->t = 0.5;
            }
        } else {
            double dx = A->tx - A->x, dz = A->tz - A->z, d = hypot(dx, dz);
            if (d < 40 || A->t <= 0) {
                A->state = GRAZE;
                A->t = 4 + 9 * frnd(&A->rnd);
            } else {
                want_v = A->state == RUN ? RUN_V : WALK_V;
                want_amp = A->state == RUN ? 0.55 : 0.32;
                double want_h = atan2(dx, dz), turn = (A->state == RUN ? 3.0 : 1.4) * dt, e = ang_diff(want_h, A->head);
                A->head += fabs(e) < turn ? e : (e > 0 ? turn : -turn);
                if (fabs(e) > 1.2) want_v *= 0.3;                       /* turning round first */
            }
        }
        A->speed += (want_v - A->speed) * fmin(1.0, dt * 3.0);
        A->amp += (want_amp - A->amp) * fmin(1.0, dt * 4.0);
        if (A->speed > 1) {
            double nx = A->x + sin(A->head) * A->speed * dt, nz = A->z + cos(A->head) * A->speed * dt, y;
            if (pasture(nx, nz, A->y, &y)) { A->x = nx; A->z = nz; A->y = y; }
            else { A->state = GRAZE; A->t = 1 + 2 * frnd(&A->rnd); A->speed = 0; }
        }
        /* the gait: a stride (both pairs) takes 1.0 s walking, 0.45 s running */
        A->phase += dt * 2 * M_PI * (A->speed / WALK_V) * (A->state == RUN ? 0.47 : 1.0);
        if (A->amp < 0.02 && A->speed < 1) A->phase = 0;
        if (A->parked) {                                                 /* the game draws it from its slot */
            int o = A->slot;
            DSW(DS_obj_x + 2 * o) = (u16)((u16)lround(A->x / 4.0) & 0x7FFF);
            DSW(DS_obj_z + 2 * o) = (u16)((u16)lround(A->z / 4.0) & 0x3FFF);
            DSW(DS_obj_y8 + 2 * o) = (u16)(lround(A->y) << 3);
            const AnimModel *M = &models[A->mi];
            double th = atan2(-cos(A->head) * M->headsign, sin(A->head) * M->headsign);   /* model +x to the head */
            DSW(DS_obj_heading + 2 * o) = (u16)((u16)lround(th / (2 * M_PI) * 256.0) << 8);
        }
    }
    /* developer aid TD3_ANIMAL_WATCH=k: once out of the car, the walker stands 6 m to the side of animal k, looking
     * at it, wherever it goes (with TD3_SNAPSHOT_DIR, a film of it) */
    const char *aw = SDL_getenv("TD3_ANIMAL_WATCH");
    if (aw && foot && SDL_atoi(aw) < n_anim) {
        const Animal *A = &an[SDL_atoi(aw)];
        double cx = A->x + cos(A->head) * 600, cz = A->z - sin(A->head) * 600, g = village_ground(cx, cz);
        foot_teleport(cx, cz, (g > -1e8 ? g : A->y) + 2);
        foot_set_view(atan2(A->x - cx, A->z - cz) / (2 * M_PI) * 65536.0, -18);
    }
}

/* the legs of model mi in animal A's pose */
static void pose(const Animal *A)
{
    AnimModel *M = &models[A->mi];
    if (A->amp < 0.01) return;
    u16 es = M->es, a = M->arrays, stride = (u16)(M->nv * 2);
    for (int L = 0; L < M->nleg; L++) {
        double ang = A->amp * sin(A->phase + (M->group[L] ? M_PI : 0)) * M->headsign;
        double cs = cos(ang), sn = sin(ang);
        int pv = M->pivot[L];
        double ph = (s16)rd16(es, (u16)(a + 2 * pv)), px = (s16)rd16(es, (u16)(a + stride + 2 * pv));
        for (int k = 0; k < M->nmem[L]; k++) {
            int v = M->mem[L][k];
            s16 h = (s16)rd16(es, (u16)(a + 2 * v)), x = (s16)rd16(es, (u16)(a + stride + 2 * v));
            M->save_h[L][k] = h; M->save_x[L][k] = x;
            double dx = x - px, dy = h - ph;
            wr16(es, (u16)(a + stride + 2 * v), (u16)(s16)lround(px + dx * cs - dy * sn));
            wr16(es, (u16)(a + 2 * v), (u16)(s16)lround(ph + dx * sn + dy * cs));
        }
    }
    posed = A->mi;
}

void animals_unpose(void)
{
    if (posed < 0) return;
    AnimModel *M = &models[posed];
    u16 es = M->es, a = M->arrays, stride = (u16)(M->nv * 2);
    for (int L = 0; L < M->nleg; L++)
        for (int k = 0; k < M->nmem[L]; k++) {
            int v = M->mem[L][k];
            wr16(es, (u16)(a + 2 * v), (u16)M->save_h[L][k]);
            wr16(es, (u16)(a + stride + 2 * v), (u16)M->save_x[L][k]);
        }
    posed = -1;
}

bool animals_pose_slot(u16 slot2_bx)
{
    int o = slot2_bx >> 1;
    for (int i = 0; i < n_anim; i++) {
        if (an[i].slot != o || !an[i].parked) continue;
        DSW(DS_obj_vert_count + slot2_bx) = 0xFFFF;                      /* no reuse of last frame's vertices */
        pose(&an[i]);
        return true;
    }
    return false;
}

bool animals_static_hidden(int i)
{
    for (int k = 0; k < n_anim; k++) if (an[k].slot == i && !an[k].parked) return true;
    return false;
}

/* model_place's turn byte for animal A (the model's +x to its head) */
static u8 turn_of(const Animal *A)
{
    const AnimModel *M = &models[A->mi];
    double th = atan2(-cos(A->head) * M->headsign, sin(A->head) * M->headsign);
    return (u8)lround(th / (2 * M_PI) * 256.0);
}

void animals_emit_statics(void)
{
    if (DSB(DS_menu_preview)) return;
    prepare();
    for (int i = 0; i < n_anim; i++) {
        Animal *A = &an[i];
        A->emitted = false;
        if (A->parked) continue;
        u16 x = (u16)((u16)lround(A->x / 4.0) & 0x7FFF), z = (u16)((u16)lround(A->z / 4.0) & 0x3FFF);
        /* the parked objects' window round the camera */
        u16 ax = (u16)(x - DSW(DS_cam_x));
        ax = (u16)((s16)(u16)(ax << 1) >> 1);
        ax = (u16)(ax + DSW(DS_view_dist));
        if (ax > DSW(DS__2)) continue;
        u16 az = (u16)(z - DSW(DS_cam_z));
        az = (u16)((s16)(u16)(az << 2) >> 2);
        az = (u16)(az + DSW(DS_view_dist));
        if (az > DSW(DS__2)) continue;
        const AnimModel *M = &models[A->mi];
        DSW(DS_place_x) = x;
        DSW(DS_place_z) = z;
        DSW(DS_place_y) = (u16)lround(A->y);
        DSB(DS_place_rot) = turn_of(A);
        DSB(DS_place_shear) = 0;
        DSW(DS_cur_obj_slot1) = 0;                                       /* no object slot */
        DSW(DS_model_face_cut) = 0;
        DSB(DS_vert_overflow) = 0;
        DSB(DS_place_nverts) = (u8)M->nv;
        pose(A);
        u16 f = model_emit_vertices(far_make(M->es, M->arrays), (u16)M->nv);
        if (DSB(DS_vert_overflow) == 0) { model_emit_faces(far_make(M->es, f), (u16)M->nf); A->emitted = true; }
        animals_unpose();
    }
}

bool animals_far(int k, u8 *model, u16 *px, u16 *pz, u16 *py, u8 *rot)
{
    for (int i = 0, n = 0; i < n_anim; i++) {
        const Animal *A = &an[i];
        if (A->parked || A->emitted) continue;
        if (n++ != k) continue;
        *model = models[A->mi].model;
        *px = (u16)((u16)lround(A->x / 4.0) & 0x7FFF);
        *pz = (u16)((u16)lround(A->z / 4.0) & 0x3FFF);
        *py = (u16)lround(A->y);
        *rot = turn_of(A);
        return true;
    }
    return false;
}
