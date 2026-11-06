/* OG: (TOWNS.md "Real villages") houses added to a leg where a village was planned: copies of the leg's own house
 * models (the parked objects' houses and the statics' barns, silos, sheds), each placed by the middle of its walls
 * and the way its front faces, on the ground the leg's tiles have there. world_build_visible emits them as
 * model_place emits the static objects (model_emit_vertices / model_emit_faces at the cell it builds), so the
 * original renderer draws them, the car crashes into their walls (near_face_hits) and the walker meets them. */
#define RENDER3D_INTERNAL                                 /* model_emit_vertices / model_emit_faces */
#include "game/game.h"
#include "game/village.h"

#include <math.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <string.h>

typedef struct {
    u8 model;           /* 0: not a house added, the service sv for the leg's own house nearest (x, z) */
    bool parked;
    double x, z;        /* the middle of the walls (true x4 units: 0 .. 131072, 0 .. 65536) */
    u8 rot;
    int sv;
} Plan;

/* a planned village: scene (0 Pacific - Yosemite, 1 Cape Cod - Niagara), leg (0-based) */
typedef struct {
    int scene, leg;
    const char *name;
    int n;
    Plan h[24];
} Village;

/* The models' fronts (from the leg's own houses, whose doors face the road): the porch house 55 and the long house
 * 50 (parked objects), the barn 51, the small house 54 and the silo 52 (statics). Front south: 55 rot 0, 50 / 51
 * rot 40h; front north: 55 rot 80h, 50 / 51 rot C0h. */
static const Village villages[] = {
    /* PILGRIM COVE: Cape Cod - Niagara, Pilgrim Way, by the start. The main street runs 75 m east from the bridge
     * (its middle z 59408, 4.8 m wide) between two dunes at either end and bends south; the village takes its level
     * middle (x 115000 .. 119400): houses on both sides 6.5 m back, fronts to it, the gun shop and the pub east of
     * the village pond, the garage across, its bay at the road; a farm on the level ground beyond the bend, by the
     * leg's own house there, the police station. */
    { 1, 0, "PILGRIM COVE", 10, {
        { 50, true,  116900, 60546, 0x40, VSV_GUNS },
        { 55, true,  118100, 60506, 0x00, VSV_PUB },
        { 54, false, 119050, 60490, 0x00, VSV_NONE },
        { 54, false, 115300, 58290, 0x80, VSV_NONE },
        { 55, true,  116300, 58302, 0x80, VSV_NONE },
        { 54, false, 117050, 58290, 0x80, VSV_NONE },
        { 51, false, 117920, 58300, 0xC0, VSV_GARAGE },
        { 51, false, 122900, 61200, 0x40, VSV_NONE },
        { 52, false, 123550, 61200, 0x00, VSV_NONE },
        { 0,  false, 123568, 58200, 0x00, VSV_POLICE },               /* the leg's own house beyond the bend */
    } },
};
#define NVILLAGES ((int)(sizeof villages / sizeof villages[0]))

#define SLOPE_MAX 12.0                  /* height units (about 28 cm) the ground may rise under a house and round it */

static VillageHouse placed[24];
static int nplaced;
static struct { double x, z; int sv; } tags[8];
static int ntags;
static bool emitted[24];
static int build_col, build_row;
static const char *placed_name;
static u32 placed_key = 0xFFFFFFFFu;

/* the objects set's model m: its header, the vertex arrays A (height), B (x), C (z) at si + 4 (statics) or si + 8
 * (parked objects), nf faces after them */
static bool model_at(u8 m, bool parked, u16 *es, u16 *arrays, int *nf, int *nv)
{
    *es = DSW(DS_objects_set + 2);
    u16 set = DSW(DS_objects_set);
    if (!*es) return false;
    u16 si = (u16)(rd16(*es, (u16)(set + 2 * m)) + set);
    *nf = rd8(*es, si);
    *nv = rd8(*es, (u16)(si + 1));
    *arrays = (u16)(si + (parked ? 8 : 4));
    return *nf > 0 && *nv > 0;
}

/* the middle of the model's walls and roof (its solid faces) in its own frame */
static bool model_middle(u8 m, bool parked, double *cx, double *cz, double *hx, double *hz)
{
    u16 es, a;
    int nf, nv;
    if (!model_at(m, parked, &es, &a, &nf, &nv)) return false;
    u16 stride = (u16)(nv * 2), f = (u16)(a + 3 * stride);
    double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
        u16 w0 = rd16(es, f);
        int t = rd16(es, (u16)(f + 6)) >> 11;
        if (t != 1 && t != 2 && t != 3) continue;
        for (int j = 0; j < (w0 >> 14) + 1; j++) {
            int vi = rd16(es, (u16)(f + 2 * j)) & 0x7FF;
            if (vi >= nv) continue;
            double x = (s16)rd16(es, (u16)(a + stride + 2 * vi)), z = (s16)rd16(es, (u16)(a + 2 * stride + 2 * vi));
            x0 = fmin(x0, x); x1 = fmax(x1, x); z0 = fmin(z0, z); z1 = fmax(z1, z);
        }
    }
    if (x0 > x1) return false;
    *cx = (x0 + x1) / 2; *cz = (z0 + z1) / 2; *hx = (x1 - x0) / 2; *hz = (z1 - z0) / 2;
    return true;
}

