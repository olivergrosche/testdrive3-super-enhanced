/* OG: villages (TOWNS.md). The houses of a leg are found in its objects (the game's own models, as enh_far.c reads
 * them), grouped into villages with names, and some of them get a service: a garage, a gun shop, a pub, a police
 * station, with a sign over the door on the road's side and a marker on the ground before it. Walked into, the
 * marker opens the shop's menu; the garage's bay, driven into with a wanted level, resprays the car. Money: found,
 * earned, paid. Read-only on the game's data (but the developer aid TD3_TOWN_GOTO). */
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
#include "game/village.h"

#define RAD2PX (1024.0 / M_PI)
#define A2RAD  (2.0 * M_PI / 65536.0)

#define NBLD 96
#define NPLAN_OLD 24                    /* a hand-planned village's houses at most (village.c) */
#define NVIL 32
#define START_MONEY 150
#define LEG_PRIZE   300
#define TICKET_FINE 50
#define HOSPITAL    100
#define FINE_STAR   150
#define SPRAY_STAR  100
#define DOOR_OUT 140.0                  /* the door's marker: out from the wall (x4 units) */
#define DOOR_R    90.0                  /* walked into within this */
#define BAY_OUT  380.0                  /* the garage's bay: out from the wall */
#define BAY_R    250.0
#define LINK     4500.0                 /* houses this near are one village */

enum { SV_NONE, SV_GARAGE, SV_GUNS, SV_PUB, SV_POLICE, SV_N };
static const char *const sv_name[SV_N] = { "", "GARAGE", "GUNS", "PUB", "POLICE" };
static const u32 sv_rgb[SV_N] = { 0, 0x38C8E8, 0xFF5038, 0xFFB030, 0x6088FF };

typedef struct {
    double X, Z, y, top;                /* the footprint's middle (true x4, unwrapped), its base, its height */
    double ax, az, hx, hz;              /* the local x axis in the world (unit), half the extent along x and z */
    double nx, nz, half;                /* the door's side: outward, half its length */
    double dX, dZ, gX, gZ;              /* the door's marker, the garage's bay (true x4) */
    double road;                        /* from the door to the main road on foot (walk grid), 1e9 no way */
    double road_air;                    /* from the door to the nearest road face, straight (the dump's) */
    int rcell;                          /* the main road's cell the way on foot starts at, -1 none */
    int vil, sv, obj;
    double cover, onroad;               /* the roof's share of the footprint, the road's share under it (the dump's) */
    double gy;                          /* the ground at the door (the base until seen) */
    bool gy_ok, armed, armed_car;       /* walked / driven away since the last visit */
} Bld;
typedef struct { double X, Z; int nb, nsv; const char *name; } Vil;

static Bld bld[NBLD];
static int nbld;
static int n_onroad;                    /* the dump's: models over the road, no houses (add_house) */
static Vil vil[NVIL];
static int nvil;
static u32 scan_key = 0xFFFFFFFFu;

/* the villages' names: the west (Pacific - Yosemite), New England (Cape Cod - Niagara) */
static const char *const names_ne[] = {
    "SALT MARSH", "CRANBERRY HOLLOW", "LOBSTER BAY", "MAPLE CORNERS", "NORTH HARBOR", "WHALERS REST", "BIRCH HILL",
    "STONEWALL", "HUNTERS BROOK", "GREEN HARBOR", "FOXFIELD", "CEDAR NECK", "OYSTER POND", "BEACON POINT", "ELMWOOD",
    "AMBER FALLS", "GREYSTONE", "OLD MILL", "HAWTHORNE", "SAWYERS CROSSING", "QUAKER HILL", "IRON BRIDGE",
    "SCHOONER BAY", "WILLOWBROOK", "BRANT LEDGE", "FIDDLERS GREEN", "COVERED BRIDGE", "HOPEWELL", "TANNERS FORD",
    "MOOSE MEADOW",
};
static const char *const names[] = {
    "PINE HOLLOW", "CEDAR FLATS", "RED MESA", "MILL CREEK", "SILVER GULCH", "OAK HOLLOW", "SANTA LUZ", "DRY WELLS",
    "EAGLE FORK", "COPPER SPRINGS", "WILLOW BEND", "DUSTY CORNERS", "LOST CANYON", "SAGE JUNCTION", "TALL TIMBER",
    "COYOTE FLATS", "QUAIL VALLEY", "HARMONY", "BUZZARD GAP", "SUNDOWN", "GOLD HOLLOW", "MESA LINDA", "LAST CHANCE",
    "PALO SECO", "FOGHORN COVE", "RATTLESNAKE BEND", "BLUE JAY", "LITTLE SHASTA", "PIGEON POINT", "HOLLISTER RIDGE",
};
#define NNAMES ((int)(sizeof names / sizeof names[0]))

/* money (the race's), its lines under it */
static int money = START_MONEY;
static struct { char t[40]; int d; double age; } mline[3];
static double money_show;               /* the money stays on the car's screen this long */
static bool car_armour;
static u32 races_seen, legs_seen, deaths_seen;
static int tickets_seen;
static bool counters_ok;

/* the player this frame: wrapped x4 (the snapshot's) and true x4 (the game's position) */
static double px, pz, py, TX, TZ;
static double beer, beer_t;             /* the pub: how tipsy, the sway's clock */
static double spray_t;                  /* the garage's spray over the screen */
static struct { char t[48]; u32 rgb; double t_left; } cmsg;
static int last_vil = -1;
static double banner_t;
static int banner_vil;
static bool goto_done;

/* a shop's menu */
enum { IT_LEAVE, IT_INFO, IT_AMMO, IT_WEAPON, IT_ARMOUR, IT_FOOD, IT_FINE, IT_CAR_ARMOUR, IT_JOB };
typedef struct { char label[32]; char right[16]; int kind, arg, arg2, price; char hint[44]; } Item;
static struct {
    bool open;
    int b, sel, n;
    Item it[8];
    char note[40];
    u32 note_rgb;
    double note_t, hp;
} menu;

static inline double wraps(double a) { a = fmod(a, 65536.0); if (a < -32768) a += 65536; if (a >= 32768) a -= 65536; return a; }

static double env_num(const char *name, double def)
{
    const char *e = SDL_getenv(name);
    return e ? SDL_atof(e) : def;
}

/* ------------------------------------------------------------------------------------------------------
 * Money
 * ------------------------------------------------------------------------------------------------------ */

static void money_line(int d, const char *why)
{
    memmove(&mline[1], &mline[0], sizeof mline[0] * 2);
    snprintf(mline[0].t, sizeof mline[0].t, "%s$%d%s%s", d < 0 ? "-" : "+", d < 0 ? -d : d, why ? " " : "", why ? why : "");
    mline[0].d = d;
    mline[0].age = 0;
    money_show = 5;
}

int enh_money(void) { return money; }

void enh_money_add(int d, const char *why)
{
    if (d <= 0) return;
    money += d;
    if (money > 999999) money = 999999;
    money_line(d, why);
    pcm_sfx_play(PCM_CASH, 0.55, 0);
}

bool enh_money_pay(int d, const char *why)
{
    if (d > money) return false;
    money -= d;
    money_line(-d, why);
    pcm_sfx_play(PCM_CASH, 0.8, 0);
    return true;
}

/* what the player has of it, at most (a fine, a fee) */
static void charge(int d, const char *why)
{
    if (d > money) d = money;
    if (d <= 0) return;
    money -= d;
    money_line(-d, why);
}

static void centre_msg(const char *t, u32 rgb, double secs)
{
    snprintf(cmsg.t, sizeof cmsg.t, "%s", t);
    cmsg.rgb = rgb;
    cmsg.t_left = secs;
}

bool enh_town_car_armour(void) { return car_armour; }

double enh_town_sway(void)
{
    if (beer <= 0.01) return 0;
    double k = fmin(beer, 3.0);
    return k * (650 * sin(beer_t * 1.1) + 280 * sin(beer_t * 2.7 + 1.0));
}

/* ------------------------------------------------------------------------------------------------------
 * The leg's houses
 * ------------------------------------------------------------------------------------------------------ */

/* a tile model's header (nf, nv); its vertex arrays A (height), B (x), C (z) at si + 4, the faces after them */
static bool tile_model(u8 al, u16 *es_out, u16 *si_out, int *nf_out, int *nv_out)
{
    u16 es, set, bx, cut = 0;
    if (al >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); al = (u8)(al - 0x40); }
    else            { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
    if (es == 0) return false;
    bx = (u16)(set + 2 * al);
    u16 e = rd16(es, bx);
    if (e <= 0x10) { cut = e; bx = (u16)(bx - 2); }
    u16 si = (u16)(rd16(es, bx) + set);
    int nf = rd8(es, si), nv = rd8(es, (u16)(si + 1));
    *es_out = es; *si_out = si; *nf_out = nf - cut; *nv_out = nv;
    return nf - cut > 0 && nv > 0;
}

/* the distance (x4 units) from (X, Z) (true) to the nearest road face (types 10h .. 14h) of the cells around it */
static double road_dist(double X, double Z)
{
    double best = 1e9;
    int col0 = (int)floor(X / 4096.0), zc0 = (int)floor(Z / 4096.0);
    for (int dc = -1; dc <= 1; dc++)
        for (int dz = -1; dz <= 1; dz++) {
            int col = col0 + dc, zc = zc0 + dz, row = 15 - zc;
            if (col < 0 || col > 31 || row < 0 || row > 15) continue;
            u16 m = DSW(DS_leg_map + 2 * (col + row * 32));
            u16 es, si;
            int nf, nv;
            if (!tile_model((u8)m, &es, &si, &nf, &nv)) continue;
            u8 rot = (u8)((m >> 8) & 0xC0);
            double ox = col * 4096.0 + 2048.0, oz = zc * 4096.0 + 2048.0;
            u16 stride = (u16)(nv * 2), f = (u16)(si + 4 + 3 * stride);
            for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
                u16 w[4];
                for (int j = 0; j < 4; j++) w[j] = rd16(es, (u16)(f + 2 * j));
                int t = w[3] >> 11, n = (w[0] >> 14) + 1;
                if (t < 0x10 || t > 0x14 || n < 3) continue;
                double vx[4], vz[4];
                bool ok = true;
                for (int j = 0; j < n; j++) {
                    int vi = w[j] & 0x7FF;
                    if (vi >= nv) { ok = false; break; }
                    double b = (s16)rd16(es, (u16)(si + 4 + stride + 2 * vi)), c = (s16)rd16(es, (u16)(si + 4 + 2 * stride + 2 * vi));
                    double rb = b, rc = c;
                    if (rot == 0x40)      { rb = c; rc = -b; }
                    else if (rot == 0x80) { rb = -b; rc = -c; }
                    else if (rot == 0xC0) { rb = -c; rc = b; }
                    vx[j] = ox + rb; vz[j] = oz + rc;
                }
                if (!ok) continue;
                /* inside (either winding), else the nearest edge */
                int pos = 0, neg = 0;
                double d = 1e9;
                for (int j = 0; j < n; j++) {
                    int j2 = (j + 1) % n;
                    double ex = vx[j2] - vx[j], ez = vz[j2] - vz[j], qx = X - vx[j], qz = Z - vz[j];
                    double cr = ex * qz - ez * qx;
                    if (cr > 0) pos++; else if (cr < 0) neg++;
                    double l2 = ex * ex + ez * ez, u = l2 > 0 ? (qx * ex + qz * ez) / l2 : 0;
                    u = u < 0 ? 0 : u > 1 ? 1 : u;
                    double rx = qx - ex * u, rz = qz - ez * u;
                    d = fmin(d, sqrt(rx * rx + rz * rz));
                }
                if (pos == 0 || neg == 0) d = 0;
                best = fmin(best, d);
            }
        }
    return best;
}

/* The leg's roads as a network of map cells (TOWNS.md "Jobs": a job's time comes from the way by road): a cell is a
 * node when its tile has road faces; two cells next to each other are joined when road faces of both reach their
 * common edge at places within 6 m of each other. Built at the leg's scan. */
static bool rnode[512];
static double redge[512][4][2];         /* per cell and side (0 +x, 1 -x, 2 +z, 3 -z): where along it road touches it */
static int finish_cell = -1;            /* the leg's finish: the cell whose tile has the gas station (faces 1Fh) */

static void road_net(void)
{
    finish_cell = -1;
    for (int c = 0; c < 512; c++) {
        rnode[c] = false;
        for (int e = 0; e < 4; e++) { redge[c][e][0] = 1e9; redge[c][e][1] = -1e9; }
        int col = c & 31, row = c >> 5, zc = 15 - row;
        u16 m = DSW(DS_leg_map + 2 * c);
        u16 es, si;
        int nf, nv;
        if (!tile_model((u8)m, &es, &si, &nf, &nv)) continue;
        u8 rot = (u8)((m >> 8) & 0xC0);
        double ox = col * 4096.0 + 2048.0, oz = zc * 4096.0 + 2048.0;
        u16 stride = (u16)(nv * 2), f = (u16)(si + 4 + 3 * stride);
        for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
            u16 w[4];
            for (int j = 0; j < 4; j++) w[j] = rd16(es, (u16)(f + 2 * j));
            int t = w[3] >> 11, n = (w[0] >> 14) + 1;
            if (t == 0x1F) finish_cell = c;
            if (t < 0x10 || t > 0x14 || n < 3) continue;
            rnode[c] = true;
            for (int j = 0; j < n; j++) {
                int vi = w[j] & 0x7FF;
                if (vi >= nv) continue;
                double b = (s16)rd16(es, (u16)(si + 4 + stride + 2 * vi)), cc = (s16)rd16(es, (u16)(si + 4 + 2 * stride + 2 * vi));
                double rb = b, rc = cc;
                if (rot == 0x40)      { rb = cc; rc = -b; }
                else if (rot == 0x80) { rb = -b; rc = -cc; }
                else if (rot == 0xC0) { rb = -cc; rc = b; }
                double x = ox + rb, z = oz + rc;
                double lx = x - col * 4096.0, lz = z - zc * 4096.0;          /* within the cell, 0 .. 4096 */
                const double tol = 64;
                double along[4] = { lz, lz, lx, lx };
                bool on[4] = { lx > 4096 - tol, lx < tol, lz > 4096 - tol, lz < tol };
                for (int e = 0; e < 4; e++)
                    if (on[e]) { redge[c][e][0] = fmin(redge[c][e][0], along[e]); redge[c][e][1] = fmax(redge[c][e][1], along[e]); }
            }
        }
    }
}

/* the cell next to c on side e (0 +x, 1 -x, 2 +z, 3 -z), -1 off the map */
static int cell_next(int c, int e)
{
    int col = c & 31, row = c >> 5;
    if (e == 0) col++; else if (e == 1) col--; else if (e == 2) row--; else row++;
    if (col < 0 || col > 31 || row < 0 || row > 15) return -1;
    return col + row * 32;
}

/* the road cell nearest (X, Z) within two cells, -1 none */
static int road_cell_near(double X, double Z)
{
    int col0 = (int)floor(X / 4096.0), zc0 = (int)floor(Z / 4096.0), best = -1;
    double bd = 1e18;
    for (int dc = -2; dc <= 2; dc++)
        for (int dz = -2; dz <= 2; dz++) {
            int col = col0 + dc, zc = zc0 + dz, row = 15 - zc;
            if (col < 0 || col > 31 || row < 0 || row > 15) continue;
            int c = col + row * 32;
            if (!rnode[c]) continue;
            double d = hypot(col * 4096.0 + 2048.0 - X, zc * 4096.0 + 2048.0 - Z);
            if (d < bd) { bd = d; best = c; }
        }
    return best;
}

/* road cell c joined to the cell next to it on side e (-1: none) */
static int road_joined(int c, int e)
{
    int d = cell_next(c, e);
    if (d < 0 || !rnode[d]) return -1;
    int back = e ^ 1;                                                    /* the same edge seen from d */
    if (redge[c][e][0] > redge[c][e][1] || redge[d][back][0] > redge[d][back][1]) return -1;
    if (redge[c][e][0] > redge[d][back][1] + 600 || redge[d][back][0] > redge[c][e][1] + 600) return -1;
    return d;
}

/* the way by road from road cell a to road cell b, metres (cell to cell, 41 m a step); -1 no way */
static double route_cells(int a, int b)
{
    if (a < 0 || b < 0) return -1;
    static int dist[512], queue[512];
    for (int i = 0; i < 512; i++) dist[i] = -1;
    int qh = 0, qt = 0;
    dist[a] = 0; queue[qt++] = a;
    while (qh < qt) {
        int c = queue[qh++];
        if (c == b) break;
        for (int e = 0; e < 4; e++) {
            int d = road_joined(c, e);
            if (d < 0 || dist[d] >= 0) continue;
            dist[d] = dist[c] + 1;
            queue[qt++] = d;
        }
    }
    if (dist[b] < 0) return -1;
    return dist[b] * 40.96 + 20;
}

/* The road network's parts (cells joined by road_joined); the main road = the biggest part (the leg's course) */
static int rcomp[512], rcomp_n, main_comp = -1, main_cells;

static void road_comps(void)
{
    static int size[512], queue[512];
    rcomp_n = 0; main_comp = -1; main_cells = 0;
    for (int c = 0; c < 512; c++) rcomp[c] = -1;
    for (int c0 = 0; c0 < 512; c0++) {
        if (!rnode[c0] || rcomp[c0] >= 0) continue;
        int k = rcomp_n++, qh = 0, qt = 0;
        size[k] = 0;
        rcomp[c0] = k; queue[qt++] = c0;
        while (qh < qt) {
            int c = queue[qh++];
            size[k]++;
            for (int e = 0; e < 4; e++) {
                int d = road_joined(c, e);
                if (d < 0 || rcomp[d] >= 0) continue;
                rcomp[d] = k;
                queue[qt++] = d;
            }
        }
        if (size[k] > main_cells) { main_cells = size[k]; main_comp = k; }
    }
}

/* ------------------------------------------------------------------------------------------------------
 * On foot from the main road (TOWNS.md "Jobs": every goal can be reached). The leg's tiles sampled every WG
 * units (1.28 m): at each point the highest ground face (not steep, not a zone), water (0Eh) and holes not to
 * walk on; the walls (steep faces, type-1 lines, not the zones) block the points whose ground they meet at a
 * body's height, as foot.c's resolve. From the main road's points the way on foot (8 neighbours, no corner cut,
 * at most WG_DY up or down a step) to every point within WALK_MAX: its length and the road cell it starts at.
 * A house's door counts only with such a way: no goal across a river, up a cliff or by a road of its own.
 * ------------------------------------------------------------------------------------------------------ */
#define WG        128.0
#define WGX       1024                  /* 131072 / WG */
#define WGZ       512                   /* 65536 / WG */
#define WG_DY     160.0                 /* height units between neighbouring points (a slope's, a step's) */
#define WG_STEP    18.0                 /* a wall lower than this over the ground is stepped onto (foot.c F_STEP) */
#define WG_HEAD    78.0                 /* ... higher than this is over the head (foot.c F_HEIGHT) */
#define WALK_MAX 7000.0                 /* x4 units: the search's reach (70 m) */
static float wg_y[WGZ][WGX];            /* the ground, height units; -1e9 none */
static u8 wg_t[WGZ][WGX];               /* its face type */
static u8 wg_wall[WGZ][WGX];
static float wg_d[WGZ][WGX];            /* the way on foot from the main road, x4 units; 1e9 none */
static double walk_ms;                  /* the last walk_grid's time (the dump's) */
static short wg_src[WGZ][WGX];          /* the road cell it starts at */

/* face k of map cell c's tile: its corners (true x4; height units), type and corner count; false none */
typedef struct { u16 es, si; int nf, nv; u8 rot; double ox, oz, oy; } TileRef;

static bool tile_ref(int c, TileRef *r)
{
    int col = c & 31, row = c >> 5, zc = 15 - row;
    u16 m = DSW(DS_leg_map + 2 * c);
    if (!tile_model((u8)m, &r->es, &r->si, &r->nf, &r->nv)) return false;
    r->rot = (u8)((m >> 8) & 0xC0);
    r->ox = col * 4096.0 + 2048.0; r->oz = zc * 4096.0 + 2048.0; r->oy = (m & 0x3F00);
    return true;
}

static bool tile_face(const TileRef *r, int k, double *vx, double *vy, double *vz, int *type, int *n)
{
    u16 stride = (u16)(r->nv * 2), f = (u16)(r->si + 4 + 3 * stride + 8 * k);
    u16 w[4];
    for (int j = 0; j < 4; j++) w[j] = rd16(r->es, (u16)(f + 2 * j));
    *type = w[3] >> 11; *n = (w[0] >> 14) + 1;
    for (int j = 0; j < *n; j++) {
        int vi = w[j] & 0x7FF;
        if (vi >= r->nv) return false;
        double a = (s16)rd16(r->es, (u16)(r->si + 4 + 2 * vi)), b = (s16)rd16(r->es, (u16)(r->si + 4 + stride + 2 * vi)),
               cc = (s16)rd16(r->es, (u16)(r->si + 4 + 2 * stride + 2 * vi)), rb = b, rc = cc;
        if (r->rot == 0x40)      { rb = cc; rc = -b; }
        else if (r->rot == 0x80) { rb = -b; rc = -cc; }
        else if (r->rot == 0xC0) { rb = -cc; rc = b; }
        vx[j] = r->ox + rb; vz[j] = r->oz + rc; vy[j] = r->oy + a;
    }
    return true;
}

