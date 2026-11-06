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

#define NBLD 64
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
    double road;                        /* from the door to the nearest road face */
    int vil, sv, obj;
    double gy;                          /* the ground at the door (the base until seen) */
    bool gy_ok, armed, armed_car;       /* walked / driven away since the last visit */
} Bld;
typedef struct { double X, Z; int nb, nsv; const char *name; } Vil;

static Bld bld[NBLD];
static int nbld;
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
enum { IT_LEAVE, IT_INFO, IT_AMMO, IT_WEAPON, IT_ARMOUR, IT_FOOD, IT_FINE, IT_CAR_ARMOUR };
typedef struct { char label[32]; char right[16]; int kind, arg, arg2, price; } Item;
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
    u16 fa = (u16)(a + 3 * stride);
    for (int k = 0; k < nf; k++, fa = (u16)(fa + 8)) {
        u16 w0 = rd16(es, fa);
        if ((rd16(es, (u16)(fa + 6)) >> 11) != 1) continue;
        solid++;
        for (int j = 0; j < (w0 >> 14) + 1; j++) {
            int vi = rd16(es, (u16)(fa + 2 * j)) & 0x7FF;
            if (vi < nv && vi < 256) used[vi] = true;
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
}

static void scan(void)
{
    nbld = nvil = 0;
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
    /* the door: on the side nearest a road (not into another house) */
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
            double r = road_dist(dX, dZ), score = r;
            for (int j = 0; j < nbld; j++) if (j != i && inside(&bld[j], dX, dZ, 150)) score += 1e7;
            if (half < 90) score += 5000;                                /* a narrow end: no door there */
            if (score < best) {
                best = score;
                b->nx = nx; b->nz = nz; b->half = half; b->road = r;
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
    for (int i = 0; i < nbld; i++) {                                     /* the houses */
        const Bld *b = &bld[i];
        double zx = -b->az, zz = b->ax;
        for (double u = -b->hx; u <= b->hx; u += U / 2.0)
            for (double v = -b->hz; v <= b->hz; v += U / 2.0) {
                double X = b->X + b->ax * u + zx * v, Z = b->Z + b->az * u + zz * v;
                int xx = (int)(X / U), yy = (int)(H - Z / U);
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                size_t k = (size_t)yy * W + xx;
                img[3 * k] = 200; img[3 * k + 1] = 40; img[3 * k + 2] = 40;
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

void enh_town_dump(void)
{
    const char *flat = SDL_getenv("TD3_TOWN_FLAT");
    if (flat) town_flat(flat);
    const char *map_path = SDL_getenv("TD3_TOWN_MAP");
    if (map_path) town_map(map_path);
    fprintf(stderr, "town: scene %d leg %d: %d houses, %d villages\n", DSW(DS_scene_index), DSB(DS_leg_index), nbld, nvil);
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
                "door %6.0f %6.0f (local %s), road %5.0f\n", i, b->obj, fl & 0x3F, b->obj > 0 ? DSW(DS_obj_heading + 2 * b->obj) >> 8 : 0,
                b->vil, sv_name[b->sv], 2 * b->hx, 2 * b->hz, b->top, b->X, b->Z, b->y, b->dX, b->dZ, side, b->road);
    }
}

/* ------------------------------------------------------------------------------------------------------
 * The shops
 * ------------------------------------------------------------------------------------------------------ */

static void item(const char *label, int kind, int arg, int arg2, int price, const char *right)
{
    if (menu.n >= 8) return;
    Item *t = &menu.it[menu.n++];
    snprintf(t->label, sizeof t->label, "%s", label);
    t->kind = kind; t->arg = arg; t->arg2 = arg2; t->price = price;
    if (right) snprintf(t->right, sizeof t->right, "%s", right);
    else if (price > 0) snprintf(t->right, sizeof t->right, "$%d", price);
    else t->right[0] = 0;
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
    case IT_INFO:
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
        if (SDL_getenv("TD3_TOWN_DUMP")) enh_town_dump();
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

    /* the services: the door walked into, the garage's bay driven into */
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
            if (d < DOOR_R && b->armed && !menu.open && fabs(feet - b->gy) < 90) { b->armed = false; open_menu(i); }
        } else if (b->sv == SV_GARAGE) {
            double gx = wraps(b->gX - px), gz = wraps(b->gZ - pz), dg = sqrt(gx * gx + gz * gz);
            if (dg > BAY_R * 2.5) b->armed_car = true;
            if (dg < BAY_R && b->armed_car && DSW(DS_car_speed) < 12) { b->armed_car = false; respray(); }
        }
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
                u32 rc = it->kind == IT_INFO ? 0x9A9A9A : !afford ? 0xFF7060 : 0x90FF90;
                if (!strcmp(it->right, "FULL") || !strcmp(it->right, "FITTED") || !strcmp(it->right, "NONE")) rc = 0x9A9A9A;
                enh_text(xrgb, FW, FH, S, it->right, x1 - 8 - enh_text_width(it->right, 1), y, 1, rc, false);
            }
        }
        if (menu.note_t > 0) enh_text(xrgb, FW, FH, S, menu.note, em + 160 - enh_text_width(menu.note, 1) / 2, yn, 1, menu.note_rgb, false);
        const char *h = "A/SPACE BUY   B/E LEAVE";
        enh_text(xrgb, FW, FH, S, h, em + 160 - enh_text_width(h, 1) / 2, yh, 1, 0x8A8A8A, false);
    }
}