/* the ground of the leg's tiles at (X, Z) (true x4): the highest ground face there, height units; -1e9 none */
static int ground_type;                 /* the face type of the last ground() found */
static double ground(double X, double Z)
{
    ground_type = -1;
    int col = (int)floor(X / 4096.0), zc = (int)floor(Z / 4096.0), row = 15 - zc;
    if (col < 0 || col > 31 || row < 0 || row > 15) return -1e9;
    u16 m = DSW(DS_leg_map + 2 * (col + row * 32));
    u8 al = (u8)m;
    u16 es, set, bx, cut = 0;
    if (al >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); al = (u8)(al - 0x40); }
    else            { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
    if (!es) return -1e9;
    bx = (u16)(set + 2 * al);
    u16 e = rd16(es, bx);
    if (e <= 0x10) { cut = e; bx = (u16)(bx - 2); }
    u16 si = (u16)(rd16(es, bx) + set);
    int nf = rd8(es, si) - cut, nv = rd8(es, (u16)(si + 1));
    u8 rot = (u8)((m >> 8) & 0xC0);
    double ox = col * 4096.0 + 2048.0, oz = zc * 4096.0 + 2048.0, oy = (m & 0x3F00), best = -1e9;
    u16 stride = (u16)(nv * 2), f = (u16)(si + 4 + 3 * stride);
    for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
        u16 w[4];
        for (int j = 0; j < 4; j++) w[j] = rd16(es, (u16)(f + 2 * j));
        int t = w[3] >> 11, n = (w[0] >> 14) + 1;
        if (n < 3 || t == 0 || t == 1 || t == 3) continue;           /* the ground: not walls */
        double vx[4], vz[4], vy[4];
        bool ok = true;
        for (int j = 0; j < n; j++) {
            int vi = w[j] & 0x7FF;
            if (vi >= nv) { ok = false; break; }
            double a = (s16)rd16(es, (u16)(si + 4 + 2 * vi)), b = (s16)rd16(es, (u16)(si + 4 + stride + 2 * vi)),
                   c = (s16)rd16(es, (u16)(si + 4 + 2 * stride + 2 * vi)), rb = b, rc = c;
            if (rot == 0x40)      { rb = c; rc = -b; }
            else if (rot == 0x80) { rb = -b; rc = -c; }
            else if (rot == 0xC0) { rb = -c; rc = b; }
            vx[j] = ox + rb; vz[j] = oz + rc; vy[j] = oy + a;
        }
        if (!ok) continue;
        for (int t2 = 0; t2 + 2 < n; t2++) {                         /* the fan's triangles */
            int i0 = 0, i1 = 1 + t2, i2 = 2 + t2;
            double d = (vz[i1] - vz[i2]) * (vx[i0] - vx[i2]) + (vx[i2] - vx[i1]) * (vz[i0] - vz[i2]);
            if (fabs(d) < 1e-9) continue;
            double l0 = ((vz[i1] - vz[i2]) * (X - vx[i2]) + (vx[i2] - vx[i1]) * (Z - vz[i2])) / d;
            double l1 = ((vz[i2] - vz[i0]) * (X - vx[i2]) + (vx[i0] - vx[i2]) * (Z - vz[i2])) / d, l2 = 1 - l0 - l1;
            if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6) continue;
            double y = l0 * vy[i0] + l1 * vy[i1] + l2 * vy[i2];
            if (y > best) { best = y; ground_type = t; }
        }
    }
    return best;
}

double village_ground(double X, double Z) { return ground(X, Z); }