/* steep as foot.c's (the normal's horizontal part over 1.5 times its vertical one) */
static bool face_steep(const double *vx, const double *vy, const double *vz)
{
    double ux = vx[1] - vx[0], uy = vy[1] - vy[0], uz = vz[1] - vz[0];
    double wx = vx[2] - vx[0], wy = vy[2] - vy[0], wz = vz[2] - vz[0];
    double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    double h = sqrt(nx * nx + nz * nz);
    return h > 1.5 * fabs(ny) && h > 1e-6;
}

static inline void wg_mark_wall(int gx, int gz, double ylo, double yhi)
{
    if (gx < 0 || gx >= WGX || gz < 0 || gz >= WGZ) return;
    double g = wg_y[gz][gx];
    if (g < -1e8) return;
    if (yhi >= g + WG_STEP && ylo <= g + WG_HEAD) wg_wall[gz][gx] = 1;
}

static inline bool wg_walkable(int gx, int gz)
{
    return gx >= 0 && gx < WGX && gz >= 0 && gz < WGZ && wg_y[gz][gx] > -1e8f && wg_t[gz][gx] != 0x0E && !wg_wall[gz][gx];
}

/* the search's heap: (way, point), entries made stale by a shorter way are skipped when taken */
typedef struct { float d; int i; } WgEntry;
#define WG_HEAP (1 << 20)
static WgEntry wg_heap[WG_HEAP];
static int wg_heap_n;
static bool wg_heap_full;

static void wg_push(float d, int i)
{
    if (wg_heap_n >= WG_HEAP) { wg_heap_full = true; return; }
    int k = wg_heap_n++;
    while (k > 0 && wg_heap[(k - 1) / 2].d > d) { wg_heap[k] = wg_heap[(k - 1) / 2]; k = (k - 1) / 2; }
    wg_heap[k].d = d; wg_heap[k].i = i;
}

static WgEntry wg_pop(void)
{
    WgEntry top = wg_heap[0], last = wg_heap[--wg_heap_n];
    int k = 0;
    for (;;) {
        int l = 2 * k + 1, m = l;
        if (l >= wg_heap_n) break;
        if (l + 1 < wg_heap_n && wg_heap[l + 1].d < wg_heap[l].d) m = l + 1;
        if (wg_heap[m].d >= last.d) break;
        wg_heap[k] = wg_heap[m];
        k = m;
    }
    if (wg_heap_n > 0) wg_heap[k] = last;
    return top;
}

/* a wall's inside (a slanting cliff): the grid points under the polygon (x, z) */
static void wg_raster(const double *vx, const double *vz, int n, double ylo, double yhi)
{
    double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
    for (int j = 0; j < n; j++) { x0 = fmin(x0, vx[j]); x1 = fmax(x1, vx[j]); z0 = fmin(z0, vz[j]); z1 = fmax(z1, vz[j]); }
    int gx0 = (int)ceil(x0 / WG - 0.5), gx1 = (int)floor(x1 / WG - 0.5), gz0 = (int)ceil(z0 / WG - 0.5), gz1 = (int)floor(z1 / WG - 0.5);
    if (gx0 < 0) gx0 = 0;
    if (gz0 < 0) gz0 = 0;
    if (gx1 >= WGX) gx1 = WGX - 1;
    if (gz1 >= WGZ) gz1 = WGZ - 1;
    for (int gz = gz0; gz <= gz1; gz++)
        for (int gx = gx0; gx <= gx1; gx++) {
            double X = (gx + 0.5) * WG, Z = (gz + 0.5) * WG;
            for (int t = 0; t + 2 < n; t++) {                            /* the fan's triangles */
                int i0 = 0, i1 = 1 + t, i2 = 2 + t;
                double d = (vz[i1] - vz[i2]) * (vx[i0] - vx[i2]) + (vx[i2] - vx[i1]) * (vz[i0] - vz[i2]);
                if (fabs(d) < 1e-9) continue;
                double l0 = ((vz[i1] - vz[i2]) * (X - vx[i2]) + (vx[i2] - vx[i1]) * (Z - vz[i2])) / d;
                double l1 = ((vz[i2] - vz[i0]) * (X - vx[i2]) + (vx[i0] - vx[i2]) * (Z - vz[i2])) / d, l2 = 1 - l0 - l1;
                if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6) continue;
                wg_mark_wall(gx, gz, ylo, yhi);
                break;
            }
        }
}

static void walk_grid(void)
{
    for (int gz = 0; gz < WGZ; gz++)
        for (int gx = 0; gx < WGX; gx++) { wg_y[gz][gx] = -1e9f; wg_t[gz][gx] = 0xFF; wg_wall[gz][gx] = 0; wg_d[gz][gx] = 1e9f; wg_src[gz][gx] = -1; }
    /* the ground: the highest face at each point, its type */
    for (int c = 0; c < 512; c++) {
        TileRef r;
        if (!tile_ref(c, &r)) continue;
        for (int k = 0; k < r.nf; k++) {
            double vx[4], vy[4], vz[4];
            int t, n;
            if (!tile_face(&r, k, vx, vy, vz, &t, &n) || n < 3 || t == 0 || (t >= 0x1C && t <= 0x1F)) continue;
            if (face_steep(vx, vy, vz)) continue;
            double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
            for (int j = 0; j < n; j++) { x0 = fmin(x0, vx[j]); x1 = fmax(x1, vx[j]); z0 = fmin(z0, vz[j]); z1 = fmax(z1, vz[j]); }
            int gx0 = (int)fmax(0, ceil(x0 / WG - 0.5)), gx1 = (int)fmin(WGX - 1, floor(x1 / WG - 0.5));
            int gz0 = (int)fmax(0, ceil(z0 / WG - 0.5)), gz1 = (int)fmin(WGZ - 1, floor(z1 / WG - 0.5));
            for (int gz = gz0; gz <= gz1; gz++)
                for (int gx = gx0; gx <= gx1; gx++) {
                    double X = (gx + 0.5) * WG, Z = (gz + 0.5) * WG;
                    for (int tt = 0; tt + 2 < n; tt++) {
                        int i0 = 0, i1 = 1 + tt, i2 = 2 + tt;
                        double d = (vz[i1] - vz[i2]) * (vx[i0] - vx[i2]) + (vx[i2] - vx[i1]) * (vz[i0] - vz[i2]);
                        if (fabs(d) < 1e-9) continue;
                        double l0 = ((vz[i1] - vz[i2]) * (X - vx[i2]) + (vx[i2] - vx[i1]) * (Z - vz[i2])) / d;
                        double l1 = ((vz[i2] - vz[i0]) * (X - vx[i2]) + (vx[i0] - vx[i2]) * (Z - vz[i2])) / d, l2 = 1 - l0 - l1;
                        if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6) continue;
                        double y = l0 * vy[i0] + l1 * vy[i1] + l2 * vy[i2];
                        if (y > wg_y[gz][gx]) { wg_y[gz][gx] = (float)y; wg_t[gz][gx] = (u8)t; }
                        break;
                    }
                }
        }
    }
    /* the walls: their outline every half point, their inside (a slanting cliff) */
    for (int c = 0; c < 512; c++) {
        TileRef r;
        if (!tile_ref(c, &r)) continue;
        for (int k = 0; k < r.nf; k++) {
            double vx[4], vy[4], vz[4];
            int t, n;
            if (!tile_face(&r, k, vx, vy, vz, &t, &n) || t == 0 || (t >= 0x1C && t <= 0x1F)) continue;
            if (n == 2 ? t != 1 : n < 3 || !face_steep(vx, vy, vz)) continue;
            double ylo = 1e18, yhi = -1e18;
            for (int j = 0; j < n; j++) { ylo = fmin(ylo, vy[j]); yhi = fmax(yhi, vy[j]); }
            for (int j = 0; j < n; j++) {
                int j2 = (j + 1) % n;
                if (n == 2 && j == 1) break;
                double ex = vx[j2] - vx[j], ez = vz[j2] - vz[j], len = sqrt(ex * ex + ez * ez);
                int steps = (int)(len / (WG / 2)) + 1;
                for (int s = 0; s <= steps; s++) {
                    double u = (double)s / steps;
                    wg_mark_wall((int)floor((vx[j] + ex * u) / WG), (int)floor((vz[j] + ez * u) / WG), ylo, yhi);
                }
            }
            if (n >= 3) wg_raster(vx, vz, n, ylo, yhi);
        }
    }
    /* the way on foot from the main road's points: Dijkstra over 8 neighbours, a heap with stale entries skipped */
    wg_heap_n = 0;
    if (main_comp < 0) return;
    for (int gz = 0; gz < WGZ; gz++)
        for (int gx = 0; gx < WGX; gx++) {
            u8 t = wg_t[gz][gx];
            if (t < 0x10 || t > 0x14 || wg_wall[gz][gx]) continue;
            int col = (int)((gx + 0.5) * WG / 4096.0), zc = (int)((gz + 0.5) * WG / 4096.0), c = col + (15 - zc) * 32;
            if (col > 31 || zc > 15 || rcomp[c] != main_comp) continue;
            wg_d[gz][gx] = 0;
            wg_src[gz][gx] = (short)c;
            wg_push(0, gz * WGX + gx);
        }
    static const int dxs[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dzs[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    while (wg_heap_n > 0) {
        WgEntry top = wg_pop();
        int gx = top.i % WGX, gz = top.i / WGX;
        if (top.d > wg_d[gz][gx]) continue;                              /* stale */
        float y0 = wg_y[gz][gx];
        for (int e = 0; e < 8; e++) {
            int nx = gx + dxs[e], nz = gz + dzs[e];
            if (!wg_walkable(nx, nz)) continue;
            bool diag = e >= 4;
            if (diag && (!wg_walkable(nx, gz) || !wg_walkable(gx, nz))) continue;   /* no corner cut */
            if (fabs(wg_y[nz][nx] - y0) > WG_DY * (diag ? 1.41 : 1.0)) continue;
            float nd = top.d + (float)(WG * (diag ? 1.41421356 : 1.0));
            if (nd >= wg_d[nz][nx] || nd > WALK_MAX) continue;
            wg_d[nz][nx] = nd;
            wg_src[nz][nx] = wg_src[gz][gx];
            wg_push(nd, nz * WGX + nx);
        }
    }
}

/* the way on foot from the main road to (X, Z) (true x4; the best point within 1.3 m), 1e9 none; its road cell */
static double walk_at(double X, double Z, int *cell)
{
    int gx = (int)floor(X / WG), gz = (int)floor(Z / WG);
    double best = 1e9;
    if (cell) *cell = -1;
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++) {
            int x = gx + a, z = gz + b;
            if (x < 0 || x >= WGX || z < 0 || z >= WGZ) continue;
            double d = wg_d[z][x] + ((a || b) ? WG : 0);
            if (wg_d[z][x] < 1e8f && d < best) { best = d; if (cell) *cell = wg_src[z][x]; }
        }
    return best;
}

static bool inside(const Bld *b, double X, double Z, double grow);
bool enh_town_house_at(double X, double Z, double grow)
{
    for (int i = 0; i < nbld; i++) if (inside(&bld[i], X, Z, grow)) return true;
    return false;
}

/* a goal for a job: its door at most 40 m on foot from the main road (TOWNS.md "Jobs") */
static bool house_reachable(int i) { return i >= 0 && i < nbld && bld[i].road <= 4000 && bld[i].rcell >= 0; }

/* the way by road from road cell c to the leg's finish, metres; -1 none */
static double to_finish(int c)
{
    int f = finish_cell >= 0 && !rnode[finish_cell] ? road_cell_near((finish_cell & 31) * 4096.0 + 2048.0,
                                                                     (15 - (finish_cell >> 5)) * 4096.0 + 2048.0) : finish_cell;
    return route_cells(c, f);
}

/* the point (X, Z) inside a house's footprint (grown by `grow`)? */
static bool inside(const Bld *b, double X, double Z, double grow)
{
    double qx = X - b->X, qz = Z - b->Z;
    double lx = qx * b->ax + qz * b->az, lz = qx * -b->az + qz * b->ax;   /* local z = (-az, ax)... see scan */
    return fabs(lx) < b->hx + grow && fabs(lz) < b->hz + grow;
}

static u32 leg_key(void)
{
    u32 h = 2166136261u;
    for (int i = 0; i < 512; i++) { h ^= DSW(DS_leg_map + 2 * i); h *= 16777619u; }
    h ^= (u32)DSW(DS_obj_count) << 8 ^ (u32)DSW(DS_obj_first_static) << 16 ^ (u32)DSW(DS_scene_index) << 24 ^ DSB(DS_leg_index);
    return h;
}

/* a house, if the model is one: the object model m (stat: the statics' format, else the parked objects'), its origin
 * (x4 units, height units), its turn; obj: its object slot (-1: a planned village's), sv: a service planned there */
static void add_house(int m, bool stat, double ox, double oz, double oy, u8 rot, int obj, int sv)
{
    u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
    if (!es || nbld >= NBLD) return;
    u16 p = (u16)(rd16(es, (u16)(set + 2 * m)) + set);
    int nf = rd8(es, p), nv = rd8(es, (u16)(p + 1));
    if (nf <= 0 || nv <= 0) return;
    u16 a = (u16)(p + (stat ? 4 : 8)), stride = (u16)(nv * 2);
    /* the solid faces (type 1: walls and roofs) make the house; a driveway or a yard is not it */
    bool used[256];
    memset(used, 0, sizeof used);
    int solid = 0;
    double flat = 0;                                                     /* the solid faces' area seen from above */
    u16 fa = (u16)(a + 3 * stride);
    for (int k = 0; k < nf; k++, fa = (u16)(fa + 8)) {
        u16 w0 = rd16(es, fa);
        if ((rd16(es, (u16)(fa + 6)) >> 11) != 1) continue;
        solid++;
        int nvf = (w0 >> 14) + 1;
        double px[4], pz[4];
        bool ok = true;
        for (int j = 0; j < nvf; j++) {
            int vi = rd16(es, (u16)(fa + 2 * j)) & 0x7FF;
            if (vi < nv && vi < 256) {
                used[vi] = true;
                px[j] = (s16)rd16(es, (u16)(a + stride + 2 * vi));
                pz[j] = (s16)rd16(es, (u16)(a + 2 * stride + 2 * vi));
            } else ok = false;
        }
        if (ok && nvf >= 3) {
            double ar = 0;
            for (int j = 0; j < nvf; j++) ar += px[j] * pz[(j + 1) % nvf] - px[(j + 1) % nvf] * pz[j];
            flat += fabs(ar) / 2;
        }
    }
    if (solid < 4) return;
    double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9, h0 = 1e9, h1 = -1e9;
    for (int k = 0; k < nv && k < 256; k++) {
        if (!used[k]) continue;
        double h = (s16)rd16(es, (u16)(a + 2 * k)), x = (s16)rd16(es, (u16)(a + stride + 2 * k)),
               z = (s16)rd16(es, (u16)(a + 2 * stride + 2 * k));
        x0 = fmin(x0, x); x1 = fmax(x1, x); z0 = fmin(z0, z); z1 = fmax(z1, z); h0 = fmin(h0, h); h1 = fmax(h1, h);
    }
    double w = x1 - x0, d = z1 - z0, ht = h1 - h0;
    if (fmin(w, d) < 180 || w * d < 60000 || ht < 90 || fmax(w, d) > 2500) return;
    /* a tower, not a house: higher than 2.5 times its narrow side (a height unit is 2.33 x4 units): the power
     * line's pylons on Cape Cod's fourth leg (model 27, 2.6 m wide, 12 m high) were a "village" with shops */
    if (ht * 2.33 > 2.5 * fmin(w, d)) return;
    double phi = rot * (2.0 * M_PI / 256.0), cs = cos(phi), sn = sin(phi);
    double cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    Bld *b = &bld[nbld++];
    memset(b, 0, sizeof *b);
    /* vertex_rotate: x' = x cos + z sin, z' = -x sin + z cos */
    b->X = ox + cx * cs + cz * sn;
    b->Z = oz - cx * sn + cz * cs;
    b->ax = cs; b->az = -sn;                                             /* the local x axis; local z = (sn, cs) */
    b->hx = w / 2; b->hz = d / 2;
    b->y = oy + h0;
    b->top = ht;
    b->obj = obj;
    b->vil = -1;
    b->sv = sv;
    b->armed = b->armed_car = true;
    b->cover = flat / (w * d);
    int n = 0, r = 0;                                                    /* the walk grid's points under it */
    double ext = hypot(b->hx, b->hz);
    for (int gz = (int)floor((b->Z - ext) / WG); gz <= (int)floor((b->Z + ext) / WG); gz++)
        for (int gx = (int)floor((b->X - ext) / WG); gx <= (int)floor((b->X + ext) / WG); gx++) {
            if (gx < 0 || gx >= WGX || gz < 0 || gz >= WGZ || !inside(b, gx * (double)WG, gz * (double)WG, -32)) continue;
            n++;
            if (wg_t[gz][gx] >= 0x10 && wg_t[gz][gx] <= 0x14) r++;
        }
    b->onroad = n ? (double)r / n : 0;
    /* nothing over the road is a house: the traffic lights' gantry over the crossroads (model 5, 8.5 m square, 1.5
     * to 1.8 m "high" by its solid faces: the posts and the beam) stood on the road on eight of the ten legs and, by
     * the road, was a pub, a gun shop, a garage or a police station sixteen times (2026-11-07, Cape Cod - Niagara's
     * fifth leg: the garage's sign over the lights, its ring on the crossroads). The road under the houses: 0 on
     * every leg, under the gantry 0.90 to 0.98 */
    if (b->onroad > 0.25) { nbld--; n_onroad++; }
}

/* ------------------------------------------------------------------------------------------------------
 * More villages (TOWNS.md "More villages"): at a leg's start, houses of the leg's own models are planned along the
 * main road where it runs straight, east-west or north-south (the statics turn by quarter turns), on level open
 * ground on both sides, their fronts to the road, as PILGRIM COVE was planned by hand. game/village.c places them
 * (level ground only) and the game builds them as its own. At most three villages a leg, 300 m apart (a hamlet of
 * the leg's own may grow), the most houses first, then the nearer to the start.
 * ------------------------------------------------------------------------------------------------------ */
typedef struct { u8 model; bool parked; double fx, fz; } HouseKind;   /* its front: a unit vector in its own frame */
static const HouseKind kinds_ne[] = {                                /* Cape Cod - Niagara: the fronts as PILGRIM COVE's */
    { 55, true, 0, -1 }, { 54, false, 0, -1 }, { 50, true, 1, 0 }, { 54, false, 0, -1 }, { 55, true, 0, -1 }, { 51, false, 1, 0 },
};
static const HouseKind kinds_w[] = {                                 /* Pacific - Yosemite: as the leg's own face the road */
    { 52, true, 1, 0 }, { 53, true, 1, 0 }, { 54, false, 0, -1 }, { 52, true, 1, 0 }, { 51, true, 0, 1 }, { 53, true, 1, 0 },
};
#define PV_SETBACK  350.0               /* from the road's edge to the front */
#define PV_RUN     3200.0               /* a straight stretch at least this long (32 m) */
#define PV_SPAN    8000.0               /* a village at most this long along the road */
#define PV_MAXH       7
#define PV_CELLV    420                 /* added houses' vertices in one map cell (PILGRIM COVE's fullest: 446) */
#define PV_APART  30000.0               /* from the other planned villages (a leg's own hamlet may grow) */
#define PV_VILLAGES   3

/* what a planned house may not stand on: every object of the leg (a church, its tower, a pylon, a boat: not only
 * the houses), 1.5 m round it, and every sprite (the tiles' trees, rocks, signs; the leg's own): its trunk */
static u8 pv_blocked[WGZ][WGX];

static void pv_block_rect(double X, double Z, double ax, double az, double hx, double hz, double margin)
{
    double r = hypot(hx, hz) + margin;
    for (int gz = (int)floor((Z - r) / WG); gz <= (int)floor((Z + r) / WG); gz++)
        for (int gx = (int)floor((X - r) / WG); gx <= (int)floor((X + r) / WG); gx++) {
            if (gx < 0 || gx >= WGX || gz < 0 || gz >= WGZ) continue;
            double qx = (gx + 0.5) * WG - X, qz = (gz + 0.5) * WG - Z;
            double lx = qx * ax + qz * az, lz = -qx * az + qz * ax;
            if (fabs(lx) <= hx + margin && fabs(lz) <= hz + margin) pv_blocked[gz][gx] = 1;
        }
}

static void pv_obstacles(void)
{
    memset(pv_blocked, 0, sizeof pv_blocked);
    u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
    int first = DSW(DS_obj_first_static), n = DSW(DS_obj_count);
    for (int o = 1; o < n && o < 160 && es; o++) {
        u16 f = DSW(DS_obj_flags + 2 * o);
        bool stat = o >= first, parked = !stat && (f & 0x3000) == 0x1000;
        if (!stat && !parked) continue;
        int m = f & 0x3F;
        if (m <= 3) continue;
        u16 p = (u16)(rd16(es, (u16)(set + 2 * m)) + set);
        int nv = rd8(es, (u16)(p + 1));
        u16 a = (u16)(p + (stat ? 4 : 8)), stride = (u16)(nv * 2);
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (int k = 0; k < nv; k++) {
            double x = (s16)rd16(es, (u16)(a + stride + 2 * k)), z = (s16)rd16(es, (u16)(a + 2 * stride + 2 * k));
            x0 = fmin(x0, x); x1 = fmax(x1, x); z0 = fmin(z0, z); z1 = fmax(z1, z);
        }
        if (x0 > x1) continue;
        u8 rot = (u8)(DSW(DS_obj_heading + 2 * o) >> 8);
        if (stat) rot &= 0xC0;
        double phi = rot * (2.0 * M_PI / 256.0), cs = cos(phi), sn = sin(phi), cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
        double ox = DSW(DS_obj_x + 2 * o) * 4.0, oz = DSW(DS_obj_z + 2 * o) * 4.0;
        pv_block_rect(ox + cx * cs + cz * sn, oz - cx * sn + cz * cs, cs, -sn, (x1 - x0) / 2, (z1 - z0) / 2, 150);
    }
    /* the leg's own sprites (9 ..: trees, rocks, animals) */
    for (int i = 9; i < DSW(DS_sprite_count) && i < 320; i++)
        if (DSW(DS_sprite_id + 2 * i))
            pv_block_rect(DSW(DS_sprite_x + 2 * i) * 4.0, DSW(DS_sprite_z + 2 * i) * 4.0, 1, 0, 70, 70, 0);
    /* the tiles' sprite children, thinned by the vegetation seed as the game does (enh_far.c children) */
    for (int c = 0; c < 512; c++) {
        TileRef r;
        if (!tile_ref(c, &r)) continue;
        int nc = rd8(r.es, (u16)(r.si + 2));
        u16 q = (u16)(r.si + 4 + rd8(r.es, r.si) * 8 + rd8(r.es, (u16)(r.si + 1)) * 6);
        int col = c & 31, zc = 15 - (c >> 5);
        u16 px = (u16)((col * 4 + 2) << 8), pz = (u16)((zc * 4 + 2) << 8);
        u16 di = DSW(DS_veg_seed);
        u8 cnt = (u8)((u16)(px + pz) >> 12);
        if (cnt) di = (u16)((di << cnt) | (di >> (16 - cnt)));
        for (int k = 0; k < nc; k++, q = (u16)(q + 8)) {
            u16 id = rd16(r.es, q);
            if ((u8)id >= 0x12) {
                u16 carry = di & 1;
                di = (u16)((di >> 1) | (carry << 15));
                if (!carry) continue;
            }
            if (!id) continue;
            s16 x = (s16)rd16(r.es, (u16)(q + 2)), z = (s16)rd16(r.es, (u16)(q + 4)), t;
            if (r.rot == 0x40)      { t = x; x = z; z = (s16)-t; }
            else if (r.rot == 0x80) { x = (s16)-x; z = (s16)-z; }
            else if (r.rot == 0xC0) { t = x; x = (s16)-z; z = t; }
            pv_block_rect((px + x) * 4.0, (pz + z) * 4.0, 1, 0, 70, 70, 0);
        }
    }
}

static bool pv_main_road(int gx, int gz)
{
    return gx >= 0 && gx < WGX && gz >= 0 && gz < WGZ && wg_d[gz][gx] == 0 && wg_t[gz][gx] >= 0x10 && wg_t[gz][gx] <= 0x14;
}

/* the footprint (true x4: middle, half extents along x and z) on level open ground: no water, road, wall or hole
 * under it or 1.5 m round it, the ground within SLOPE (village.c's 12 height units) */
static bool pv_open(double X, double Z, double ex, double ez)
{
    double lo = 1e18, hi = -1e18;
    for (int gz = (int)floor((Z - ez - 150) / WG); gz <= (int)floor((Z + ez + 150) / WG); gz++)
        for (int gx = (int)floor((X - ex - 150) / WG); gx <= (int)floor((X + ex + 150) / WG); gx++) {
            if (gx < 0 || gx >= WGX || gz < 0 || gz >= WGZ) return false;
            u8 t = wg_t[gz][gx];
            if (wg_y[gz][gx] < -1e8f || wg_wall[gz][gx] || t == 0x0E || (t >= 0x10 && t <= 0x14)) return false;
            double qx = (gx + 0.5) * WG - X, qz = (gz + 0.5) * WG - Z;
            if (pv_blocked[gz][gx] && fabs(qx) <= ex + 64 && fabs(qz) <= ez + 64) return false;   /* under the house */
            lo = fmin(lo, wg_y[gz][gx]); hi = fmax(hi, wg_y[gz][gx]);
        }
    return hi - lo <= 10.0;
}

typedef struct { VillageAuto h[PV_MAXH]; double ex[PV_MAXH], ez[PV_MAXH]; int nv[PV_MAXH]; int n; double X, Z; } PvVillage;

static void plan_villages(void)
{
    village_set_auto(NULL, 0);
    pv_obstacles();
    bool ne = DSW(DS_scene_index) == 1;
    const HouseKind *kinds = ne ? kinds_ne : kinds_w;
    int nk = ne ? (int)(sizeof kinds_ne / sizeof kinds_ne[0]) : (int)(sizeof kinds_w / sizeof kinds_w[0]);
    /* what is there already: the leg's own houses and a hand-planned village */
    double ox[NBLD + NPLAN_OLD], oz[NBLD + NPLAN_OLD], oe[NBLD + NPLAN_OLD];
    int no = 0;
    for (int i = 0; i < nbld; i++) { ox[no] = bld[i].X; oz[no] = bld[i].Z; oe[no] = fmax(bld[i].hx, bld[i].hz); no++; }
    for (int i = 0, vn = village_count(); i < vn && no < NBLD + NPLAN_OLD; i++) {
        const VillageHouse *h = village_house(i);
        ox[no] = h->px * 4.0; oz[no] = h->pz * 4.0; oe[no] = 600; no++;
    }
    double sx = (u32)DSL(DS_pos_x) / 32.0, sz = (u32)DSL(DS_pos_z) / 32.0;   /* the start (the car) */
    static PvVillage cand[96];
    int nc = 0;
    /* the straight stretches: along x (a = 0) and along z (a = 1), the road's band across at each point */
    for (int a = 0; a < 2; a++) {
        int U = a ? WGZ : WGX, V = a ? WGX : WGZ;
        int run_u0 = -1, run_v0 = 0;
        double run_c[1024], run_w[1024];
        int run_n = 0;
        for (int u = 0; u <= U; u++) {
            /* the band nearest the run's line (or the first one) at u */
            double c = -1, w = 0;
            if (u < U) {
                int want = run_u0 >= 0 ? (int)run_c[run_n - 1] : -1;
                double bestd = 1e9;
                for (int v = 0; v < V; v++) {
                    if (!(a ? pv_main_road(v, u) : pv_main_road(u, v))) continue;
                    int v1 = v;
                    while (v1 + 1 < V && (a ? pv_main_road(v1 + 1, u) : pv_main_road(u, v1 + 1))) v1++;
                    int bw = v1 - v + 1;
                    double bc = (v + v1) / 2.0;
                    if (bw >= 3 && bw <= 10) {
                        double d = want < 0 ? 0 : fabs(bc - want);
                        if (d < bestd) { bestd = d; c = bc; w = bw; }
                    }
                    v = v1;
                }
                if (run_u0 >= 0 && (c < 0 || fabs(c - run_c[run_n - 1]) > 1.0 || fabs(c - run_c[0]) > 3.0)) c = c < 0 ? -1 : -2;
            }
            if (c >= 0 && run_u0 >= 0 && run_n < 1024) { run_c[run_n] = c; run_w[run_n] = w; run_n++; continue; }
            /* the run ends: houses along it */
            if (run_u0 >= 0 && run_n * WG >= PV_RUN) {
                PvVillage *pv = &cand[nc];
                memset(pv, 0, sizeof *pv);
                int k = (int)((run_u0 * 7 + run_v0 * 13) % nk);
                for (int side = -1; side <= 1; side += 2) {
                    double along = 400;                                  /* from the run's start (x4) */
                    while (along < run_n * WG - 400 && pv->n < PV_MAXH) {
                        const HouseKind *hk = &kinds[k % nk];
                        double hx, hz;
                        int nv;
                        if (!village_model_size(hk->model, hk->parked, &hx, &hz, &nv)) { k++; along += 300; continue; }
                        /* the turn that puts the front to the road: front (world) across the road towards it */
                        double wfx = a ? -side : 0, wfz = a ? 0 : -side;
                        u8 rot = 0;
                        for (int q = 0; q < 4; q++) {
                            double ph = q * M_PI / 2, cs = cos(ph), sn = sin(ph);
                            double rx = hk->fx * cs + hk->fz * sn, rz = -hk->fx * sn + hk->fz * cs;
                            if (fabs(rx - wfx) < 0.1 && fabs(rz - wfz) < 0.1) { rot = (u8)(q * 64); break; }
                        }
                        double ex = (rot == 0 || rot == 128) ? hx : hz, ez = (rot == 0 || rot == 128) ? hz : hx;
                        double e_along = a ? ez : ex, e_across = a ? ex : ez;
                        int iu = (int)((along + e_along) / WG);
                        if (iu >= run_n) break;
                        double cu = (run_u0 + iu + 0.5) * WG, cv = (run_c[iu] + 0.5) * WG;
                        double across = cv + side * (run_w[iu] * WG / 2 + PV_SETBACK + e_across);
                        double X = a ? across : (run_u0 * WG + along + e_along), Z = a ? (run_u0 * WG + along + e_along) : across;
                        (void)cu;
                        bool ok = pv_open(X, Z, ex, ez) && hypot(X - sx, Z - sz) > 3000;
                        for (int i = 0; i < no && ok; i++) if (hypot(X - ox[i], Z - oz[i]) < oe[i] + fmax(ex, ez) + 400) ok = false;
                        for (int i = 0; i < pv->n && ok; i++)
                            if (fabs(X - pv->h[i].x) < ex + pv->ex[i] + 300 && fabs(Z - pv->h[i].z) < ez + pv->ez[i] + 300) ok = false;
                        if (!ok) { along += 200; continue; }
                        VillageAuto *h = &pv->h[pv->n];
                        h->model = hk->model; h->parked = hk->parked; h->x = X; h->z = Z; h->rot = rot;
                        pv->ex[pv->n] = ex; pv->ez[pv->n] = ez; pv->nv[pv->n] = nv;
                        pv->n++;
                        k++;
                        along += 2 * e_along + 450 + 250 * ((k * 5) % 3);
                    }
                }
                if (pv->n >= 3 && nc < 96) {
                    /* at most PV_SPAN along the road: the fullest window */
                    double mx = 0, mz = 0;
                    for (int i = 0; i < pv->n; i++) { mx += pv->h[i].x; mz += pv->h[i].z; }
                    pv->X = mx / pv->n; pv->Z = mz / pv->n;
                    int keep = 0;
                    for (int i = 0; i < pv->n; i++)
                        if (fabs((a ? pv->h[i].z - pv->Z : pv->h[i].x - pv->X)) <= PV_SPAN / 2) {
                            pv->h[keep] = pv->h[i]; pv->ex[keep] = pv->ex[i]; pv->ez[keep] = pv->ez[i]; pv->nv[keep] = pv->nv[i]; keep++;
                        }
                    pv->n = keep;
                    if (pv->n >= 3) nc++;
                    if (SDL_getenv("TD3_TOWN_DUMP") && pv->n >= 3)
                        fprintf(stderr, "town: stretch %s at %.0f %.0f, %d m: room for %d houses\n", a ? "N-S" : "E-W", pv->X, pv->Z,
                                (int)(run_n * WG / 100), pv->n);
                }
            }
            run_u0 = -1;
            run_n = 0;
            if (c >= 0 || c == -2) {                                     /* a new run from here */
                if (c == -2) {
                    /* the band that broke the run starts the next: find it again without a line */
                    double bc = -1, bw = 0;
                    for (int v = 0; v < V; v++) {
                        if (!(a ? pv_main_road(v, u) : pv_main_road(u, v))) continue;
                        int v1 = v;
                        while (v1 + 1 < V && (a ? pv_main_road(v1 + 1, u) : pv_main_road(u, v1 + 1))) v1++;
                        if (v1 - v + 1 >= 3 && v1 - v + 1 <= 10) { bc = (v + v1) / 2.0; bw = v1 - v + 1; break; }
                        v = v1;
                    }
                    c = bc; w = bw;
                }
                if (c >= 0) { run_u0 = u; run_v0 = (int)c; run_c[0] = c; run_w[0] = w; run_n = 1; }
            }
        }
    }
    /* the villages: the most houses first, then the nearest the start; apart from what is there and each other */
    VillageAuto out[2 * PV_MAXH];
    int nout = 0, nvill = 0;
    static int cellv[512];
    memset(cellv, 0, sizeof cellv);
    for (int i = 0, vn = village_count(); i < vn; i++) {                 /* a hand-planned village's vertices */
        const VillageHouse *h = village_house(i);
        double hx, hz;
        int nv, c = (int)(h->px * 4.0 / 4096) + (15 - (int)(h->pz * 4.0 / 4096)) * 32;
        if (c >= 0 && c < 512 && village_model_size(h->model, h->parked, &hx, &hz, &nv)) cellv[c] += nv;
    }
    bool used[96] = { false };
    while (nvill < PV_VILLAGES) {
        int best = -1;
        for (int i = 0; i < nc; i++) {
            if (used[i]) continue;
            bool far_ok = true;
            for (int j = 0; j < nout && far_ok; j++) if (hypot(cand[i].X - out[j].x, cand[i].Z - out[j].z) < PV_APART) far_ok = false;
            if (!far_ok) { used[i] = true; continue; }
            if (best < 0 || cand[i].n > cand[best].n ||
                (cand[i].n == cand[best].n && hypot(cand[i].X - sx, cand[i].Z - sz) < hypot(cand[best].X - sx, cand[best].Z - sz)))
                best = i;
        }
        if (best < 0) break;
        used[best] = true;
        int took = 0;
        for (int i = 0; i < cand[best].n; i++) {
            const VillageAuto *h = &cand[best].h[i];
            int c = (int)(h->x / 4096) + (15 - (int)(h->z / 4096)) * 32;
            if (c < 0 || c >= 512 || cellv[c] + cand[best].nv[i] > PV_CELLV) continue;
            cellv[c] += cand[best].nv[i];
            out[nout++] = *h;
            took++;
        }
        if (SDL_getenv("TD3_TOWN_DUMP"))
            fprintf(stderr, "town: planned village at %.0f %.0f (cell %.1f,%.1f): %d of %d houses\n", cand[best].X, cand[best].Z,
                    cand[best].X / 4096.0, 16 - cand[best].Z / 4096.0, took, cand[best].n);
        if (took) nvill++;
    }
    if (SDL_getenv("TD3_TOWN_DUMP")) fprintf(stderr, "town: %d straight stretches with room for a village\n", nc);
    village_set_auto(out, nout);
}

static void scan(void)
{
    nbld = nvil = n_onroad = 0;
    road_net();
    road_comps();
    u64 t0 = SDL_GetTicksNS();
    walk_grid();
    walk_ms = (SDL_GetTicksNS() - t0) / 1e6;
    u16 es = DSW(DS_objects_set + 2);
    if (!es) return;
    int first = DSW(DS_obj_first_static), n = DSW(DS_obj_count);
    for (int o = 1; o < n && o < 160 && nbld < NBLD; o++) {
        u16 f = DSW(DS_obj_flags + 2 * o);
        bool stat = o >= first, parked = !stat && (f & 0x3000) == 0x1000;
        if (!stat && !parked) continue;
        if (stat && ((f >> 8) & 0x30)) continue;
        if (parked && (f >> 8) == 0x1F) continue;                        /* the lightning bolt */
        int m = f & 0x3F;
        if (m <= 3) continue;
        /* model_place's turn: by the heading's high byte (a static: quarter turns only) */
        u8 rot = (u8)(DSW(DS_obj_heading + 2 * o) >> 8);
        if (stat) rot &= 0xC0;
        add_house(m, stat, DSW(DS_obj_x + 2 * o) * 4.0, DSW(DS_obj_z + 2 * o) * 4.0, DSW(DS_obj_y8 + 2 * o) >> 3, rot, o, SV_NONE);
    }
    if (!SDL_getenv("TD3_NO_VILLAGE")) plan_villages();                 /* OG: more villages */
    /* the planned services in the leg's own houses: the house nearest the place (within 10 m) */
    for (int t = 0; ; t++) {
        double tx, tz;
        int sv = village_tag(t, &tx, &tz);
        if (sv == VSV_NONE) break;
        int best = -1;
        double bd = 1000.0 * 1000.0;
        for (int i = 0; i < nbld; i++) {
            double dx = bld[i].X - tx, dz = bld[i].Z - tz;
            if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = i; }
        }
        if (best >= 0) bld[best].sv = sv;
    }
    /* the planned villages' houses (game/village.c), their services with them */
    for (int i = 0, vn = village_count(); i < vn; i++) {
        const VillageHouse *h = village_house(i);
        add_house(h->model, !h->parked, h->px * 4.0, h->pz * 4.0, h->py, h->rot, -1, h->sv);
    }
    /* the door: on the side with the shortest way on foot from the main road (not into another house); a house
     * with no way to any side keeps the side nearest a road, its road 1e9: no service, no job goes there */
    for (int i = 0; i < nbld; i++) {
        Bld *b = &bld[i];
        double zx = -b->az, zz = b->ax;                                  /* local z (sn, cs) */
        double best = 1e18;
        for (int s = 0; s < 4; s++) {
            double nx = s == 0 ? b->ax : s == 1 ? -b->ax : s == 2 ? zx : -zx;
            double nz = s == 0 ? b->az : s == 1 ? -b->az : s == 2 ? zz : -zz;
            double ext = s < 2 ? b->hx : b->hz, half = s < 2 ? b->hz : b->hx;
            double mx = b->X + nx * ext, mz = b->Z + nz * ext;
            double dX = mx + nx * DOOR_OUT, dZ = mz + nz * DOOR_OUT;
            int cell;
            double air = road_dist(dX, dZ), walk = walk_at(dX, dZ, &cell);
            double score = walk < 1e8 ? walk : 1e8 + air;
            for (int j = 0; j < nbld; j++) if (j != i && inside(&bld[j], dX, dZ, 150)) score += 1e10;
            if (half < 90) score += 5000;                                /* a narrow end: no door there */
            if (score < best) {
                best = score;
                b->nx = nx; b->nz = nz; b->half = half;
                b->road = walk; b->road_air = air; b->rcell = cell;
                b->dX = dX; b->dZ = dZ;
                b->gX = mx + nx * BAY_OUT; b->gZ = mz + nz * BAY_OUT;
            }
        }
        b->gy = b->y;
    }
    /* villages: the houses within LINK of each other */
    for (int i = 0; i < nbld; i++) {
        if (bld[i].vil >= 0 || nvil >= NVIL) continue;
        int v = nvil++, stack[NBLD], ns = 0;
        memset(&vil[v], 0, sizeof vil[v]);
        bld[i].vil = v;
        stack[ns++] = i;
        while (ns) {
            int a = stack[--ns];
            vil[v].X += bld[a].X; vil[v].Z += bld[a].Z; vil[v].nb++;
            for (int j = 0; j < nbld; j++) {
                if (bld[j].vil >= 0) continue;
                double dx = bld[j].X - bld[a].X, dz = bld[j].Z - bld[a].Z;
                if (dx * dx + dz * dz < LINK * LINK) { bld[j].vil = v; stack[ns++] = j; }
            }
        }
        vil[v].X /= vil[v].nb; vil[v].Z /= vil[v].nb;
    }
    /* names: the same for the same leg every time; the planned village keeps its own */
    bool used_name[NNAMES] = { false };
    u32 seed = (u32)DSW(DS_scene_index) * 7919u + DSB(DS_leg_index) * 104729u;
    const char *const *list = DSW(DS_scene_index) == 1 ? names_ne : names;
    for (int v = 0; v < nvil; v++) {
        u32 h = seed + (u32)(vil[v].X / 512) * 31u + (u32)(vil[v].Z / 512) * 17u;
        h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
        int k = (int)(h % NNAMES);
        for (int t = 0; t < NNAMES && used_name[k]; t++) k = (k + 1) % NNAMES;
        used_name[k] = true;
        vil[v].name = list[k];
    }
    if (village_name())
        for (int i = 0; i < nbld; i++)
            if (bld[i].obj < 0 && bld[i].vil >= 0) { vil[bld[i].vil].name = village_name(); break; }
    for (int i = 0; i < nbld; i++) if (bld[i].sv && bld[i].vil >= 0) vil[bld[i].vil].nsv++;   /* the planned services */
    /* the services: each once on the leg, in the bigger villages first, apart from each other; on a long leg
     * with many houses a second round, far from the first */
    static const int order[4] = { SV_GUNS, SV_PUB, SV_GARAGE, SV_POLICE };
    for (int round = 0; round < 2; round++) {
        if (round == 1 && nbld < 12) break;
        for (int q = 0; q < 4; q++) {
            int sv = order[q], best = -1;
            double bs = -1e18;
            bool planned = false;                                        /* a planned village has it: the first round's */
            for (int j = 0; j < nbld; j++) if (bld[j].sv == sv) planned = true;   /* (only planned ones yet) */
            if (round == 0 && planned) continue;
            for (int i = 0; i < nbld; i++) {
                const Bld *b = &bld[i];
                if (b->sv || b->road > (sv == SV_GARAGE ? 1400 : 3000)) continue;
                const Vil *V = &vil[b->vil];
                double near_same = 1e9;
                for (int j = 0; j < nbld; j++)
                    if (bld[j].sv == sv) near_same = fmin(near_same, hypot(bld[j].X - b->X, bld[j].Z - b->Z));
                if (round == 1 && near_same < 30000) continue;
                double score = V->nb * 10.0 - V->nsv * 22.0 - b->road / 150.0 + fmin(near_same, 60000) / 3000.0;
                if (score > bs) { bs = score; best = i; }
            }
            if (best < 0) continue;
            bld[best].sv = sv;
            vil[bld[best].vil].nsv++;
        }
    }
}