/* the current leg's houses: placed again when the leg or its data changed */
static void prepare(void)
{
    u32 key = (u32)DSW(DS_scene_index) << 24 ^ (u32)DSB(DS_leg_index) << 16 ^ DSW(DS_objects_set) ^ (u32)DSW(DS_objects_set + 2) << 8;
    if (key == placed_key) return;
    placed_key = key;
    nplaced = 0;
    ntags = 0;
    placed_name = NULL;
    for (int v = 0; v < NVILLAGES; v++) {
        const Village *V = &villages[v];
        if (V->scene != DSW(DS_scene_index) || V->leg != DSB(DS_leg_index)) continue;
        placed_name = V->name;
        for (int i = 0; i < V->n && nplaced < 24; i++) {
            const Plan *p = &V->h[i];
            if (p->model == 0) {                                     /* a service for one of the leg's houses */
                if (ntags < 8) { tags[ntags].x = p->x; tags[ntags].z = p->z; tags[ntags].sv = p->sv; ntags++; }
                continue;
            }
            double cx, cz, hx, hz;
            if (!model_middle(p->model, p->parked, &cx, &cz, &hx, &hz)) continue;
            /* vertex_rotate: x' = x cos + z sin, z' = -x sin + z cos; the origin so that the middle lands there */
            double phi = p->rot * (2.0 * M_PI / 256.0), cs = cos(phi), sn = sin(phi);
            double ox = p->x - (cx * cs + cz * sn), oz = p->z - (-cx * sn + cz * cs);
            /* level ground only: a 5 x 5 grid over the footprint and 1.5 m round it; a house on a slope or at its foot
             * (half sunk into the hill, or floating) is left out */
            double g = 1e9, gmax = -1e9;
            bool hole = false, wet = false;
            for (int iu = 0; iu < 5; iu++)
                for (int iw = 0; iw < 5; iw++) {
                    double u = (iu - 2) / 2.0 * (hx + 150), w = (iw - 2) / 2.0 * (hz + 150);
                    double gy = ground(p->x + u * cs + w * sn, p->z - u * sn + w * cs);
                    if (gy < -1e8) { hole = true; continue; }
                    if (ground_type == 0x0E || (ground_type >= 0x10 && ground_type <= 0x14)) wet = true;   /* water, road */
                    if (gy < g) g = gy;
                    if (gy > gmax) gmax = gy;
                }
            if (SDL_getenv("TD3_TOWN_DUMP"))
                SDL_Log("village: %s house %d (model %d) at %.0f %.0f: ground %.0f .. %.0f%s", V->name, i, p->model, p->x, p->z,
                        g, gmax, hole || g > 1e8 ? " (no ground: left out)" : wet ? " (water or road: left out)" :
                        gmax - g > SLOPE_MAX ? " (a slope: left out)" : "");
            if (hole || wet || g > 1e8 || gmax - g > SLOPE_MAX) continue;
            VillageHouse *h = &placed[nplaced++];
            h->model = p->model;
            h->parked = p->parked;
            h->px = (u16)lround(ox / 4.0);
            h->pz = (u16)lround(oz / 4.0);
            h->py = (u16)lround(g);
            h->rot = p->rot;
            h->sv = p->sv;
        }
    }
}

int village_count(void)
{
    static int off = -1;                                             /* developer aid: TD3_NO_VILLAGE=1 */
    if (off < 0) off = SDL_getenv("TD3_NO_VILLAGE") != NULL;
    if (DSB(DS_menu_preview) || off) return 0;
    prepare();
    return nplaced;
}

const VillageHouse *village_house(int i)
{
    return i >= 0 && i < nplaced ? &placed[i] : NULL;
}

int village_tag(int i, double *x, double *z)
{
    if (DSB(DS_menu_preview)) return VSV_NONE;
    prepare();
    if (i < 0 || i >= ntags) return VSV_NONE;
    *x = tags[i].x; *z = tags[i].z;
    return tags[i].sv;
}

const char *village_name(void)
{
    if (DSB(DS_menu_preview)) return NULL;
    prepare();
    return placed_name;
}

void village_build_begin(void)
{
    memset(emitted, 0, sizeof emitted);
    build_col = DSB(DS_cam_x + 1) >> 2;
    build_row = 15 - (DSB(DS_cam_z + 1) >> 2);
}

bool village_emitted(int i)
{
    return i >= 0 && i < nplaced && emitted[i];
}

void village_cell(u8 ch_x, u8 cz)
{
    int n = village_count();
    for (int i = 0; i < n; i++) {
        const VillageHouse *h = &placed[i];
        if ((u8)((h->px >> 8) & 0x7C) != ch_x || (u8)((h->pz >> 8) & 0x3C) != cz) continue;
        int col = (h->px >> 10) & 31, row = 15 - ((h->pz >> 10) & 15);
        if (col != build_col || row != build_row) continue;               /* the far ring's */
        emitted[i] = true;
        u16 es, a;
        int nf, nv;
        if (!model_at(h->model, h->parked, &es, &a, &nf, &nv)) continue;
        DSW(DS_place_x) = h->px;
        DSW(DS_place_z) = h->pz;
        DSW(DS_place_y) = h->py;
        DSB(DS_place_rot) = h->rot;
        DSB(DS_place_shear) = 0;
        DSW(DS_cur_obj_slot1) = 0;                                   /* no object slot */
        DSW(DS_model_face_cut) = 0;
        DSB(DS_vert_overflow) = 0;
        DSB(DS_place_nverts) = (u8)nv;
        u16 f = model_emit_vertices(far_make(es, a), (u16)nv);
        if (DSB(DS_vert_overflow) == 0) model_emit_faces(far_make(es, f), (u16)nf);
    }
}