/* developer aid TD3_TOWN_MAP=file.ppm: the leg from above, 1 pixel = 32 x4 units (4096 x 2048): the tiles' faces by
 * type (road grey, water blue, the ground green by its height, walls brown), the houses found (red, the door's side
 * yellow), the car's start (white) */
static void town_map(const char *path)
{
    const int W = 4096, H = 2048, U = 32;
    u8 *img = calloc((size_t)W * H, 3);
    float *zb = malloc(sizeof(float) * (size_t)W * H);
    if (!img || !zb) { free(img); free(zb); return; }
    for (int i = 0; i < W * H; i++) zb[i] = -1e9f;
    for (int c = 0; c < 512; c++) {
        int col = c & 31, row = c >> 5, zc = 15 - row;
        u16 m = DSW(DS_leg_map + 2 * c);
        u16 es, si;
        int nf, nv;
        if (!tile_model((u8)m, &es, &si, &nf, &nv)) continue;
        u8 rot = (u8)((m >> 8) & 0xC0);
        double ox = col * 4096.0 + 2048.0, oz = zc * 4096.0 + 2048.0, oy = (m & 0x3F00);
        u16 stride = (u16)(nv * 2), f = (u16)(si + 4 + 3 * stride);
        for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
            u16 w[4];
            for (int j = 0; j < 4; j++) w[j] = rd16(es, (u16)(f + 2 * j));
            int t = w[3] >> 11, n = (w[0] >> 14) + 1;
            if (n < 3 || t == 0) continue;
            double vx[4], vz[4], vy[4];
            bool ok = true;
            for (int j = 0; j < n; j++) {
                int vi = w[j] & 0x7FF;
                if (vi >= nv) { ok = false; break; }
                double a = (s16)rd16(es, (u16)(si + 4 + 2 * vi)), b = (s16)rd16(es, (u16)(si + 4 + stride + 2 * vi)),
                       cc = (s16)rd16(es, (u16)(si + 4 + 2 * stride + 2 * vi));
                double rb = b, rc = cc;
                if (rot == 0x40)      { rb = cc; rc = -b; }
                else if (rot == 0x80) { rb = -b; rc = -cc; }
                else if (rot == 0xC0) { rb = -cc; rc = b; }
                vx[j] = (ox + rb) / U; vz[j] = (oz + rc) / U; vy[j] = oy + a;
            }
            if (!ok) continue;
            double ymean = 0;
            for (int j = 0; j < n; j++) ymean += vy[j] / n;
            int r, g, b;
            double hl = fmin(1.0, fmax(0.0, (ymean - 200) / 1600.0));
            if (t == 0x0E) { r = 30; g = 60; b = 170; }
            else if (t >= 0x10 && t <= 0x14) { r = 80; g = 80; b = 84; }
            else if (t == 1 || t == 3) { r = 120; g = 80; b = 50; }
            else if (t == 0x1F) { r = 220; g = 200; b = 60; }
            else { r = (int)(40 + 90 * hl); g = (int)(110 + 100 * hl); b = (int)(40 + 60 * hl); }
            int y0 = H, y1 = -1;
            for (int j = 0; j < n; j++) { int yy = (int)(H - vz[j]); if (yy < y0) y0 = yy; if (yy > y1) y1 = yy; }
            if (y0 < 0) y0 = 0;
            if (y1 >= H) y1 = H - 1;
            for (int yy = y0; yy <= y1; yy++) {
                double zc2 = H - yy - 0.5, xl = 1e9, xr = -1e9;
                for (int j = 0; j < n; j++) {
                    int j2 = (j + 1) % n;
                    double za = vz[j], zb2 = vz[j2];
                    if ((zc2 < za) == (zc2 < zb2)) continue;
                    double xx = vx[j] + (zc2 - za) / (zb2 - za) * (vx[j2] - vx[j]);
                    xl = fmin(xl, xx); xr = fmax(xr, xx);
                }
                for (int xx = (int)fmax(0, ceil(xl - 0.5)); xx <= (int)fmin(W - 1, floor(xr - 0.5)); xx++) {
                    size_t i = (size_t)yy * W + xx;
                    if (ymean < zb[i] - 1) continue;                     /* the highest face shows */
                    zb[i] = (float)ymean;
                    img[3 * i] = (u8)r; img[3 * i + 1] = (u8)g; img[3 * i + 2] = (u8)b;
                }
            }
        }
    }
    for (int yy = 0; yy < H; yy++)                                       /* on foot from the main road: lighter */
        for (int xx = 0; xx < W; xx++) {                                 /* (walls black) */
            int gx = (int)((xx + 0.5) * U / WG), gz = (int)((H - yy - 0.5) * U / WG);
            if (gx >= WGX || gz >= WGZ) continue;
            size_t i = (size_t)yy * W + xx;
            if (wg_wall[gz][gx]) { img[3 * i] = img[3 * i + 1] = img[3 * i + 2] = 0; continue; }
            if (wg_d[gz][gx] < 1e8f)
                for (int ch = 0; ch < 3; ch++) img[3 * i + ch] = (u8)(img[3 * i + ch] + (255 - img[3 * i + ch]) * 0.35);
        }
    for (int i = 0; i < nbld; i++) {                                     /* the houses (no goal: magenta) */
        const Bld *b = &bld[i];
        double zx = -b->az, zz = b->ax;
        bool goal = house_reachable(i);
        for (double u = -b->hx; u <= b->hx; u += U / 2.0)
            for (double v = -b->hz; v <= b->hz; v += U / 2.0) {
                double X = b->X + b->ax * u + zx * v, Z = b->Z + b->az * u + zz * v;
                int xx = (int)(X / U), yy = (int)(H - Z / U);
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                size_t k = (size_t)yy * W + xx;
                img[3 * k] = goal ? 200 : 230; img[3 * k + 1] = 40; img[3 * k + 2] = goal ? 40 : 230;
            }
        for (int d = -3; d <= 3; d++) {
            int xx = (int)(b->dX / U) + d, yy = (int)(H - b->dZ / U);
            for (int e = -1; e <= 1; e++)
                if (xx >= 0 && xx < W && yy + e >= 0 && yy + e < H) { size_t k = (size_t)(yy + e) * W + xx; img[3 * k] = 250; img[3 * k + 1] = 230; img[3 * k + 2] = 40; }
        }
    }
    {   /* the car */
        int cx = (int)((u32)DSL(DS_pos_x) / 32.0 / U), cy = (int)(H - (u32)DSL(DS_pos_z) / 32.0 / U);
        for (int d = -6; d <= 6; d++)
            for (int e = -6; e <= 6; e++)
                if (abs(d) + abs(e) <= 6 && cx + d >= 0 && cx + d < W && cy + e >= 0 && cy + e < H) {
                    size_t k = (size_t)(cy + e) * W + cx + d;
                    img[3 * k] = img[3 * k + 1] = img[3 * k + 2] = 255;
                }
    }
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fprintf(fp, "P6\n%d %d\n255\n", W, H);
        fwrite(img, 3, (size_t)W * H, fp);
        fclose(fp);
        fprintf(stderr, "town: map written to %s (1 px = %d units; car at %.0f %.0f)\n", path, U, (u32)DSL(DS_pos_x) / 32.0,
                (u32)DSL(DS_pos_z) / 32.0);
    }
    free(img);
    free(zb);
}

/* developer aid TD3_TOWN_FLAT=x0,z0,x1,z1 (true x4 units): the ground's level there, 2 m a character: '.' level
 * within 28 cm over 6 m, '+' within 1 m, '#' steeper, '~' none; the road '=' */
static void town_flat(const char *spec)
{
    double x0, z0, x1, z1;
    if (sscanf(spec, "%lf,%lf,%lf,%lf", &x0, &z0, &x1, &z1) != 4) return;
    fprintf(stderr, "flat: x %.0f .. %.0f (2 m a column), z %.0f (top) .. %.0f\n", x0, x1, z1, z0);
    for (double z = z1; z >= z0; z -= 200) {
        char line[512];
        int n = 0;
        for (double x = x0; x <= x1 && n < 500; x += 200) {
            double lo = 1e9, hi = -1e9;
            bool none = false;
            for (int a = -1; a <= 1; a++)
                for (int b = -1; b <= 1; b++) {
                    double g = village_ground(x + a * 300, z + b * 300);
                    if (g < -1e8) { none = true; continue; }
                    lo = fmin(lo, g); hi = fmax(hi, g);
                }
            char ch = none ? '~' : hi - lo <= 12 ? '.' : hi - lo <= 43 ? '+' : '#';
            if (road_dist(x, z) < 100) ch = '=';
            line[n++] = ch;
        }
        line[n] = 0;
        fprintf(stderr, "flat: %6.0f %s\n", z, line);
    }
}

/* developer aid TD3_WG_DUMP=x0,z0,x1,z1 (true x4): the walk grid there, a point a character: '=' road, '.' ground,
 * '~' water, '#' wall, ' ' no ground; upper case / '*' within reach of the main road; then the heights in tens */
static void wg_dump(const char *spec)
{
    double x0, z0, x1, z1;
    if (sscanf(spec, "%lf,%lf,%lf,%lf", &x0, &z0, &x1, &z1) != 4) return;
    int gx0 = (int)(x0 / WG), gx1 = (int)(x1 / WG), gz0 = (int)(z0 / WG), gz1 = (int)(z1 / WG);
    fprintf(stderr, "wg: x %d .. %d, z %d (top) .. %d\n", gx0, gx1, gz1, gz0);
    for (int pass = 0; pass < 2; pass++)
        for (int gz = gz1; gz >= gz0; gz--) {
            char line[600];
            int n = 0;
            for (int gx = gx0; gx <= gx1 && n < 590; gx++) {
                if (gx < 0 || gx >= WGX || gz < 0 || gz >= WGZ) { line[n++] = '?'; continue; }
                bool in = wg_d[gz][gx] < 1e8f;
                u8 t = wg_t[gz][gx];
                if (pass == 1) {                                         /* the height in tens: three digits */
                    if (wg_y[gz][gx] < -1e8) { line[n++] = ' '; line[n++] = ' '; line[n++] = ' '; line[n++] = ' '; continue; }
                    n += snprintf(line + n, 8, "%3d ", ((int)(wg_y[gz][gx] / 10) % 1000 + 1000) % 1000);
                    continue;
                }
                char ch = wg_y[gz][gx] < -1e8 ? ' ' : wg_wall[gz][gx] ? '#' : t == 0x0E ? '~' : (t >= 0x10 && t <= 0x14) ? (in ? 'R' : '=') : (in ? '*' : '.');
                line[n++] = ch;
            }
            line[n] = 0;
            fprintf(stderr, "wg%c %4d %s\n", pass ? 'h' : ':', gz, line);
        }
}

void enh_town_dump(void)
{
    const char *flat = SDL_getenv("TD3_TOWN_FLAT");
    if (flat) town_flat(flat);
    const char *wgd = SDL_getenv("TD3_WG_DUMP");
    if (wgd) wg_dump(wgd);
    const char *map_path = SDL_getenv("TD3_TOWN_MAP");
    if (map_path) town_map(map_path);
    fprintf(stderr, "town: scene %d leg %d: %d houses (%d over the road are none), %d villages\n", DSW(DS_scene_index),
            DSB(DS_leg_index), nbld, n_onroad, nvil);
    {
        int cells = 0, reach = 0, goals = 0;
        for (int c = 0; c < 512; c++) cells += rnode[c];
        for (int gz = 0; gz < WGZ; gz++) for (int gx = 0; gx < WGX; gx++) reach += wg_d[gz][gx] < 1e8f;
        for (int i = 0; i < nbld; i++) goals += house_reachable(i);
        int sc = road_cell_near((u32)DSL(DS_pos_x) / 32.0, (u32)DSL(DS_pos_z) / 32.0);
        int fc = finish_cell >= 0 && !rnode[finish_cell] ? road_cell_near((finish_cell & 31) * 4096.0 + 2048.0,
                                                                          (15 - (finish_cell >> 5)) * 4096.0 + 2048.0) : finish_cell;
        fprintf(stderr, "town: roads %d cells in %d parts, the main road %d cells (start %s, finish %s); on foot %d points "
                "(%.0f%%, %.0f ms)%s; %d of %d houses reachable\n", cells, rcomp_n, main_cells,
                sc >= 0 && rcomp[sc] == main_comp ? "on it" : "OFF IT", fc >= 0 && rcomp[fc] == main_comp ? "on it" : "OFF IT",
                reach, 100.0 * reach / (WGX * WGZ), walk_ms, wg_heap_full ? " (heap full)" : "", goals, nbld);
    }
    for (int v = 0; v < nvil; v++)
        fprintf(stderr, "  village %d %-16s %d houses, %d services, at cell %.1f,%.1f\n", v, vil[v].name, vil[v].nb,
                vil[v].nsv, vil[v].X / 4096.0, 15 - vil[v].Z / 4096.0 + 1);
    for (int i = 0; i < nbld; i++) {
        const Bld *b = &bld[i];
        u16 fl = b->obj > 0 ? DSW(DS_obj_flags + 2 * b->obj) : 0;
        /* the door's side in the model's own frame: +x, -x, +z, -z */
        double lx = b->nx * b->ax + b->nz * b->az, lz = b->nx * -b->az + b->nz * b->ax;
        const char *side = fabs(lx) > fabs(lz) ? (lx > 0 ? "+x" : "-x") : (lz > 0 ? "+z" : "-z");
        fprintf(stderr, "  house %2d obj %3d model %2d rot %3d village %2d %-7s %4.0f x %4.0f h %3.0f at %6.0f %6.0f y %4.0f; "
                "door %6.0f %6.0f (local %s), road %5.0f straight, on foot %s%5.0f%s; roof %.2f, road under %.2f\n", i, b->obj, fl & 0x3F,
                b->obj > 0 ? DSW(DS_obj_heading + 2 * b->obj) >> 8 : 0, b->vil, sv_name[b->sv], 2 * b->hx, 2 * b->hz, b->top,
                b->X, b->Z, b->y, b->dX, b->dZ, side, b->road_air, b->road < 1e8 ? "" : "none ", b->road < 1e8 ? b->road : 0.0,
                house_reachable(i) ? "" : b->road_air <= 4000 ? "  << NO GOAL (was one)" : "  (no goal)", b->cover, b->onroad);
    }
    if (SDL_getenv("TD3_MODEL_DUMP")) {                                 /* TD3_MODEL_DUMP=m[,s]: a model's vertices and faces */
        const char *md = SDL_getenv("TD3_MODEL_DUMP");
        int m = SDL_atoi(md), stat = strchr(md, 's') != NULL;
        u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
        if (es && m > 3 && m < 64) {
            u16 p = (u16)(rd16(es, (u16)(set + 2 * m)) + set);
            int nf = rd8(es, p), nv = rd8(es, (u16)(p + 1));
            u16 a = (u16)(p + (stat ? 4 : 8)), stride = (u16)(nv * 2);
            fprintf(stderr, "model %d (%s): %d faces, %d vertices\n", m, stat ? "static" : "parked", nf, nv);
            for (int k = 0; k < nv; k++)
                fprintf(stderr, "  v%-3d h %5d x %5d z %5d\n", k, (s16)rd16(es, (u16)(a + 2 * k)),
                        (s16)rd16(es, (u16)(a + stride + 2 * k)), (s16)rd16(es, (u16)(a + 2 * stride + 2 * k)));
            u16 f = (u16)(a + 3 * stride);
            for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
                u16 w0 = rd16(es, f), w1 = rd16(es, (u16)(f + 2)), w2 = rd16(es, (u16)(f + 4)), w3 = rd16(es, (u16)(f + 6));
                int n = (w0 >> 14) + 1;
                fprintf(stderr, "  f%-3d type %2d n %d: %d %d %d %d (colours %02X %02X)\n", k, w3 >> 11, n, w0 & 0x7FF,
                        w1 & 0x7FF, w2 & 0x7FF, w3 & 0x7FF, (w1 >> 11) & 0x1F, (w2 >> 11) & 0x1F);
            }
        }
    }
    if (SDL_getenv("TD3_OBJ_DUMP")) {                                   /* every object of the leg: model, flags, size */
        u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
        int first = DSW(DS_obj_first_static);
        for (int o = 1; o < DSW(DS_obj_count) && o < 160 && es; o++) {
            u16 f = DSW(DS_obj_flags + 2 * o);
            int m = f & 0x3F;
            u16 p = (u16)(rd16(es, (u16)(set + 2 * m)) + set);
            int nf = rd8(es, p), nv = rd8(es, (u16)(p + 1));
            bool stat = o >= first;
            u16 a = (u16)(p + (stat ? 4 : 8)), stride = (u16)(nv * 2);
            double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9, h0 = 1e9, h1 = -1e9;
            for (int k = 0; k < nv && k < 300; k++) {
                double h = (s16)rd16(es, (u16)(a + 2 * k)), x = (s16)rd16(es, (u16)(a + stride + 2 * k)),
                       z = (s16)rd16(es, (u16)(a + 2 * stride + 2 * k));
                x0 = fmin(x0, x); x1 = fmax(x1, x); z0 = fmin(z0, z); z1 = fmax(z1, z); h0 = fmin(h0, h); h1 = fmax(h1, h);
            }
            fprintf(stderr, "obj %3d %s model %2d flags %04X at %6.0f %6.0f y %4u: %d faces %d verts, %.0f x %.0f h %.0f\n", o,
                    stat ? "static" : "dyn   ", m, f, DSW(DS_obj_x + 2 * o) * 4.0, DSW(DS_obj_z + 2 * o) * 4.0,
                    DSW(DS_obj_y8 + 2 * o) >> 3, nf, nv, x1 - x0, z1 - z0, h1 - h0);
        }
    }
    /* the object models the leg's houses use: their format and counts (village.c copies them) */
    {
        bool seen[64] = { false };
        int first = DSW(DS_obj_first_static);
        u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
        for (int i = 0; i < nbld && es; i++) {
            int o = bld[i].obj;
            if (o <= 0) continue;
            int m = DSW(DS_obj_flags + 2 * o) & 0x3F;
            if (seen[m]) continue;
            seen[m] = true;
            u16 p = (u16)(rd16(es, (u16)(set + 2 * m)) + set);
            fprintf(stderr, "  model %2d: %s, %d faces, %d vertices, %.0f x %.0f h %.0f\n", m, o >= first ? "static" : "parked",
                    rd8(es, p), rd8(es, (u16)(p + 1)), 2 * bld[i].hx, 2 * bld[i].hz, bld[i].top);
        }
    }
}

/* ------------------------------------------------------------------------------------------------------
 * The shops
 * ------------------------------------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------------------------------------
 * Jobs (TOWNS.md "Jobs"): the pub's barkeeper has a parcel to take somewhere, the police station a bounty on a gang;
 * made up at random, one at a time. While one runs, a line at the top and an arrow point the way.
 * ------------------------------------------------------------------------------------------------------ */

enum { JOB_NONE, JOB_PARCEL, JOB_EXPRESS, JOB_HOT, JOB_BOUNTY, JOB_RACE, JOB_GETAWAY, JOB_RECOVER, JOB_TAXI, JOB_N };
static const char *const job_name[JOB_N] = { "", "PARCEL", "EXPRESS PARCEL", "HOT GOODS", "BOUNTY", "RACE", "GETAWAY",
                                             "STOLEN CAR", "TAXI" };
typedef struct {
    int type, stage;
    int from;                   /* the house that gave it (the bounty's police station) */
    int target;                 /* the goal's house: a delivery's, the taxi's, the race's, the getaway's drop, the gang's,
                                 * the theft's village */
    int target2;                /* the getaway: where the loot waits */
    double gx, gz, gy;          /* the gang's place (true x4, height units) */
    double t_left, t;           /* seconds left (0: no limit); a stage's clock */
    int reward;
    int car;                    /* the rival's or the stolen car's object, 0 none */
    u16 car_flags;              /* its flags before */
    bool spawned, crashed;
    double cd;                  /* the race's count down (seconds run; < 0: not side by side yet) */
} Job;
static Job job, offer[2];       /* the job running; the pub's and the police's offer (type 0: none made yet) */
static u32 job_rng;

static double job_rand(void)
{
    if (!job_rng) job_rng = (u32)SDL_GetTicksNS() | 1u;
    job_rng ^= job_rng << 13; job_rng ^= job_rng >> 17; job_rng ^= job_rng << 5;
    return (job_rng & 0xFFFFFF) / 16777216.0;
}

static const char *house_village(int i)
{
    return i >= 0 && i < nbld && bld[i].vil >= 0 ? vil[bld[i].vil].name : "?";
}
static const char *job_where(const Job *j) { return house_village(j->target); }

/* the way by road from house a's door to house b's (their main road cells), metres; -1 none */
static double house_route(int a, int b)
{
    if (a < 0 || a >= nbld || b < 0 || b >= nbld) return -1;
    return route_cells(bld[a].rcell, bld[b].rcell);
}

/* a house without a service in another village than `from`'s, dmin .. dmax (x4, straight) from `from`, its door
 * reachable (house_reachable) and by road from `from`'s, not `not`; -1 none */
static int pick_list(int from, double dmin, double dmax, int not, int *cand)
{
    int nc = 0;
    if (from < 0 || from >= nbld) return 0;
    double X = bld[from].X, Z = bld[from].Z;
    for (int pass = 0; pass < 2 && nc == 0; pass++)
        for (int i = 0; i < nbld; i++) {
            const Bld *b = &bld[i];
            double d = hypot(b->X - X, b->Z - Z);
            if (i == from || i == not || !house_reachable(i)) continue;
            if (pass == 0 && (b->sv || b->vil == bld[from].vil || d < dmin || d > dmax)) continue;
            /* a leg of few houses: any house 60 m on, the services too, but not a police station (a getaway's drop
             * there could never be made: its car sees the drop; nor a gang's hideout, nor hot goods) */
            if (pass == 1 && (d < 6000 || b->sv == SV_POLICE)) continue;
            if (house_route(from, i) < 0) continue;
            cand[nc++] = i;
        }
    return nc;
}

static int pick_house(int from, double dmin, double dmax, int not)
{
    int cand[NBLD], nc = pick_list(from, dmin, dmax, not, cand);
    return nc ? cand[(int)(job_rand() * nc) % nc] : -1;
}

/* a parcel's time and pay for the way from house `from` to house t (TOWNS.md "Jobs") */
static void delivery_terms(Job *o, int from, int t, double time_k, double pay_k)
{
    const Bld *b = &bld[t];
    double X = bld[from].X, Z = bld[from].Z;
    double m = house_route(from, t), walk = b->road / 100.0;
    double secs = 40 + m / 10.0 + walk / 3.0, pay = fmin(60 + m * 0.3, 500);
    if (o->type == JOB_EXPRESS) secs = 30 + m / 13.0 + walk / 4.0;
    o->t_left = env_num("TD3_JOB_TIME", secs * time_k);
    o->reward = ((int)(pay * pay_k) + 5) / 10 * 10;
    if (SDL_getenv("TD3_FOES_DEBUG") && !SDL_getenv("TD3_JOB_CHECK"))
        fprintf(stderr, "job offer: %s to house %d: %.0f m by road (%.0f straight), %.0f m on foot, %.0f s\n",
                job_name[o->type], t, m, hypot(b->X - X, b->Z - Z) / 100.0, walk, o->t_left);
}

/* an offer from the house `from` (kind 0: the pub, 1: the police station), at random (TD3_JOB_TYPE=n: that one) */
static bool make_offer_once(int kind, int from);

/* an offer: another kind of job when one cannot be made here (a getaway with no drop on a leg of two houses); none
 * left over. A failed one kept its type, and the next visit showed it as a job with no goal */
static bool make_offer(int kind, int from)
{
    for (int k = 0; k < 8; k++)
        if (make_offer_once(kind, from)) return true;
    memset(&offer[kind], 0, sizeof offer[kind]);
    return false;
}

static bool make_offer_once(int kind, int from)
{
    Job *o = &offer[kind];
    memset(o, 0, sizeof *o);
    if (from < 0 || from >= nbld) return false;
    const Bld *f = &bld[from];
    o->from = from;
    o->target2 = -1;
    double r = job_rand();
    int want = (int)env_num("TD3_JOB_TYPE", 0);
    if (kind == 1) o->type = want ? want : r < 0.55 ? JOB_BOUNTY : JOB_RECOVER;
    else o->type = want ? want : r < 0.22 ? JOB_PARCEL : r < 0.35 ? JOB_EXPRESS : r < 0.45 ? JOB_HOT :
                                 r < 0.65 ? JOB_RACE : r < 0.80 ? JOB_GETAWAY : JOB_TAXI;
    switch (o->type) {
    case JOB_BOUNTY: case JOB_RECOVER: {
        int t = pick_house(from, 20000, 70000, -1);
        if (t < 0) return false;
        const Bld *b = &bld[t];
        o->target = t;
        o->gx = b->dX + b->nx * 500; o->gz = b->dZ + b->nz * 500; o->gy = b->gy;   /* before its door */
        if (walk_at(o->gx, o->gz, NULL) > 1e8) { o->gx = b->dX; o->gz = b->dZ; }   /* no way there: at the door */
        o->reward = o->type == JOB_BOUNTY ? 300 + 50 * (int)(job_rand() * 5) : 250 + 50 * (int)(job_rand() * 4);
        return true;
    }
    case JOB_RACE: {                                                 /* a village ahead, on the way to the finish */
        double here = to_finish(f->rcell);
        int cand[NBLD], nc = 0;
        for (int i = 0; i < nbld && here > 0; i++) {
            const Bld *b = &bld[i];
            if (b->vil == f->vil || !house_reachable(i)) continue;
            double m = house_route(from, i), there = to_finish(b->rcell);
            if (m < 300 || m > 1300 || there < 0 || there > here - 200) continue;
            cand[nc++] = i;
        }
        if (!nc) { o->type = JOB_PARCEL; break; }                     /* nothing ahead: a parcel instead */
        o->target = cand[(int)(job_rand() * nc) % nc];
        o->reward = 200 + 50 * (int)(job_rand() * 5);
        o->t_left = 0;
        if (SDL_getenv("TD3_FOES_DEBUG"))
            fprintf(stderr, "job offer: RACE to house %d (%s), %.0f m by road\n", o->target, house_village(o->target),
                    house_route(from, o->target));
        return true;
    }
    case JOB_GETAWAY: {                                              /* the loot somewhere, the drop somewhere else */
        int a = pick_house(from, 12000, 60000, -1);
        if (a < 0) return false;
        int b = pick_house(a, 15000, 70000, from);
        if (b < 0) return false;
        o->target2 = a;
        o->target = b;
        o->reward = 500 + 100 * (int)(job_rand() * 5);
        return true;
    }
    default: break;
    }
    int t = pick_house(from, 12000, 70000, -1);
    if (t < 0) return false;
    o->target = t;
    if (o->type == JOB_TAXI) delivery_terms(o, from, t, 1.15, 1.2);
    else delivery_terms(o, from, t, 1.0, o->type == JOB_EXPRESS ? 1.6 : o->type == JOB_HOT ? 2.2 : 1.0);
    return true;
}

/* ------------------------------------------------------------------------------------------------------
 * Developer aid TD3_JOB_CHECK=1 (TOWNS.md "Jobs"): every offer each pub and police station of the leg can make,
 * checked at the leg's start. The way between two doors on the walk grid, the road preferred (a step off it counts
 * 8 times): its length on the road and off it. A timed job needs the player to get to the car (15 s), drive the
 * road part, walk the rest (5 m/s): the speed it asks on the road is printed, over 13 m/s TIGHT, over 17 m/s (the car
 * does 19 at most, measured) TOO SHORT.
 * ------------------------------------------------------------------------------------------------------ */
static bool check_way(double X0, double Z0, double X1, double Z1, double *on, double *off)
{
    int sx = (int)floor(X0 / WG), sz = (int)floor(Z0 / WG), tx = (int)floor(X1 / WG), tz = (int)floor(Z1 / WG);
    *on = *off = 0;
    for (int a = -1; a <= 1 && !wg_walkable(sx, sz); a++)                 /* the door's point or one by it */
        for (int b = -1; b <= 1; b++) if (wg_walkable(sx + a, sz + b)) { sx += a; sz += b; break; }
    for (int a = -1; a <= 1 && !wg_walkable(tx, tz); a++)
        for (int b = -1; b <= 1; b++) if (wg_walkable(tx + a, tz + b)) { tx += a; tz += b; break; }
    if (!wg_walkable(sx, sz) || !wg_walkable(tx, tz)) return false;
    float *cost = malloc(sizeof(float) * WGX * WGZ);
    int *par = malloc(sizeof(int) * WGX * WGZ);
    if (!cost || !par) { free(cost); free(par); return false; }
    for (int i = 0; i < WGX * WGZ; i++) { cost[i] = 1e30f; par[i] = -1; }
    int s = sz * WGX + sx, t = tz * WGX + tx;
    cost[s] = 0;
    wg_heap_n = 0;
    wg_push(0, s);
    static const int dxs[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dzs[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    bool found = false;
    while (wg_heap_n > 0) {
        WgEntry top = wg_pop();
        if (top.d > cost[top.i]) continue;
        if (top.i == t) { found = true; break; }
        int gx = top.i % WGX, gz = top.i / WGX;
        for (int e = 0; e < 8; e++) {
            int nx = gx + dxs[e], nz = gz + dzs[e];
            if (!wg_walkable(nx, nz)) continue;
            bool diag = e >= 4;
            if (diag && (!wg_walkable(nx, gz) || !wg_walkable(gx, nz))) continue;
            bool road = wg_t[nz][nx] >= 0x10 && wg_t[nz][nx] <= 0x14, from_road = wg_t[gz][gx] >= 0x10 && wg_t[gz][gx] <= 0x14;
            /* road to road at any step: a bridge's deck lies 190 units over its ramps in the tiles (Cape Cod's second
             * leg), and the car drives over it */
            if (!(road && from_road) && fabs(wg_y[nz][nx] - wg_y[gz][gx]) > WG_DY * (diag ? 1.41 : 1.0)) continue;
            float nd = top.d + (float)((diag ? 1.41421356 : 1.0) * (road ? 1 : 8));
            int ni = nz * WGX + nx;
            if (nd >= cost[ni]) continue;
            cost[ni] = nd;
            par[ni] = top.i;
            wg_push(nd, ni);
        }
    }
    if (found) {
        bool trace = env_num("TD3_JOB_CHECK", 1) >= 2;
        int k = 0;
        for (int i = t; par[i] >= 0; i = par[i], k++) {
            int p = par[i], gx = i % WGX, gz = i / WGX;
            double len = ((p % WGX != gx) && (p / WGX != gz) ? 1.41421356 : 1.0) * WG / 100.0;
            if (wg_t[gz][gx] >= 0x10 && wg_t[gz][gx] <= 0x14) *on += len; else *off += len;
            if (trace && k % 25 == 0) fprintf(stderr, "check: path %.0f %.0f type %02X\n", (gx + 0.5) * WG, (gz + 0.5) * WG, wg_t[gz][gx]);
        }
    }
    free(cost);
    free(par);
    return found;
}

static int check_flags;                 /* the leg's TIGHT / TOO SHORT / NO WAY count */

/* the speed a timed job asks on the road (m/s); taxi: from getting in, no walk at the end */
static double check_timed(int type, int from, int t, const char *tag)
{
    Job o;
    memset(&o, 0, sizeof o);
    o.type = type;
    delivery_terms(&o, from, t, type == JOB_TAXI ? 1.15 : 1.0, 1.0);
    double on, off;
    if (!check_way(bld[from].dX, bld[from].dZ, bld[t].dX, bld[t].dZ, &on, &off)) {
        fprintf(stderr, "check:     %-7s -> house %2d %-16s NO WAY ON THE GRID\n", tag, t, house_village(t));
        check_flags++;
        return 99;
    }
    double spare = o.t_left - (type == JOB_TAXI ? 0 : 15) - off / 5.0, need = spare > 1 ? on / spare : 99;
    const char *flag = need > 17 ? "  TOO SHORT" : need > 13 ? "  TIGHT" : "";
    if (*flag) check_flags++;
    fprintf(stderr, "check:     %-7s -> house %2d %-16s %4.0f s: road %5.0f m (cells %5.0f), off it %3.0f m -> %4.1f m/s%s\n",
            tag, t, house_village(t), o.t_left, on, house_route(from, t), off, need, flag);
    return need;
}

static void job_check(void)
{
    if (!SDL_getenv("TD3_JOB_CHECK")) return;
    check_flags = 0;
    int garages = 0;
    for (int i = 0; i < nbld; i++) garages += bld[i].sv == SV_GARAGE;
    for (int from = 0; from < nbld; from++) {
        const Bld *f = &bld[from];
        if (f->sv != SV_PUB && f->sv != SV_POLICE) continue;
        fprintf(stderr, "check: %s house %d (%s), door %s%.0f m on foot from the main road\n", sv_name[f->sv], from,
                house_village(from), f->road < 1e8 ? "" : "NOT ", f->road < 1e8 ? f->road / 100 : 0.0);
        if (!house_reachable(from)) { fprintf(stderr, "check:   THE SERVICE ITSELF CANNOT BE REACHED\n"); check_flags++; }
        int cand[NBLD], nc;
        if (f->sv == SV_PUB) {
            nc = pick_list(from, 12000, 70000, -1, cand);
            fprintf(stderr, "check:   PARCEL / EXPRESS / HOT / TAXI: %d goals%s\n", nc, nc ? "" : "  NONE");
            if (!nc) check_flags++;
            for (int k = 0; k < nc; k++) {
                check_timed(JOB_PARCEL, from, cand[k], "PARCEL");
                check_timed(JOB_EXPRESS, from, cand[k], "EXPRESS");
                check_timed(JOB_TAXI, from, cand[k], "TAXI");
            }
            /* the race: as make_offer */
            double here = to_finish(f->rcell);
            int nr = 0;
            for (int i = 0; i < nbld && here > 0; i++) {
                const Bld *b = &bld[i];
                if (b->vil == f->vil || !house_reachable(i)) continue;
                double m = house_route(from, i), there = to_finish(b->rcell);
                if (m < 300 || m > 1300 || there < 0 || there > here - 200) continue;
                nr++;
                fprintf(stderr, "check:     RACE    -> house %2d %-16s %4.0f m by road\n", i, house_village(i), m);
            }
            fprintf(stderr, "check:   RACE: %d goals ahead%s (finish %s)\n", nr, nr ? "" : " (a parcel instead)",
                    here > 0 ? "by road" : "NOT BY ROAD");
            /* the getaway: every loot, its drops; a garage to lose the police by */
            int nl = pick_list(from, 12000, 60000, -1, cand), pairs = 0, no_drop = 0;
            for (int k = 0; k < nl; k++) {
                int drops[NBLD], nd = pick_list(cand[k], 15000, 70000, from, drops);
                pairs += nd;
                if (!nd) no_drop++;
                double on, off;
                bool ok = check_way(f->dX, f->dZ, bld[cand[k]].dX, bld[cand[k]].dZ, &on, &off);
                fprintf(stderr, "check:     LOOT    -> house %2d %-16s road %5.0f m, off it %3.0f m%s; %d drops\n", cand[k],
                        house_village(cand[k]), on, off, ok ? "" : "  NO WAY ON THE GRID", nd);
                if (!ok) check_flags++;
            }
            fprintf(stderr, "check:   GETAWAY: %d loots, %d loot-drop pairs%s; %d garage(s) on the leg\n", nl, pairs,
                    no_drop ? "  (a loot with no drop)" : "", garages);
        } else {
            nc = pick_list(from, 20000, 70000, -1, cand);
            fprintf(stderr, "check:   BOUNTY / STOLEN CAR: %d goals%s\n", nc, nc ? "" : "  NONE");
            if (!nc) check_flags++;
            for (int k = 0; k < nc; k++) {
                const Bld *b = &bld[cand[k]];
                double gx = b->dX + b->nx * 500, gz = b->dZ + b->nz * 500, on, off;
                bool gang = walk_at(gx, gz, NULL) < 1e8;
                bool ok = check_way(f->dX, f->dZ, b->dX, b->dZ, &on, &off);
                fprintf(stderr, "check:     BOUNTY  -> house %2d %-16s road %5.0f m, off it %3.0f m; the gang %s%s\n", cand[k],
                        house_village(cand[k]), on, off, gang ? "before the door" : "AT the door (no way 5 m out)",
                        ok ? "" : "  NO WAY ON THE GRID");
                if (!ok) check_flags++;
            }
        }
    }
    fprintf(stderr, "check: scene %d leg %d: %d flag(s)\n", DSW(DS_scene_index), DSB(DS_leg_index), check_flags);
}

/* a car of the job let go (the rival, the stolen car): its flags back, moving on */
static void job_release_car(void)
{
    if (job.car > 0) {
        weapon_hold(job.car, false);
        if (job.car_flags) DSW(DS_obj_flags + 2 * job.car) = job.car_flags;
    }
    job.car = 0;
}

static void job_end(const char *msg, bool good)
{
    if (!job.type) return;
    job_release_car();
    if (good) {
        static const char *const why[JOB_N] = { "", "DELIVERED", "DELIVERED", "DELIVERED", "BOUNTY", "RACE WON", "GETAWAY",
                                                "CAR RETURNED", "FARE" };
        enh_money_add(job.reward, why[job.type]);
        centre_msg(msg, 0x80FF80, 3.0);
    } else { centre_msg(msg, 0xFF6050, 3.0); pcm_sfx_play(PCM_DENY, 0.7, 0); }
    if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "job: %s (%s)\n", msg, job_name[job.type]);
    memset(&job, 0, sizeof job);
}

static void job_paid(const char *what)
{
    char t[48];
    snprintf(t, sizeof t, "%s +$%d", what, job.reward);
    job_end(t, true);
}

static void accept_job(int kind)
{
    job = offer[kind];
    memset(&offer[kind], 0, sizeof offer[kind]);                     /* a new one next time */
    job.car = 0;
    if (job.type == JOB_HOT) enh_foes_hot(2);                        /* the police know at once */
    pcm_sfx_play(PCM_PICKUP, 0.8, 0);
    if (SDL_getenv("TD3_FOES_DEBUG"))
        fprintf(stderr, "job: %s to %s (house %d), $%d, %.0f s\n", job_name[job.type], job_where(&job), job.target, job.reward, job.t_left);
}

/* an object's place, true x4 (compare it with TX / TZ, never with the wrapped px / pz: they part 655 m east-west) */
static void obj_place(int o, double *x, double *z)
{
    *x = DSW(DS_obj_x + 2 * o) * 4.0;
    *z = DSW(DS_obj_z + 2 * o) * 4.0;
}

/* where the arrow points now (false: none), its label and the goal's road cell (-1: the one nearest it) */
static bool job_goal(double *x, double *z, const char **label, int *cell)
{
    const Bld *b = job.target >= 0 && job.target < nbld ? &bld[job.target] : NULL;
    if (!b) return false;
    *x = b->dX; *z = b->dZ;
    *cell = b->rcell;
    *label = "DROP";
    switch (job.type) {
    case JOB_BOUNTY:
        if (job.stage == 2) { *x = bld[job.from].dX; *z = bld[job.from].dZ; *cell = bld[job.from].rcell; *label = "POLICE"; return true; }
        *x = job.gx; *z = job.gz; *label = "GANG";
        return true;
    case JOB_RACE:
        if (job.stage == 1 && job.car > 0) { obj_place(job.car, x, z); *cell = -1; *label = "RIVAL"; return true; }
        *label = "FINISH";
        return job.stage == 2;
    case JOB_TAXI:
        return job.stage == 1;
    case JOB_GETAWAY:
        if (job.stage == 0) { *x = bld[job.target2].dX; *z = bld[job.target2].dZ; *cell = bld[job.target2].rcell; *label = "LOOT"; }
        return true;
    case JOB_RECOVER:
        if (job.stage >= 1 && job.car > 0) { obj_place(job.car, x, z); *cell = -1; *label = job.stage == 1 ? "STOLEN" : "CAR"; return true; }
        *label = "THEFT";
        return true;
    default: return true;
    }
}

/* where road crosses the edge e of road cell c (its middle), true x4 */
static void edge_point(int c, int e, double *x, double *z)
{
    int col = c & 31, zc = 15 - (c >> 5);
    double along = (redge[c][e][0] + redge[c][e][1]) / 2;
    if (e == 0)      { *x = (col + 1) * 4096.0; *z = zc * 4096.0 + along; }
    else if (e == 1) { *x = col * 4096.0;       *z = zc * 4096.0 + along; }
    else if (e == 2) { *x = col * 4096.0 + along; *z = (zc + 1) * 4096.0; }
    else             { *x = col * 4096.0 + along; *z = zc * 4096.0; }
}

/* The job's arrow along the road (TOWNS.md "Jobs"; true x4, as the goals; the camera's wrapped place would turn a goal
 * over 327 m east or west round): from the player's road cell the way by road to the goal's cell (a breadth-first
 * search back from the goal), the arrow at its crossing 50 m on or more; within two cells of the goal by road, 120 m
 * straight, or off the road network: straight at the goal. *road_m: the way's length (metres; -1 straight). */
static void arrow_aim(double gx, double gz, int gcell, double *ax, double *az, double *road_m)
{
    *ax = gx; *az = gz; *road_m = -1;
    if (gcell < 0) gcell = road_cell_near(gx, gz);
    int col = (int)floor(TX / 4096.0), zc = (int)floor(TZ / 4096.0), pc = -1;
    if (col >= 0 && col <= 31 && zc >= 0 && zc <= 15 && rnode[col + (15 - zc) * 32]) pc = col + (15 - zc) * 32;
    if (pc < 0) pc = road_cell_near(TX, TZ);
    if (gcell < 0 || pc < 0 || hypot(gx - TX, gz - TZ) < 12000) return;
    static int dist[512], queue[512];
    for (int i = 0; i < 512; i++) dist[i] = -1;
    int qh = 0, qt = 0;
    dist[gcell] = 0; queue[qt++] = gcell;
    while (qh < qt && dist[pc] < 0) {
        int c = queue[qh++];
        for (int e = 0; e < 4; e++) {
            int d = road_joined(c, e);
            if (d < 0 || dist[d] >= 0) continue;
            dist[d] = dist[c] + 1;
            queue[qt++] = d;
        }
    }
    if (dist[pc] < 0) return;                                        /* off the network: straight */
    *road_m = dist[pc] * 40.96 + 20;
    if (dist[pc] <= 2) return;
    for (int c = pc, step = 0; step < 8 && dist[c] > 0; step++) {
        int next = -1, ne = -1;
        for (int e = 0; e < 4 && next < 0; e++) {
            int d = road_joined(c, e);
            if (d >= 0 && dist[d] == dist[c] - 1) { next = d; ne = e; }
        }
        if (next < 0) break;
        edge_point(c, ne, ax, az);
        if (hypot(*ax - TX, *az - TZ) > 5000 || dist[next] == 0) break;
        c = next;
    }
}

/* where a ring shows the door to walk into (false: none) */
static bool job_ring(int *house)
{
    switch (job.type) {
    case JOB_PARCEL: case JOB_EXPRESS: case JOB_HOT: case JOB_TAXI: *house = job.target; return job.type != JOB_TAXI || job.stage == 1;
    case JOB_GETAWAY: *house = job.stage == 0 ? job.target2 : job.target; return true;
    default: return false;
    }
}

/* the walker in the ring before house i's door */
static bool at_door(const EnhSnap *c, int i, bool foot, double feet)
{
    Bld *b = &bld[i];
    if (fabs(b->dX - TX) > 30000 || fabs(b->dZ - TZ) > 30000) return false;
    double dx = wraps(b->dX - px), dz = wraps(b->dZ - pz), d = sqrt(dx * dx + dz * dz);
    if (d < 9000) {                                                  /* the ground at the door, once it is built */
        double g = enh_ground_height(c, b->dX, b->dZ, b->y + 80, 160);
        if (g > -1e8) { b->gy = g; b->gy_ok = true; }
    }
    return foot && d < DOOR_R && fabs(feet - b->gy) < 90;
}

/* a moving vehicle the job may take: no police, no train, not wrecked or held; the opponents only if `opp` */
static bool job_car_ok(int o, bool opp)
{
    if (o <= 0 || o >= DSW(DS_obj_a475) || o >= 160) return false;
    u16 f = DSW(DS_obj_flags + 2 * o);
    u8 m = (u8)(f & 0x3F);
    if (!(f & 0x2000) || m == 0x11 || m == 0x30 || (m >= 0x12 && m <= 0x14)) return false;
    if (weapon_wrecked(o) || weapon_frozen((u16)(2 * o))) return false;
    bool is_opp = DSB(DS_race_computer_cars) != 0 && o <= 2;
    return opp || !is_opp;
}

static void job_frame(const EnhSnap *c, bool foot, double feet, double dt)
{
    if (!job.type) return;
    if (job.target < 0 || job.target >= nbld) { memset(&job, 0, sizeof job); return; }
    Bld *b = &bld[job.target];
    job.t += dt;
    if (job.t_left > 0 && !(job.type == JOB_TAXI && job.stage == 0)) {
        job.t_left -= dt;
        if (job.t_left <= 0) { job_end("TOO LATE - JOB FAILED", false); return; }
    }
    double dx = wraps(b->dX - px), dz = wraps(b->dZ - pz), d = sqrt(dx * dx + dz * dz);
    bool near = fabs(b->dX - TX) < 30000 && fabs(b->dZ - TZ) < 30000;
    switch (job.type) {
    case JOB_PARCEL: case JOB_EXPRESS: case JOB_HOT:
        if (at_door(c, job.target, foot, feet)) job_paid("DELIVERED!");
        return;

    case JOB_TAXI: {                                                 /* the fare gets in with you; no crashes; stop at the door */
        bool crashed = DSB(DS_crashed) != 0;
        if (job.stage == 0) { if (!foot) { job.stage = 1; centre_msg("YOUR FARE IS IN. DRIVE CAREFULLY", 0xF0E070, 2.5); } }
        else {
            if (crashed && !job.crashed) { job_end("YOUR FARE JUMPED OUT - JOB FAILED", false); return; }
            if (!foot && near && d < b->road + 2500 && DSW(DS_car_speed) < 5) job_paid("THANKS FOR THE RIDE!");
        }
        job.crashed = crashed;
        return;
    }

    case JOB_GETAWAY:
        if (job.stage == 0) {
            if (at_door(c, job.target2, foot, feet)) {
                job.stage = 1;
                enh_foes_hot(3);
                centre_msg("GOT THE LOOT - LOSE THE COPS!", 0xFF6050, 3.0);
            }
            return;
        }
        if (at_door(c, job.target, foot, feet)) {
            if (enh_foes_stars() == 0) job_paid("CLEAN GETAWAY!");
            else if (cmsg.t_left <= 0) centre_msg("TOO HOT - LOSE THE COPS FIRST", 0xFF6050, 2.0);
        }
        return;

    case JOB_BOUNTY: {                                               /* the gang near its place; back to the police to be paid */
        if (job.stage == 0) {
            double gx = wraps(job.gx - px), gz = wraps(job.gz - pz);
            if (near && gx * gx + gz * gz < 9000.0 * 9000.0 && enh_foes_gang(c, job.gx, job.gz, job.gy)) job.stage = 1;
            return;
        }
        if (job.stage == 1) {
            int left = enh_foes_gang_left();
            if (left == 0) { job.stage = 2; centre_msg("GANG DOWN - BACK TO THE POLICE", 0x80FF80, 3.0); }
            else if (left < 0) job.stage = 0;                        /* left behind: they wait there again */
            return;
        }
        if (at_door(c, job.from, foot, feet)) job_paid("BOUNTY COLLECTED!");
        return;
    }

    case JOB_RACE: {
        if (job.stage == 0) { if (!foot) { job.stage = 1; job.t = 0; } return; }
        if (job.stage == 1) {
            /* the rival: a car within 60 m (the opponent first, else one of the traffic going towards the goal) waits
             * on the road, the arrow on it; side by side with it (15 m) and slow, the count down, then GO. Before, the
             * nearest car within 150 m was taken wherever it was, and the player never saw whom he raced */
            if (foot) return;
            if (job.car > 0) {
                double ox, oz;
                obj_place(job.car, &ox, &oz);
                double e = hypot(ox - TX, oz - TZ);
                if (e > 25000) { job_release_car(); job.cd = -1; return; }    /* driven off: another one */
                if (job.cd < 0) {
                    if ((e < 1500 && DSW(DS_car_speed) < 20) || env_num("TD3_JOB_GOTO", 0) > 0) {   /* the test: at once */
                        job.cd = 0;
                        centre_msg("ON YOUR MARKS", 0xF0E070, 1.0);
                    }
                    return;
                }
                if (e > 2500) { job.cd = -1; return; }                  /* pulled away early */
                job.cd += dt;
            }
            if (!job.car) {
                int best = 0;
                double bd = 6000.0 * 6000.0;
                int n = DSW(DS_obj_a475);
                for (int o = 1; o < n && o < 160; o++) {
                    if (!job_car_ok(o, true)) continue;
                    double ox, oz;
                    obj_place(o, &ox, &oz);
                    double ex = ox - TX, ez = oz - TZ, e = ex * ex + ez * ez;            /* true x4 */
                    bool opp = DSB(DS_race_computer_cars) != 0 && o <= 2;
                    if (!opp) {                                      /* traffic: heading towards the goal */
                        double h = (u16)(DSW(DS_obj_heading + 2 * o) + 0x4000) * A2RAD;
                        double tx = b->dX - ox, tz = b->dZ - oz, tl = sqrt(tx * tx + tz * tz);
                        if (tl < 1 || (sin(h) * tx + cos(h) * tz) / tl < 0.3) continue;
                    }
                    if (opp) e *= 0.25;                              /* the opponent first */
                    if (e < bd) { bd = e; best = o; }
                }
                if (!best) {
                    if (job.t > 90) job_end("NOBODY SHOWED UP - RACE OFF", false);
                    return;
                }
                job.car = best;
                job.car_flags = DSW(DS_obj_flags + 2 * best);
                weapon_hold(best, true);
                job.cd = -1;
                centre_msg("YOUR RIVAL IS WAITING", 0xF0E070, 2.0);
                if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "job: rival %d\n", best);
                return;
            }
            if (job.cd > 3.0) {                                      /* go */
                job.stage = 2;
                weapon_hold(job.car, false);
                bool opp = DSB(DS_race_computer_cars) != 0 && job.car <= 2;
                if (!opp) DSW(DS_obj_flags + 2 * job.car) = (u16)(job.car_flags | 0x00C0);   /* the fastest speed class */
                centre_msg("GO!", 0x80FF80, 1.2);
                pcm_sfx_play(PCM_PICKUP, 1.0, 0);
            }
            return;
        }
        /* the race: the first within 30 m (the rival: of the road there) of the goal wins */
        if (near && d < b->road + 3000) { job_paid("YOU WON THE RACE!"); return; }
        if (job.car > 0) {
            if (weapon_wrecked(job.car)) { job_paid("YOUR RIVAL IS OUT!"); return; }
            double ox, oz;
            obj_place(job.car, &ox, &oz);
            double ex = b->dX - ox, ez = b->dZ - oz;
            if (sqrt(ex * ex + ez * ez) < b->road + 2500) { job_end("YOUR RIVAL WON - RACE LOST", false); return; }
        }
        if (job.t > 300) job_end("RACE OFF", false);
        return;
    }

    case JOB_RECOVER: {
        if (job.stage == 0) {                                        /* at the village the thief drives off */
            if (!near || d > 15000) return;
            int best = 0;
            double bd = (job.t > 40 ? 40000.0 : 25000.0) * (job.t > 40 ? 40000.0 : 25000.0);
            int n = DSW(DS_obj_a475);
            for (int o = 1; o < n && o < 160; o++) {
                if (!job_car_ok(o, false)) continue;
                double ox, oz;
                obj_place(o, &ox, &oz);
                double ex = ox - TX, ez = oz - TZ, e = ex * ex + ez * ez;            /* true x4 */
                if (e < bd) { bd = e; best = o; }
            }
            if (!best) {
                static double dbg_t;
                if (SDL_getenv("TD3_FOES_DEBUG") && (dbg_t += dt) > 2) {
                    dbg_t = 0;
                    double nd = 1e18;
                    int nn = 0, all = 0;
                    for (int o = 1; o < n && o < 160; o++) {
                        if (DSW(DS_obj_flags + 2 * o) & 0x2000) all++;
                        if (!job_car_ok(o, false)) continue;
                        nn++;
                        double ox, oz;
                        obj_place(o, &ox, &oz);
                        nd = fmin(nd, hypot(ox - TX, oz - TZ));
                    }
                    fprintf(stderr, "job: no car to steal yet (%d moving, %d free, nearest %.0f)\n", all, nn, nd);
                }
                return;
            }
            job.car = best;
            job.car_flags = DSW(DS_obj_flags + 2 * best);
            DSW(DS_obj_flags + 2 * best) = (u16)(job.car_flags | 0x00C0);   /* away, as fast as it goes */
            u16 w = DSW(DS_obj_waypoint + 2 * best);
            if (!(w & 0xFFC0)) DSW(DS_obj_waypoint + 2 * best) = (u16)(w + 0x40);   /* a parked one wakes */
            job.stage = 1;
            job.t = 0;
            centre_msg("THERE IT GOES - STOP IT!", 0xFF6050, 2.5);
            if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "job: stolen car %d\n", best);
            return;
        }
        if (weapon_wrecked(job.car)) { job_end("YOU WRECKED IT - JOB FAILED", false); return; }
        double ox, oz;
        obj_place(job.car, &ox, &oz);
        double ex = ox - TX, ez = oz - TZ, e = sqrt(ex * ex + ez * ez);   /* true x4 */
        if (job.stage == 1) {
            if (e > 50000) { job_end("IT GOT AWAY - JOB FAILED", false); return; }
            if (weapon_hits(job.car) >= 2.0) {                       /* two pistol rounds stop it (five wreck it) */
                weapon_hold(job.car, true);
                DSW(DS_obj_flags + 2 * job.car) = job.car_flags;
                job.car_flags = 0;
                job.stage = 2;
                centre_msg("IT STOPPED - THE THIEF RUNS. GET TO THE CAR", 0x80FF80, 3.0);
            }
            return;
        }
        if (e < 600) job_paid("CAR RECOVERED!");
        return;
    }
    default: return;
    }
}

static void item(const char *label, int kind, int arg, int arg2, int price, const char *right)
{
    if (menu.n >= 8) return;
    Item *t = &menu.it[menu.n++];
    snprintf(t->label, sizeof t->label, "%s", label);
    t->kind = kind; t->arg = arg; t->arg2 = arg2; t->price = price;
    if (right) snprintf(t->right, sizeof t->right, "%s", right);
    else if (price > 0) snprintf(t->right, sizeof t->right, "$%d", price);
    else t->right[0] = 0;
    t->hint[0] = 0;
}

/* the job line of the pub's or the police station's menu: the offer, or the job running */
static void job_item(int kind)
{
    if (job.type) {
        item(job.type == JOB_BOUNTY ? "BOUNTY ON" : "JOB ON", IT_INFO, 0, 0, 0, NULL);
        snprintf(menu.it[menu.n - 1].hint, sizeof menu.it[0].hint, "%s: %s", job_name[job.type], job_where(&job));
        return;
    }
    if (!offer[kind].type && !make_offer(kind, menu.b)) return;
    const Job *o = &offer[kind];
    char label[32], pay[16];
    snprintf(label, sizeof label, "JOB: %s", job_name[o->type]);
    snprintf(pay, sizeof pay, "+$%d", o->reward);                      /* paid, not a price */
    item(label, IT_JOB, kind, 0, o->reward, pay);
    Item *t = &menu.it[menu.n - 1];
    switch (o->type) {
    case JOB_BOUNTY: snprintf(t->hint, sizeof t->hint, "A GANG HIDES AT %s", job_where(o)); break;
    case JOB_RECOVER: snprintf(t->hint, sizeof t->hint, "A CAR WAS STOLEN AT %s", job_where(o)); break;
    case JOB_RACE: snprintf(t->hint, sizeof t->hint, "A LOCAL RACES YOU TO %s", job_where(o)); break;
    case JOB_GETAWAY: snprintf(t->hint, sizeof t->hint, "LOOT AT %s", house_village(o->target2)); break;
    case JOB_TAXI: snprintf(t->hint, sizeof t->hint, "A FARE TO %s IN %d:%02d", job_where(o), (int)o->t_left / 60, (int)o->t_left % 60); break;
    default: snprintf(t->hint, sizeof t->hint, "TO %s IN %d:%02d", job_where(o), (int)o->t_left / 60, (int)o->t_left % 60); break;
    }
}

static void build_items(void)
{
    menu.n = 0;
    bool own;
    int rounds, room;
    switch (bld[menu.b].sv) {
    case SV_GUNS:
        enh_gun_state(0, &own, &rounds, &room);
        item("PISTOL ROUNDS X24", IT_AMMO, 0, 24, 30, room <= 0 ? "FULL" : NULL);
        enh_gun_state(1, &own, &rounds, &room);
        if (own) item("UZI ROUNDS X64", IT_AMMO, 1, 64, 80, room <= 0 ? "FULL" : NULL);
        else item("UZI + 64 ROUNDS", IT_WEAPON, 1, 64, 350, NULL);
        enh_gun_state(2, &own, &rounds, &room);
        if (own) item("SHOTGUN SHELLS X16", IT_AMMO, 2, 16, 60, room <= 0 ? "FULL" : NULL);
        else item("SHOTGUN + 16 SHELLS", IT_WEAPON, 2, 16, 400, NULL);
        item("BODY ARMOUR", IT_ARMOUR, 100, 0, 150, enh_foes_armour() > 99 ? "FULL" : NULL);
        break;
    case SV_PUB:
        job_item(0);
        item("BEER", IT_FOOD, 10, 1, 5, NULL);
        item("BURGER AND FRIES", IT_FOOD, 35, 0, 15, NULL);
        item("STEAK DINNER", IT_FOOD, 100, 0, 40, NULL);
        break;
    case SV_GARAGE:
        item("ARMOUR PLATING (CAR)", IT_CAR_ARMOUR, 0, 0, 500, car_armour ? "FITTED" : NULL);
        item("RESPRAY: DRIVE IN", IT_INFO, 0, 0, 0, "$100/*");
        break;
    case SV_POLICE: {
        int st = enh_foes_stars();
        char t[32];
        snprintf(t, sizeof t, "PAY YOUR FINE");
        item(t, IT_FINE, 0, 0, st * FINE_STAR, st ? NULL : "NONE");
        job_item(1);
        snprintf(t, sizeof t, "$%d", ENH_BAIL);
        item("BAIL IF BUSTED", IT_INFO, 0, 0, 0, t);
        break;
    }
    default: break;
    }
    item("LEAVE", IT_LEAVE, 0, 0, 0, NULL);
    if (menu.sel >= menu.n) menu.sel = menu.n - 1;
}

static void note(const char *t, bool good)
{
    snprintf(menu.note, sizeof menu.note, "%s", t);
    menu.note_rgb = good ? 0x80FF80 : 0xFF6050;
    menu.note_t = 2.5;
    if (!good) pcm_sfx_play(PCM_DENY, 0.7, 0);
}

static void close_menu(void)
{
    if (!menu.open) return;
    menu.open = false;
    host_menu(false);
}

static void open_menu(int i)
{
    const Bld *b = &bld[i];
    if (b->sv != SV_POLICE && enh_foes_danger(px, pz)) {
        centre_msg("CLOSED - TROUBLE OUTSIDE", 0xFF6050, 2.0);
        pcm_sfx_play(PCM_DENY, 0.6, 0);
        return;
    }
    memset(&menu, 0, sizeof menu);
    menu.open = true;
    menu.b = i;
    menu.hp = enh_foes_health();
    build_items();
    host_menu(true);
    pcm_sfx_play(PCM_PICKUP, 0.7, 0);
    if (SDL_getenv("TD3_FOES_DEBUG")) fprintf(stderr, "town: %s opened (house %d)\n", sv_name[b->sv], i);
}

static void buy(const Item *t)
{
    bool own;
    int rounds, room;
    switch (t->kind) {
    case IT_LEAVE: close_menu(); return;
    case IT_JOB:
        if (job.type) { note("ONE JOB AT A TIME", false); return; }
        accept_job(t->arg);
        {
            static const char *const say[JOB_N] = { "", "DON'T BE LATE", "DON'T BE LATE", "THE COPS KNOW. HURRY!", "GO GET THEM",
                                                    "GET IN YOUR CAR", "PICK IT UP. QUIETLY.", "FIND THAT CAR", "YOUR FARE WAITS AT THE CAR" };
            note(say[job.type], true);
        }
        break;
    case IT_INFO:
        if (!strcmp(t->label, "JOB ON") || !strcmp(t->label, "BOUNTY ON")) { note("FINISH THE ONE YOU HAVE", true); return; }
        if (bld[menu.b].sv == SV_GARAGE) note("DRIVE INTO THE BAY OUTSIDE", true);
        else note("PAID HERE OR AT ARREST", true);
        return;
    case IT_AMMO: case IT_WEAPON:
        enh_gun_state(t->arg, &own, &rounds, &room);
        if (room <= 0) { note("YOU CAN'T CARRY MORE", false); return; }
        if (!enh_money_pay(t->price, t->label)) { note("NOT ENOUGH MONEY", false); return; }
        enh_gun_pickup(t->arg, t->arg2);
        note(t->kind == IT_WEAPON ? "ENJOY IT. RESPONSIBLY." : "THERE YOU GO", true);
        break;
    case IT_ARMOUR:
        if (enh_foes_armour() > 99) { note("YOU'RE COVERED ALREADY", false); return; }
        if (!enh_money_pay(t->price, "ARMOUR")) { note("NOT ENOUGH MONEY", false); return; }
        enh_foes_add_armour(t->arg);
        note("STRAPPED IN", true);
        break;
    case IT_FOOD:
        if (!t->arg2 && enh_foes_health() > 99.5) { note("YOU'RE NOT HUNGRY", false); return; }
        if (!enh_money_pay(t->price, t->label)) { note("NOT ENOUGH MONEY", false); return; }
        enh_foes_heal(t->arg);
        if (t->arg2) { beer += 1.0; note(beer > 2.5 ? "WHOA, EASY THERE" : "CHEERS!", true); }
        else note("ENJOY YOUR MEAL", true);
        break;
    case IT_FINE: {
        int st = enh_foes_stars();
        if (!st) { note("NOTHING ON YOU. YET.", true); return; }
        if (!enh_money_pay(st * FINE_STAR, "FINE")) { note("NOT ENOUGH MONEY", false); return; }
        enh_foes_clear_wanted();
        note("PAID. STAY OUT OF TROUBLE.", true);
        break;
    }
    case IT_CAR_ARMOUR:
        if (car_armour) { note("ALREADY FITTED", false); return; }
        if (!enh_money_pay(t->price, "PLATING")) { note("NOT ENOUGH MONEY", false); return; }
        car_armour = true;
        note("YOUR CAR IS PLATED", true);
        break;
    default: break;
    }
    menu.hp = enh_foes_health();
    build_items();
}

static void respray(void)
{
    int st = enh_foes_stars();
    if (st == 0) { centre_msg("GARAGE - GET OUT FOR PARTS", sv_rgb[SV_GARAGE], 2.5); return; }
    if (enh_foes_police_see()) {
        centre_msg("NOT WITH THE COPS WATCHING!", 0xFF6050, 2.5);
        pcm_sfx_play(PCM_DENY, 0.7, 0);
        return;
    }
    char t[48];
    if (!enh_money_pay(st * SPRAY_STAR, "RESPRAY")) {
        snprintf(t, sizeof t, "RESPRAY $%d - NOT ENOUGH MONEY", st * SPRAY_STAR);
        centre_msg(t, 0xFF6050, 2.5);
        pcm_sfx_play(PCM_DENY, 0.7, 0);
        return;
    }
    spray_t = 1.8;
    pcm_sfx_play(PCM_SPRAY, 0.9, 0);
    enh_foes_clear_wanted();
    centre_msg("NEW PAINT - NOBODY KNOWS YOU", sv_rgb[SV_GARAGE], 3.0);
}

/* ------------------------------------------------------------------------------------------------------
 * Once per displayed frame
 * ------------------------------------------------------------------------------------------------------ */

void enh_town_frame(const EnhSnap *c, const EnhView *v, double dt)
{
    (void)v;
    if (dt < 0 || dt > 0.25) dt = 0.25;
    u32 key = leg_key();
    if (key != scan_key) {                                               /* a new leg */
        scan_key = key;
        scan();
        close_menu();
        last_vil = -1;
        goto_done = false;
        memset(offer, 0, sizeof offer);
        if (job.type) job_end("JOB LOST", false);
        if (SDL_getenv("TD3_TOWN_DUMP")) enh_town_dump();
        job_check();
        {   /* developer aid TD3_JOB=1..4 (parcel, express, hot goods, bounty): a job at once, from the nearest pub
             * (police station) */
            int want = (int)env_num("TD3_JOB", 0);
            if (want >= 1 && want < JOB_N) {
                int kind = want == JOB_BOUNTY || want == JOB_RECOVER, from = -1;
                double bd = 1e18, tx = (u32)DSL(DS_pos_x) / 32.0, tz = (u32)DSL(DS_pos_z) / 32.0;
                for (int i = 0; i < nbld; i++) {
                    if (bld[i].sv != (kind ? SV_POLICE : SV_PUB)) continue;
                    double d = hypot(bld[i].X - tx, bld[i].Z - tz);
                    if (d < bd) { bd = d; from = i; }
                }
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TD3_JOB_TYPE", SDL_getenv("TD3_JOB"), true);
                for (int tries = 0; tries < 40 && from >= 0; tries++)
                    if (make_offer(kind, from) && offer[kind].type == want) { accept_job(kind); break; }
                SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "TD3_JOB_TYPE");
            }
        }
    }
    /* the race's money: afresh with a new race, a prize per leg, tickets and the hospital cost */
    if (!counters_ok || weapon_races() != races_seen) {
        if (counters_ok || weapon_races() > 0) {
            money = (int)env_num("TD3_MONEY", START_MONEY);
            car_armour = false;
            memset(mline, 0, sizeof mline);
        }
        counters_ok = true;
        races_seen = weapon_races();
        legs_seen = weapon_legs_done();
        deaths_seen = weapon_deaths();
        tickets_seen = DSB(DS_tickets);
    }
    if (weapon_legs_done() != legs_seen) {
        int n = (int)(weapon_legs_done() - legs_seen);
        legs_seen = weapon_legs_done();
        enh_money_add(LEG_PRIZE * n, "LEG PRIZE");
    }
    if (weapon_deaths() != deaths_seen) {
        deaths_seen = weapon_deaths();
        if (job.type) job_end("JOB FAILED", false);
        charge(HOSPITAL, "HOSPITAL");
        beer = 0;
        close_menu();
    }
    int tk = DSB(DS_tickets);
    if (tk < tickets_seen) tickets_seen = tk;                            /* a new leg */
    if (tk > tickets_seen) { charge(TICKET_FINE * (tk - tickets_seen), "TICKET"); tickets_seen = tk; }

    for (int i = 0; i < 3; i++) mline[i].age += dt;
    if (money_show > 0) money_show -= dt;
    if (cmsg.t_left > 0) cmsg.t_left -= dt;
    if (banner_t > 0) banner_t -= dt;
    if (spray_t > 0) spray_t -= dt;
    if (menu.note_t > 0) menu.note_t -= dt;
    beer_t += dt;
    if (beer > 0) beer = fmax(0, beer - dt / 25.0);

    {   /* developer aid TD3_FACES_LOG=1: the game's vertex and face buffers (1584 / 1600) once a second */
        static double flog_t;
        if (SDL_getenv("TD3_FACES_LOG") && (flog_t += dt) > 1.0) {
            flog_t = 0;
            fprintf(stderr, "faces: %u vertices, %u faces (static %u / %u)\n", DSW(DS_vert_count), DSW(DS_face_count),
                    DSW(DS_static_vert_end), DSW(DS_static_face_end));
        }
    }
    px = c->cam_x4; pz = c->cam_z4; py = c->cam_y;
    TX = (u32)DSL(DS_pos_x) / 32.0; TZ = (u32)DSL(DS_pos_z) / 32.0;
    {   /* developer aid TD3_POS_LOG=1: the player's place (true x4) and the car's speed once a second */
        static double plog_t;
        if (SDL_getenv("TD3_POS_LOG") && (plog_t += dt) > 1.0) {
            plog_t = 0;
            fprintf(stderr, "pos: %.0f %.0f speed %u\n", TX, TZ, DSW(DS_car_speed));
        }
    }
    bool foot = foot_active();
    double feet = foot ? py - 70 : py - 46;

    /* developer aid: TD3_TOWN_GOTO=n (1 garage, 2 guns, 3 pub, 4 police): out of the car, the walker is put
     * before the nearest such door (TD3_TOWN_GOTO=n,bay: at the garage's bay) */
    const char *gt = SDL_getenv("TD3_TOWN_GOTO");
    if (gt && foot && !goto_done) {
        int want = SDL_atoi(gt), bi = -1;
        double bd = 1e18;
        for (int i = 0; i < nbld; i++) {
            if (bld[i].sv != want) continue;
            double d = hypot(bld[i].X - TX, bld[i].Z - TZ);
            if (d < bd) { bd = d; bi = i; }
        }
        goto_done = true;
        if (bi >= 0) {
            const Bld *b = &bld[bi];
            bool bay = strstr(gt, "bay") != NULL;
            double gx = bay ? b->gX : b->dX + b->nx * 260, gz = bay ? b->gZ : b->dZ + b->nz * 260;
            foot_teleport(bay ? gx + b->nz * 260 : gx, bay ? gz - b->nx * 260 : gz, b->y + 2);
            if (bay) {                                                   /* and the car next to the walker */
                u16 h = (u16)(atan2(-b->nx, -b->nz) / A2RAD);
                foot_move_car(gx, gz, b->y, h);
            }
            foot_set_view(atan2(-b->nx, -b->nz) / A2RAD, 0);
            fprintf(stderr, "town: walker put before the %s of house %d (%s)\n", sv_name[b->sv], bi, vil[b->vil].name);
        }
    }
    /* developer aid TD3_OBJ_TOUR=dir,model: once out of the car, the walker visits every object of that model, 5 m
     * east of it looking at it, and a picture of each is saved (dir/obj_mMM_NNN.bmp, NNN the object's slot) */
    const char *otour = SDL_getenv("TD3_OBJ_TOUR");
    if (otour && foot && strchr(otour, ',')) {
        static int oi = 0, phase = 0;
        static double ot, ocx = -1, ocz;
        int want = SDL_atoi(strchr(otour, ',') + 1);
        if (ocx < 0) { ocx = TX; ocz = TZ; }
        ot += dt;
        while (oi < DSW(DS_obj_count) && oi < 160 && (oi < 1 || (DSW(DS_obj_flags + 2 * oi) & 0x3F) != want)) oi++;
        if (oi < DSW(DS_obj_count) && oi < 160) {
            double gx = DSW(DS_obj_x + 2 * oi) * 4.0 + 500, gz = DSW(DS_obj_z + 2 * oi) * 4.0;
            if (phase == 0) {                                            /* walking there, 20 m a frame */
                double dx = gx - ocx, dz = gz - ocz, d = hypot(dx, dz);
                if (d <= 2000) { ocx = gx; ocz = gz; phase = 1; ot = 0; }
                else { ocx += dx / d * 2000; ocz += dz / d * 2000; }
                int wx = (int)floor(ocx / WG), wz = (int)floor(ocz / WG);
                double gy = DSW(DS_obj_y8 + 2 * oi) >> 3;
                if (wx >= 0 && wx < WGX && wz >= 0 && wz < WGZ && wg_y[wz][wx] > -1e8f) gy = wg_y[wz][wx];
                foot_teleport(ocx, ocz, gy + 4);
                foot_set_view(atan2(-1.0, 0.0) / A2RAD, -20);
            } else if (ot > 1.2) {
                char path[600];
                SDL_snprintf(path, sizeof path, "%.*s/obj_m%02d_%03d.bmp", (int)(strchr(otour, ',') - otour), otour, want, oi);
                host_snapshot_to(path);
                fprintf(stderr, "objtour: %s\n", path);
                oi++;
                phase = 0;
            }
        }
    }
    /* developer aid TD3_TOWN_TOUR=dir: once out of the car, the walker visits every house of the leg in turn and a
     * picture of each is saved from two places: the road nearest its door, at least 7 m from the house (or
     * TD3_TOWN_TOUR_R out from the door with no road within 30 m), looking at it (dir/tour_sS_lL_hNNa.bmp) and
     * 30 m out, 40 degrees to the side (...b.bmp); the feet on the walk grid's ground there. It goes there 20 m a
     * frame, as a driver would (TOWNS.md "Developer aids") */
    const char *tour = SDL_getenv("TD3_TOWN_TOUR");
    if (tour && foot) {
        if (menu.open) close_menu();                                     /* walked through a ring on the way */
        static int ti = -1;
        static double tt, cx, cz, gxt, gzt, yaw;
        static bool shot, there;
        tt += dt;
        if (ti < 0 || (there && tt > 1.6)) {
            if (ti < 0) { ti = 2 * (int)env_num("TD3_TOWN_TOUR_FROM", 0) - 1; cx = TX; cz = TZ; }
            ti++;
            if (SDL_getenv("TD3_TOWN_TOUR_NEW"))                         /* only the added houses */
                while (ti < 2 * nbld && bld[ti / 2].obj >= 0) ti++;
            tt = 0;
            shot = there = false;
            if (ti < 2 * nbld) {
                const Bld *b = &bld[ti / 2];
                double r = ti & 1 ? 3000 : env_num("TD3_TOWN_TOUR_R", 1800), a = ti & 1 ? 0.7 : 0.0;
                double ox = b->nx * cos(a) - b->nz * sin(a), oz = b->nx * sin(a) + b->nz * cos(a);
                gxt = b->dX + ox * r;
                gzt = b->dZ + oz * r;
                if (!(ti & 1)) {                                         /* a: from the road nearest the door */
                    double best = 3000 * 3000.0;
                    int g0x = (int)floor(b->dX / WG), g0z = (int)floor(b->dZ / WG);
                    for (int dz = -24; dz <= 24; dz++)
                        for (int dx = -24; dx <= 24; dx++) {
                            int x = g0x + dx, z = g0z + dz;
                            if (x < 0 || x >= WGX || z < 0 || z >= WGZ || wg_t[z][x] < 0x10 || wg_t[z][x] > 0x14) continue;
                            double qx = x * (double)WG - b->dX, qz = z * (double)WG - b->dZ;
                            if (qx * qx + qz * qz < best) { best = qx * qx + qz * qz; gxt = x * (double)WG; gzt = z * (double)WG; }
                        }
                    double d = hypot(gxt - b->X, gzt - b->Z), want = fmax(b->hx, b->hz) + 700;
                    if (d < want && d > 1) { gxt = b->X + (gxt - b->X) / d * want; gzt = b->Z + (gzt - b->Z) / d * want; }
                }
                yaw = atan2(b->X - gxt, b->Z - gzt) / A2RAD;
            } else if (ti == 2 * nbld) fprintf(stderr, "tour: done\n");
        }
        if (ti >= 0 && ti < 2 * nbld && !there) {
            double dx = gxt - cx, dz = gzt - cz, d = hypot(dx, dz);
            if (d <= 2000) { cx = gxt; cz = gzt; there = true; tt = 0; }
            else { cx += dx / d * 2000; cz += dz / d * 2000; }
            double gy = bld[ti / 2].y;
            int gx = (int)floor(cx / WG), gz = (int)floor(cz / WG);
            if (gx >= 0 && gx < WGX && gz >= 0 && gz < WGZ && wg_y[gz][gx] > -1e8f) gy = wg_y[gz][gx];
            foot_teleport(cx, cz, gy + 4);
            foot_set_view(yaw, 0);
        }
        if (ti >= 0 && ti < 2 * nbld && there && !shot && tt > 1.2) {
            char path[600];
            SDL_snprintf(path, sizeof path, "%s/tour_s%d_l%d_h%02d%c.bmp", tour, DSW(DS_scene_index), DSB(DS_leg_index),
                         ti / 2, ti & 1 ? 'b' : 'a');
            host_snapshot_to(path);
            fprintf(stderr, "tour: %s: walker at %.0f %.0f (true), feet %.0f; house %.0f %.0f; camera %.0f %.0f, sprite %u %u, "
                    "obj %d at %u %u flags %04X\n", path, TX, TZ, py - 70, bld[ti / 2].X, bld[ti / 2].Z, px, pz,
                    DSW(DS_sprite_x) * 4, DSW(DS_sprite_z) * 4, bld[ti / 2].obj,
                    bld[ti / 2].obj > 0 ? DSW(DS_obj_x + 2 * bld[ti / 2].obj) * 4 : 0,
                    bld[ti / 2].obj > 0 ? DSW(DS_obj_z + 2 * bld[ti / 2].obj) * 4 : 0,
                    bld[ti / 2].obj > 0 ? DSW(DS_obj_flags + 2 * bld[ti / 2].obj) : 0);
            shot = true;
        }
    }

    /* the services: the door walked into, the garage's bay driven into. A job's door (its ring, the bounty's police
     * station) opens no shop while the job wants it: a leg with only services left for goals (Pacific's fifth: a
     * getaway's loot at the gun shop, its drop at the garage) opened the shop's menu over the job, and the menu holds
     * the walker; walked out and in again, the shop opens */
    int job_door = -1;
    if (job.type && !job_ring(&job_door)) job_door = -1;
    if (job.type == JOB_BOUNTY && job.stage == 2) job_door = job.from;
    for (int i = 0; i < nbld; i++) {
        Bld *b = &bld[i];
        if (!b->sv) continue;
        if (fabs(b->X - TX) > 30000 || fabs(b->Z - TZ) > 30000) { b->armed = b->armed_car = true; b->gy_ok = false; continue; }
        double dx = wraps(b->dX - px), dz = wraps(b->dZ - pz), d = sqrt(dx * dx + dz * dz);
        if (d < 9000) {                                                  /* the ground at the door, once it is built */
            double g = enh_ground_height(c, b->dX, b->dZ, b->y + 80, 160);
            if (g > -1e8) { b->gy = g; b->gy_ok = true; }
        }
        if (foot) {
            if (d > DOOR_R * 2.4) b->armed = true;
            else if (i == job_door) b->armed = false;
            if (d < DOOR_R && b->armed && !menu.open && fabs(feet - b->gy) < 90) { b->armed = false; open_menu(i); }
        } else if (b->sv == SV_GARAGE) {
            double gx = wraps(b->gX - px), gz = wraps(b->gZ - pz), dg = sqrt(gx * gx + gz * gz);
            if (dg > BAY_R * 2.5) b->armed_car = true;
            if (dg < BAY_R && b->armed_car && DSW(DS_car_speed) < 12) { b->armed_car = false; respray(); }
        }
    }

    job_frame(c, foot, feet, dt);                                        /* TOWNS.md "Jobs" */
    {   /* developer aid TD3_JOB_GOTO=1: out of the car, the walker is put 3 m before the job's goal, once a stage (a
         * bounty's gang: 40 m off; the taxi and the race: the car too, beside the walker); TD3_JOB_SHOOT=1: the stolen
         * car takes two hits 2 s after it drove off */
        static int jg_stage = -1, jg_type;
        if (!job.type) jg_stage = -1;
        if (job.type && (job.stage != jg_stage || job.type != jg_type) && foot && env_num("TD3_JOB_GOTO", 0) > 0 &&
            !(job.type == JOB_RECOVER && job.stage == 1) && !(job.type == JOB_BOUNTY && job.stage == 1)) {
            jg_stage = job.stage; jg_type = job.type;
            const Bld *b = &bld[job.target];
            double gx, gz;
            const char *lab;
            int gcell;
            if (!job_goal(&gx, &gz, &lab, &gcell)) { gx = b->dX; gz = b->dZ; }
            if (job.type == JOB_GETAWAY && job.stage == 0) b = &bld[job.target2];
            if (job.type == JOB_BOUNTY && job.stage == 2) b = &bld[job.from];
            double off = job.type == JOB_BOUNTY && job.stage == 0 ? 4000 : job.type == JOB_RECOVER && job.stage == 0 ? 6000 : 300;
            foot_teleport(gx + b->nx * off, gz + b->nz * off, b->y + 2);
            foot_set_view(atan2(-b->nx, -b->nz) / A2RAD, 0);
            if (job.type == JOB_TAXI || job.type == JOB_RACE) {
                u16 h = (u16)(atan2(-b->nz, b->nx) / A2RAD);
                foot_move_car(gx + b->nx * 300 + b->nz * 300, gz + b->nz * 300 - b->nx * 300, b->y, h);
            }
            fprintf(stderr, "job: walker put before the goal (%s, stage %d)\n", job_where(&job), job.stage);
        }
        static double js_t;
        if ((job.type == JOB_RECOVER || job.type == JOB_BOUNTY) && job.stage == 1 && env_num("TD3_JOB_SHOOT", 0) > 0) {
            if ((js_t += dt) > 2.0) {                                    /* the stolen car: two hits; the gang: down */
                if (job.type == JOB_RECOVER) { weapon_hit(job.car, 1.0); weapon_hit(job.car, 1.0); }
                else for (int i = 0; i < 3; i++) enh_foes_damage(i, 999);
                js_t = -1e9;
            }
        } else js_t = 0;
    }

    /* the shop's menu */
    if (menu.open) {
        if (!foot || enh_foes_health() < menu.hp - 0.5) close_menu();     /* hit at the counter: out */
        else {
            int bits = host_menu_input();
            if (bits & HOST_MENU_UP) { menu.sel = (menu.sel + menu.n - 1) % menu.n; pcm_sfx_play(PCM_TICK, 0.5, 0); }
            if (bits & HOST_MENU_DOWN) { menu.sel = (menu.sel + 1) % menu.n; pcm_sfx_play(PCM_TICK, 0.5, 0); }
            if (bits & HOST_MENU_OK) buy(&menu.it[menu.sel]);
            if (bits & HOST_MENU_BACK) close_menu();
        }
    }

    /* a village's name, coming in */
    int near_v = -1;
    double nd = 1e18;
    for (int k = 0; k < nvil; k++) {
        if (vil[k].nb < 2 && vil[k].nsv == 0) continue;
        double d = hypot(vil[k].X - TX, vil[k].Z - TZ);
        if (d < nd) { nd = d; near_v = k; }
    }
    if (near_v >= 0 && nd < 4500 && near_v != last_vil) {
        last_vil = near_v;
        banner_vil = near_v;
        banner_t = 4.0;
    }
    if (last_vil >= 0 && hypot(vil[last_vil].X - TX, vil[last_vil].Z - TZ) > 10000) last_vil = -1;
}

/* ------------------------------------------------------------------------------------------------------
 * Signs and markers in the world
 * ------------------------------------------------------------------------------------------------------ */

#define NPOLY 400
static EnhPoly polys[NPOLY];
static int npoly;

static inline u32 scale_rgb(u32 rgb, double k)
{
    int r = (int)(((rgb >> 16) & 0xFF) * k), g = (int)(((rgb >> 8) & 0xFF) * k), b = (int)((rgb & 0xFF) * k);
    r = r > 255 ? 255 : r < 0 ? 0 : r; g = g > 255 ? 255 : g < 0 ? 0 : g; b = b > 255 ? 255 : b < 0 ? 0 : b;
    return (u32)r << 16 | (u32)g << 8 | (u32)b;
}

/* a world quad (x4, height, z4: wrapped) as a polygon, drawn where nearer than the scene (with `slack`) */
static void quad(const double (*w)[3], u32 rgb, double slack, int S, int offx, int offy)
{
    if (npoly >= NPOLY) return;
    EnhPoly *P = &polys[npoly];
    double cx = 0, cy = 0, cz = 0;
    for (int k = 0; k < 4; k++) {
        double tx, ty, dist;
        if (!enh_front_point(w[k][0], w[k][1], w[k][2], &tx, &ty, &dist)) return;
        P->x[k] = (tx + offx) * S; P->y[k] = (ty + offy) * S;
        cx += w[k][0] / 4; cy += w[k][1] / 4; cz += w[k][2] / 4;
    }
    double tx, ty, d;
    if (!enh_front_point(cx, cy, cz, &tx, &ty, &d)) return;
    P->n = 4;
    P->z = d;
    P->zd = (float)(d - slack);
    P->rgb = rgb;
    npoly++;
}

/* the front view's depth at a target pixel (view pixels), or a large number */
static double depth_at(double tx, double ty)
{
    const EnhTarget *t = &enh_front;
    int A = t->k > 0 ? t->k : 1, sx = (int)(tx * A), sy = (int)(ty * A);
    if (!t->z || sx < 0 || sy < 0 || sx >= t->w || sy >= t->h) return 1e18;
    return t->z[(size_t)sy * t->w + sx];
}

typedef struct { double tx, ty, dist, w, h; int sv; } SignText;

void enh_town_draw(u32 *xrgb, const EnhSnap *s, const EnhView *v, int em, int offx, int offy, int y0, int y1)
{
    if (s->menu_preview || !enh_keys_racing() || !nbld) return;
    int S = enh_scale;
    npoly = 0;
    SignText st[16];
    int nst = 0;
    double camx = v->cam_x4, camz = v->cam_z4, camy = v->cam_y;
    double TXv = (u32)DSL(DS_pos_x) / 32.0, TZv = (u32)DSL(DS_pos_z) / 32.0;
    for (int i = 0; i < nbld; i++) {
        const Bld *b = &bld[i];
        if (!b->sv) continue;
        if (fabs(b->X - TXv) > 16000 || fabs(b->Z - TZv) > 16000) continue;
        double ddx = wraps(b->dX - camx), ddz = wraps(b->dZ - camz), dd = sqrt(ddx * ddx + ddz * ddz);
        if (dd > 14000) continue;
        u32 col = sv_rgb[b->sv];
        double g = b->gy + 1.5;
        /* the marker: a ring on the ground before the door (and the garage's bay, larger) */
        for (int ring = 0; ring < (b->sv == SV_GARAGE ? 2 : 1); ring++) {
            double mx = ring ? b->gX : b->dX, mz = ring ? b->gZ : b->dZ, r = ring ? BAY_R : DOOR_R, ri = r * (ring ? 0.86 : 0.70);
            u32 rc = scale_rgb(col, s->day ? (ring ? 0.75 : 0.85) : 0.6);     /* paint on the ground, no light */
            const int NS = ring ? 28 : 18;
            if (!ring && menu.open && menu.b == i) continue;             /* standing in it, at the counter */
            for (int k = 0; k < NS; k++) {
                if (ring && (k & 1)) continue;                           /* the bay: dashed */
                double a0 = k * 2 * M_PI / NS, a1 = (k + 1) * 2 * M_PI / NS;
                double sx = wraps(mx + sin((a0 + a1) / 2) * r - camx), sz = wraps(mz + cos((a0 + a1) / 2) * r - camz);
                if (sx * sx + sz * sz < 160.0 * 160.0) continue;          /* under the eye: no way to project it */
                double w[4][3] = {
                    { mx + sin(a0) * r, g, mz + cos(a0) * r }, { mx + sin(a1) * r, g, mz + cos(a1) * r },
                    { mx + sin(a1) * ri, g, mz + cos(a1) * ri }, { mx + sin(a0) * ri, g, mz + cos(a0) * ri } };
                for (int q = 0; q < 4; q++) { w[q][0] = camx + wraps(w[q][0] - camx); w[q][2] = camz + wraps(w[q][2] - camz); }
                quad(w, rc, 40, S, offx, offy);
            }
        }
        /* the sign over the door, on the wall, if the camera is before it */
        double wx = b->dX - b->nx * (DOOR_OUT - 4), wz = b->dZ - b->nz * (DOOR_OUT - 4);
        double cx = wraps(camx - wx), cz = wraps(camz - wz);
        if (cx * b->nx + cz * b->nz <= 0) continue;
        double hw = fmin(fmax(b->half * 0.7, 70), 110), hy0 = fmin(b->top * 0.62, 95), hy1 = hy0 + 30;
        double tx_ = -b->nz, tz_ = b->nx;                                /* along the wall */
        double base = b->y;
        double w[4][3] = {
            { wx - tx_ * hw, base + hy0, wz - tz_ * hw }, { wx + tx_ * hw, base + hy0, wz + tz_ * hw },
            { wx + tx_ * hw, base + hy1, wz + tz_ * hw }, { wx - tx_ * hw, base + hy1, wz - tz_ * hw } };
        for (int q = 0; q < 4; q++) { w[q][0] = camx + wraps(w[q][0] - camx); w[q][2] = camz + wraps(w[q][2] - camz); }
        bool night = !s->day;
        quad(w, scale_rgb(col, night ? 0.55 : 0.40), 25, S, offx, offy);
        /* its frame, a little out */
        double fw = hw + 5, f0 = hy0 - 3, f1 = hy1 + 3;
        double wf[4][3] = {
            { wx - b->nx * 1 - tx_ * fw, base + f0, wz - b->nz * 1 - tz_ * fw }, { wx - b->nx * 1 + tx_ * fw, base + f0, wz - b->nz * 1 + tz_ * fw },
            { wx - b->nx * 1 + tx_ * fw, base + f1, wz - b->nz * 1 + tz_ * fw }, { wx - b->nx * 1 - tx_ * fw, base + f1, wz - b->nz * 1 - tz_ * fw } };
        for (int q = 0; q < 4; q++) { wf[q][0] = camx + wraps(wf[q][0] - camx); wf[q][2] = camz + wraps(wf[q][2] - camz); }
        int before = npoly;
        quad(wf, 0x1A1A1A, 25, S, offx, offy);
        if (npoly > before) polys[npoly - 1].z += 3;                     /* behind the board */
        double ttx, tty, tdist, ltx, lty, ldist, rtx, rty, rdist;
        double mxp = camx + wraps(wx - camx), mzp = camz + wraps(wz - camz);
        if (nst < 16 && enh_front_point(mxp, base + (hy0 + hy1) / 2, mzp, &ttx, &tty, &tdist) &&
            enh_front_point(w[0][0], w[0][1], w[0][2], &ltx, &lty, &ldist) &&
            enh_front_point(w[1][0], w[1][1], w[1][2], &rtx, &rty, &rdist) &&
            depth_at(ttx, tty) > tdist - 40) {
            st[nst].tx = ttx; st[nst].ty = tty; st[nst].dist = tdist; st[nst].w = fabs(rtx - ltx); st[nst].sv = b->sv;
            double htx, hty, hd;
            st[nst].h = enh_front_point(w[3][0], w[3][1], w[3][2], &htx, &hty, &hd) ? fabs(hty - lty) : 0;
            nst++;
        }
        (void)camy;
    }
    /* TOWNS.md "Jobs": the door to deliver to, a pale yellow ring */
    int ring_house;
    if (job.type && job_ring(&ring_house) && ring_house >= 0 && ring_house < nbld) {
        const Bld *b = &bld[ring_house];
        if (fabs(b->X - TXv) < 16000 && fabs(b->Z - TZv) < 16000) {
            double mx = b->dX, mz = b->dZ, r = DOOR_R, ri = r * 0.7, g = b->gy + 1.5;
            for (int k = 0; k < 18; k++) {
                double a0 = k * 2 * M_PI / 18, a1 = (k + 1) * 2 * M_PI / 18;
                double sx = wraps(mx + sin((a0 + a1) / 2) * r - camx), sz = wraps(mz + cos((a0 + a1) / 2) * r - camz);
                if (sx * sx + sz * sz < 160.0 * 160.0) continue;
                double w[4][3] = {
                    { mx + sin(a0) * r, g, mz + cos(a0) * r }, { mx + sin(a1) * r, g, mz + cos(a1) * r },
                    { mx + sin(a1) * ri, g, mz + cos(a1) * ri }, { mx + sin(a0) * ri, g, mz + cos(a0) * ri } };
                for (int q = 0; q < 4; q++) { w[q][0] = camx + wraps(w[q][0] - camx); w[q][2] = camz + wraps(w[q][2] - camz); }
                quad(w, s->day ? 0xF0E070 : 0x9A9048, 40, S, offx, offy);
            }
        }
    }
    enh_polys(xrgb, em, polys, npoly, offx, offy, y0, y1);
    /* the signs' words, as large as fit */
    int FW = (320 + 2 * em) * S, FH = 200 * S;
    for (int k = 0; k < nst && !menu.open; k++) {
        const char *t = sv_name[st[k].sv];
        double wv = st[k].w;                                             /* the board's width, VGA pixels */
        int sc = (int)floor(fmin(wv * 0.85 / enh_text_width(t, 1), st[k].h * 0.8 / 7.0));
        if (sc < 1) continue;
        if (sc > 3) sc = 3;
        int x = (int)(st[k].tx + offx) - enh_text_width(t, sc) / 2, y = (int)(st[k].ty + offy) - (7 * sc) / 2;
        if (y < y0 || y + 7 * sc > y1) continue;
        int tw = enh_text_width(t, sc);
        if (!enh_draw_mask_shows(x, y) || !enh_draw_mask_shows(x + tw, y) || !enh_draw_mask_shows(x, y + 7 * sc) ||
            !enh_draw_mask_shows(x + tw, y + 7 * sc)) continue;                  /* not over the cockpit */
        enh_text(xrgb, FW, FH, S, t, x, y, sc, 0xF8F8F0, false);
    }
}

/* ------------------------------------------------------------------------------------------------------
 * The money, the village's name, the menu
 * ------------------------------------------------------------------------------------------------------ */

static inline u32 mixc(u32 a, u32 b, double w)
{
    int r = (int)(((a >> 16) & 0xFF) * (1 - w) + ((b >> 16) & 0xFF) * w);
    int g = (int)(((a >> 8) & 0xFF) * (1 - w) + ((b >> 8) & 0xFF) * w);
    int c = (int)((a & 0xFF) * (1 - w) + (b & 0xFF) * w);
    return (u32)r << 16 | (u32)g << 8 | (u32)c;
}

/* a box over the frame (VGA pixels of the frame), mixed with rgb by w */
static void box(u32 *xrgb, int FW, int FH, int S, int x0, int y0, int x1, int y1, u32 rgb, double w)
{
    for (int y = y0 * S; y < y1 * S; y++)
        for (int x = x0 * S; x < x1 * S; x++)
            if (x >= 0 && y >= 0 && x < FW && y < FH) xrgb[(size_t)y * FW + x] = mixc(xrgb[(size_t)y * FW + x], rgb, w);
}

void enh_town_hud(u32 *xrgb, const EnhSnap *s, const EnhView *v, int em)
{
    if (s->menu_preview || !enh_keys_racing()) return;
    int S = enh_scale, FW = (320 + 2 * em) * S, FH = 200 * S;
    /* the garage's spray: a mist over everything, clearing */
    if (spray_t > 0) {
        double k = fmin(1.0, spray_t / 1.2) * 0.85;
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FW; x++) {
                u32 n = (u32)(x * 7919 + y * 104729 + (int)(spray_t * 30) * 31337);
                n ^= n >> 13; n *= 0x5BD1E995u; n ^= n >> 15;
                double w = k * (0.75 + 0.25 * ((n & 0xFF) / 255.0));
                xrgb[(size_t)y * FW + x] = mixc(xrgb[(size_t)y * FW + x], 0xE8ECF0, w);
            }
    }
    /* the money: on foot always, in the car a while after it changed */
    bool show = s->foot || money_show > 0 || menu.open;
    if (show) {
        char t[24];
        snprintf(t, sizeof t, "$%d", money);
        int sc = 1, x = em + 320 - 8 - enh_text_width(t, sc), y = s->foot ? 22 : 30;
        enh_text(xrgb, FW, FH, S, t, x, y, sc, 0x70E070, true);
        for (int i = 0; i < 3; i++) {
            if (!mline[i].t[0] || mline[i].age > 3.5 || menu.open) continue;
            int lx = em + 320 - 8 - enh_text_width(mline[i].t, 1);
            enh_text(xrgb, FW, FH, S, mline[i].t, lx, y + 12 + i * 9, 1, mline[i].d >= 0 ? 0x80FF80 : 0xFF7060, false);
        }
    }
    /* TOWNS.md "Jobs": what to do, how long, and an arrow to the goal (only while a job runs) */
    if (job.type && !menu.open && job.target >= 0 && job.target < nbld) {
        /* short (at most about 34 characters: between the original's boxes at the top) */
        char t[72], tm[16] = "", sub[40] = "";
        if (job.t_left > 0) snprintf(tm, sizeof tm, "  %d:%02d", (int)job.t_left / 60, (int)job.t_left % 60);
        switch (job.type) {
        case JOB_BOUNTY:
            if (job.stage == 2) snprintf(t, sizeof t, "BACK TO POLICE: %s", house_village(job.from));
            else if (job.stage == 1) snprintf(t, sizeof t, "GANG: %d LEFT", enh_foes_gang_left() > 0 ? enh_foes_gang_left() : 0);
            else snprintf(t, sizeof t, "GANG AT %s", job_where(&job));
            break;
        case JOB_RACE:
            if (job.stage == 0) snprintf(t, sizeof t, "RACE > %s: GET IN", job_where(&job));
            else if (job.stage == 1 && !job.car) snprintf(t, sizeof t, "RACE > %s: NO RIVAL YET", job_where(&job));
            else if (job.stage == 1 && job.cd < 0) snprintf(t, sizeof t, "RACE: PULL UP BESIDE YOUR RIVAL");
            else if (job.stage == 1) snprintf(t, sizeof t, "RACE > %s: READY", job_where(&job));
            else {
                double ox, oz;
                obj_place(job.car, &ox, &oz);
                const Bld *b = &bld[job.target];
                double me = hypot(b->dX - TX, b->dZ - TZ), him = hypot(b->dX - ox, b->dZ - oz);   /* true x4 all */
                snprintf(t, sizeof t, "RACE > %s", job_where(&job));
                snprintf(sub, sizeof sub, "RIVAL %dM %s", (int)(fabs(me - him) / 100), him < me ? "AHEAD" : "BEHIND");
            }
            break;
        case JOB_GETAWAY:
            if (job.stage == 0) snprintf(t, sizeof t, "LOOT AT %s", house_village(job.target2));
            else snprintf(t, sizeof t, enh_foes_stars() ? "COPS! DROP > %s" : "DROP THE LOOT > %s", job_where(&job));
            break;
        case JOB_RECOVER:
            if (job.stage == 0) snprintf(t, sizeof t, "STOLEN CAR AT %s", job_where(&job));
            else if (job.stage == 1) snprintf(t, sizeof t, "STOP IT - DON'T WRECK IT");
            else snprintf(t, sizeof t, "GET TO THE CAR");
            break;
        case JOB_TAXI:
            if (job.stage == 0) snprintf(t, sizeof t, "TAXI: GET IN%s", tm);
            else snprintf(t, sizeof t, "TAXI > %s%s", job_where(&job), tm);
            break;
        default: snprintf(t, sizeof t, "%s > %s%s", job.type == JOB_EXPRESS ? "EXPRESS" : job_name[job.type], job_where(&job), tm); break;
        }
        if (sub[0]) enh_text(xrgb, FW, FH, S, sub, em + 160 - enh_text_width(sub, 1) / 2, 44, 1, 0xF0E070, true);
        bool late = job.t_left > 0 && job.t_left < 15 && fmod(job.t_left, 0.6) < 0.3;
        enh_text(xrgb, FW, FH, S, t, em + 160 - enh_text_width(t, 1) / 2, 8, 1, late ? 0xFF6050 : 0xF0E070, true);
        if (job.type == JOB_RACE && job.stage == 1 && job.car && job.cd >= 0 && job.cd < 3) {   /* the count down */
            char cd[4];
            snprintf(cd, sizeof cd, "%d", 3 - (int)job.cd);
            enh_text(xrgb, FW, FH, S, cd, em + 160 - enh_text_width(cd, 3) / 2, 60, 3, 0xF0E070, true);
        }
        double gx, gz;
        const char *lab;
        int gcell;
        if (job_goal(&gx, &gz, &lab, &gcell)) {
            double d = hypot(gx - TX, gz - TZ), ax, az, road_m;           /* true x4: see arrow_aim */
            arrow_aim(gx, gz, gcell, &ax, &az, &road_m);
            if (road_m > 0) d = road_m * 100;
            double dx = ax - TX, dz = az - TZ;
            static u64 arrow_log;
            if (SDL_getenv("TD3_FOES_DEBUG") && SDL_GetTicks() > arrow_log) {
                arrow_log = SDL_GetTicks() + 1000;
                fprintf(stderr, "arrow: %s at %.0f %.0f (%.0f m straight), player %.0f %.0f, aim %.0f %.0f (%.0f m on), "
                        "%s %.0f m\n", lab, gx, gz, hypot(gx - TX, gz - TZ) / 100, TX, TZ, ax, az, hypot(dx, dz) / 100,
                        road_m > 0 ? "by road" : "straight", d / 100);
            }
            if (d > 600) {
                double a = atan2(dx, dz) - v->heading * A2RAD, sa = sin(a), ca = cos(a);
                double qx = (em + 160) * S, qy = 100.0 * S, rr = 84.0 * S;
                double tx = qx + sa * rr, ty = qy - ca * rr * 0.66, bx = qx + sa * (rr - 8 * S), by = qy - ca * (rr - 8 * S) * 0.66;
                double ox = -ca * 6 * S, oy = -sa * 6 * S * 0.8;
                enh_line(xrgb, em, bx + ox, by + oy, tx, ty, 2.0 * S, 0xF0E070, 0.9);
                enh_line(xrgb, em, bx - ox, by - oy, tx, ty, 2.0 * S, 0xF0E070, 0.9);
                char l2[24];
                snprintf(l2, sizeof l2, "%s %dM", lab, (int)(d / 100));
                int lx = (int)(tx / S) - enh_text_width(l2, 1) / 2, ly = (int)(ty / S) + (ca > 0 ? 6 : -12);
                enh_text(xrgb, FW, FH, S, l2, lx, ly, 1, 0xF0E070, false);
            }
        }
    }
    /* the village's name, coming in */
    if (banner_t > 0 && banner_vil >= 0 && banner_vil < nvil && !menu.open) {
        const Vil *V = &vil[banner_vil];
        double a = fmin(1.0, banner_t / 0.6);
        u32 white = mixc(0x000000, 0xF4F0E0, a);
        enh_text(xrgb, FW, FH, S, V->name, em + 160 - enh_text_width(V->name, 2) / 2, 46, 2, white, true);
    }
    if (cmsg.t_left > 0)
        enh_text(xrgb, FW, FH, S, cmsg.t, em + 160 - enh_text_width(cmsg.t, 1) / 2, 132, 1, cmsg.rgb, true);
    /* the menu */
    if (menu.open) {
        const Bld *b = &bld[menu.b];
        u32 col = sv_rgb[b->sv];
        int x0 = em + 56, x1 = em + 264, yt = 46, yi = yt + 22, step = 12, yend = yi + menu.n * step + 4;
        int yn = yend + 4, yh = yn + 12, yb = yh + 12;
        box(xrgb, FW, FH, S, x0, yt, x1, yb, 0x000000, 0.82);
        box(xrgb, FW, FH, S, x0, yt, x1, yt + 18, col, 0.55);
        enh_text(xrgb, FW, FH, S, sv_name[b->sv], x0 + 8, yt + 2, 2, 0xFFFFFF, false);
        char t[24];
        snprintf(t, sizeof t, "$%d", money);
        enh_text(xrgb, FW, FH, S, t, x1 - 8 - enh_text_width(t, 1), yt + 6, 1, 0x90FF90, false);
        const char *vn = b->vil >= 0 ? vil[b->vil].name : "";
        (void)vn;
        for (int i = 0; i < menu.n; i++) {
            const Item *it = &menu.it[i];
            int y = yi + i * step;
            bool sel = i == menu.sel;
            if (sel) box(xrgb, FW, FH, S, x0 + 3, y - 3, x1 - 3, y + 9, col, 0.35);
            u32 tc = it->kind == IT_INFO ? 0x9A9A9A : sel ? 0xFFFFFF : 0xD0D0D0;
            if (sel) enh_text(xrgb, FW, FH, S, ">", x0 + 6, y, 1, 0xFFFFFF, false);
            enh_text(xrgb, FW, FH, S, it->label, x0 + 14, y, 1, tc, false);
            if (it->right[0]) {
                bool afford = it->price <= money || it->price == 0;
                u32 rc = it->kind == IT_INFO ? 0x9A9A9A : it->kind == IT_JOB || afford ? 0x90FF90 : 0xFF7060;
                if (!strcmp(it->right, "FULL") || !strcmp(it->right, "FITTED") || !strcmp(it->right, "NONE")) rc = 0x9A9A9A;
                enh_text(xrgb, FW, FH, S, it->right, x1 - 8 - enh_text_width(it->right, 1), y, 1, rc, false);
            }
        }
        if (menu.note_t > 0) enh_text(xrgb, FW, FH, S, menu.note, em + 160 - enh_text_width(menu.note, 1) / 2, yn, 1, menu.note_rgb, false);
        else if (menu.sel >= 0 && menu.sel < menu.n && menu.it[menu.sel].hint[0]) {     /* the job's where and when */
            const char *h2 = menu.it[menu.sel].hint;
            enh_text(xrgb, FW, FH, S, h2, em + 160 - enh_text_width(h2, 1) / 2, yn, 1, 0xF0E070, false);
        }
        const char *h = "A/SPACE BUY   B/E LEAVE";
        enh_text(xrgb, FW, FH, S, h, em + 160 - enh_text_width(h, 1) / 2, yh, 1, 0x8A8A8A, false);
    }
}
