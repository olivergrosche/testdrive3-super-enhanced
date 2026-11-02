/* Enhanced renderer (ENHANCED.md): options, the snapshots of the game's frames, the cockpit overlay record,
 * the smooth view state of a displayed frame and the composition over the VGA screen. */
#include "enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../host.h"
#include "game/render.h"                /* OG: obj_emit_vehicle_moving, model_emit_faces (the chase view's car) */

int enh_scale = ENH_DEFAULT_RES_SCALE, enh_aa = ENH_DEFAULT_AA, enh_motion_delay = ENH_DEFAULT_MOTION_DELAY;
int enh_haze = ENH_DEFAULT_HAZE;
int enh_fog_start = ENH_DEFAULT_FOG_START, enh_beam_night = ENH_DEFAULT_BEAM_NIGHT;
int enh_beam_day = ENH_DEFAULT_BEAM_DAY, enh_beam_soft = ENH_DEFAULT_BEAM_SOFT;
bool enh_lights = true;
static bool enabled = true;

static EnhSnap snaps[2];
EnhSnap *enh_prev = &snaps[0], *enh_cur = &snaps[1];
EnhPresent enh_present;
static bool pair_ok;                  /* prev -> cur is a continuous step (interpolate / extrapolate) */

void enh_init(bool on, int res_scale, int aa, int motion_delay, int haze, int draw_dist)
{
    enh_draw_dist = draw_dist < 0 ? 0 : draw_dist > ENH_MAX_DRAW_DIST ? ENH_MAX_DRAW_DIST : draw_dist;
    enh_haze = haze < 0 ? 0 : haze > 100 ? 100 : haze;
    enabled = on;
    enh_scale = res_scale < 1 ? 1 : res_scale > ENH_MAX_RES_SCALE ? ENH_MAX_RES_SCALE : res_scale;
    enh_aa = aa < 1 ? 1 : aa > ENH_MAX_AA ? ENH_MAX_AA : aa;
    while (enh_scale * enh_aa > ENH_MAX_SAMPLES && enh_aa > 1) enh_aa--;   /* keep the sample buffers sane */
    enh_motion_delay = motion_delay < 0 ? 0 : motion_delay > 100 ? 100 : motion_delay;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void enh_init_look(int fog_start, bool lights, int beam_night, int beam_day, int beam_soft)
{
    enh_fog_start = clampi(fog_start, 0, 90);
    enh_lights = lights;                  /* off: the original's beams, ORed into the colours */
    enh_beam_night = clampi(beam_night, 0, 100);
    enh_beam_day = clampi(beam_day, 0, 100);
    enh_beam_soft = clampi(beam_soft, 0, ENH_MAX_BEAM_SOFT);
}

bool enh_enabled(void) { return enabled; }
int  enh_res_scale(void) { return enh_scale; }

/* ------------------------------------------------------------------------------------------------------
 * OG: the chase view (CHASE.md). The game runs its cockpit view underneath; the enhanced view adds the
 * player's car and looks at it from behind, over the whole screen.
 * ------------------------------------------------------------------------------------------------------ */

static bool chase_pref = ENH_CHASE_DEFAULT;
static double chase_look_target;              /* right stick: -1 .. 1 of half a turn */
static struct { bool on; int x0, x1, y0, y1; } msgbox;

/* the camera: distance behind the eye, height over it, horizon row (TD3_CHASE="dist,height,row" to try others) */
static double chase_dist = 720, chase_height = 135, chase_row = 66;
#define CHASE_BEAM_NEAR 190.0                   /* the car's front ahead of the eye, units */
static double chase_yaw_tau = 0.22;           /* s: how fast the camera swings round behind the car */

void enh_chase_set(bool on) { chase_pref = on; }
bool enh_chase_pref(void) { return chase_pref; }
void enh_chase_toggle(void) { chase_pref = !chase_pref; }
void enh_chase_look(double turn) { chase_look_target = turn < -1 ? -1 : turn > 1 ? 1 : turn; }
void enh_msgbox(int x0, int x1, int y0, int y1)
{
    msgbox.on = true; msgbox.x0 = x0; msgbox.x1 = x1; msgbox.y0 = y0; msgbox.y1 = y1;
}
void enh_msgbox_clear(void) { msgbox.on = false; }

static void chase_params(void)
{
    static bool done;
    if (done) return;
    done = true;
    const char *vw = SDL_getenv("TD3_VIEW");             /* developer aid: TD3_VIEW=chase / cockpit to start in it */
    if (vw && !strcmp(vw, "cockpit")) chase_pref = false;
    if (vw && !strcmp(vw, "chase")) chase_pref = true;
    const char *e = SDL_getenv("TD3_CHASE");
    double a, b, c;
    if (e && sscanf(e, "%lf,%lf,%lf", &a, &b, &c) == 3) { chase_dist = a; chase_height = b; chase_row = c; }
}

/* the chase view applies to this frame: a race in the cockpit view (not F5, a replay or a crash) */
static bool chase_now(void)
{
    return chase_pref && enh_keys_racing() && DSB(DS_ext_view) == 0 && DSB(DS_menu_preview) == 0;
}

/* The player's car is not built in the cockpit view (509b skips it). For the chase view it is emitted here,
 * after the game's frame, into the game's arrays past their ends, taken into the snapshot and the game's
 * state put back exactly as it was. */
typedef struct {
    u16 vert_count, face_count, vert_base, model_face_cut, cur_obj_slot1;
    u16 place_x, place_z, place_y, place_x4, place_z4;
    u8  vert_overflow, full_transform, place_rot, place_nverts, place_shear;
    u16 ovb, ovc, ofb, ofc;
} CarSave;
static u8 car_save_v[3][2 * ENH_MAX_VERTS];
static u8 car_save_f[10 * ENH_MAX_FACES];

static void car_restore(const CarSave *k)
{
    u16 nv0 = k->vert_count, nf0 = k->face_count;
    memcpy(mp(DGROUP, (u16)(DS_vert_x + 2 * nv0)), car_save_v[0], (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(mp(DGROUP, (u16)(DS_vert_y + 2 * nv0)), car_save_v[1], (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(mp(DGROUP, (u16)(DS_vert_z + 2 * nv0)), car_save_v[2], (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(mp(DSW(DS_face_block + 2), (u16)(DSW(DS_face_block) + 10 * nf0)), car_save_f, (size_t)10 * (ENH_MAX_FACES - nf0));
    DSW(DS_vert_count) = k->vert_count;   DSW(DS_face_count) = k->face_count;
    DSW(DS_vert_base) = k->vert_base;     DSW(DS_model_face_cut) = k->model_face_cut;
    DSW(DS_cur_obj_slot1) = k->cur_obj_slot1;
    DSW(DS_place_x) = k->place_x;  DSW(DS_place_z) = k->place_z;  DSW(DS_place_y) = k->place_y;
    DSW(DS_place_x4) = k->place_x4; DSW(DS_place_z4) = k->place_z4;
    DSB(DS_vert_overflow) = k->vert_overflow; DSB(DS_obj_full_transform) = k->full_transform;
    DSB(DS_place_rot) = k->place_rot; DSB(DS_place_nverts) = k->place_nverts; DSB(DS_place_shear) = k->place_shear;
    DSW(DS_obj_vert_base) = k->ovb;  DSW(DS_obj_vert_count) = k->ovc;
    DSW(DS_obj_face_base) = k->ofb;  DSW(DS_obj_face_count) = k->ofc;
}

static bool car_emit(CarSave *k)
{
    k->vert_count = DSW(DS_vert_count);   k->face_count = DSW(DS_face_count);
    k->vert_base = DSW(DS_vert_base);     k->model_face_cut = DSW(DS_model_face_cut);
    k->cur_obj_slot1 = DSW(DS_cur_obj_slot1);
    k->place_x = DSW(DS_place_x);  k->place_z = DSW(DS_place_z);  k->place_y = DSW(DS_place_y);
    k->place_x4 = DSW(DS_place_x4); k->place_z4 = DSW(DS_place_z4);
    k->vert_overflow = DSB(DS_vert_overflow); k->full_transform = DSB(DS_obj_full_transform);
    k->place_rot = DSB(DS_place_rot); k->place_nverts = DSB(DS_place_nverts); k->place_shear = DSB(DS_place_shear);
    k->ovb = DSW(DS_obj_vert_base);  k->ovc = DSW(DS_obj_vert_count);
    k->ofb = DSW(DS_obj_face_base);  k->ofc = DSW(DS_obj_face_count);
    u16 nv0 = k->vert_count, nf0 = k->face_count;
    if (nv0 >= 0x600 || nf0 >= 0x600) return false;      /* no room left (the game's arrays end at 630h / 640h) */
    memcpy(car_save_v[0], mp(DGROUP, (u16)(DS_vert_x + 2 * nv0)), (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(car_save_v[1], mp(DGROUP, (u16)(DS_vert_y + 2 * nv0)), (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(car_save_v[2], mp(DGROUP, (u16)(DS_vert_z + 2 * nv0)), (size_t)2 * (ENH_MAX_VERTS - nv0));
    memcpy(car_save_f, mp(DSW(DS_face_block + 2), (u16)(DSW(DS_face_block) + 10 * nf0)), (size_t)10 * (ENH_MAX_FACES - nf0));
    DSB(DS_obj_full_transform) = 1;                       /* not 52a3's translated-vertex cache */
    FarPtr faces;
    u16 nf = obj_emit_vehicle_moving(0, 0, &faces);
    if (DSB(DS_vert_overflow) != 0 || nf == 0) { car_restore(k); return false; }
    DSW(DS_model_face_cut) = 0;
    model_emit_faces(faces, nf);
    if (DSW(DS_face_count) <= nf0) { car_restore(k); return false; }
    return true;
}

/* The cockpit's headlight beams (headlight_beam_draw: two quads at fixed places in V, ORed in) as the patch of
 * ground they show from the cockpit: each corner's line of sight met with the ground under the car (at most
 * 3000 units ahead). Only when the game would draw them (lights on; by day only in rain or snow). */
static void beam_capture(EnhSnap *s)
{
    s->beam_n = 0;
    if (DSB(DS_headlights) == 0) return;
    if (DSB(DS_colour_mode) != 0 && DSW(DS_rain_level) == 0 && DSW(DS_snow_level) == 0) return;
    double ground = (s16)DSW(DS_obj_y8) / 8.0, h = s->cam_y - ground;
    if (h < 8) h = 8;
    double cxa = (double)(s->cx_hi << 8), bb[6], dd[6], dmin = 1e9;
    for (int k = 0; k < 6; k++) {
        double y = (s16)DSW(DS_beam_verts + 4 * k), x = (s16)DSW(DS_beam_verts + 4 * k + 2);
        if (DSB(DS_half_window)) { y -= 0x10; x -= 0x500; }
        bb[k] = (x - cxa + s->heading) * (2.0 * M_PI / 65536.0);
        double e = (s->cam_row - y) * (M_PI / 1024.0);    /* elevation of the line of sight, radians */
        dd[k] = e < -0.015 ? h / tan(-e) : 3000.0;
        if (dd[k] > 3000.0) dd[k] = 3000.0;
        if (dd[k] < dmin) dmin = dd[k];
    }
    /* the near edge, which the dashboard hides in the cockpit, starts at the car's front instead */
    for (int k = 0; k < 6; k++) {
        double d = dd[k] < dmin * 1.25 ? CHASE_BEAM_NEAR : dd[k];
        s->beam_x[k] = s->cam_x4 + d * sin(bb[k]);
        s->beam_z[k] = s->cam_z4 + d * cos(bb[k]);
        s->beam_y[k] = ground + 2;
    }
    s->beam_n = 6;
}

/* the chase camera for a cockpit eye (x4, z4, y, heading): where it is and where it looks */
typedef struct { double x4, z4, y, yaw; } ChaseCam;
static double chase_yaw = 0.0, chase_look = 0.0;
static bool chase_yaw_set;

static ChaseCam chase_cam_at(double x4, double z4, double y, double yaw)
{
    double a = yaw * (2.0 * M_PI / 65536.0);
    ChaseCam c = { x4 - chase_dist * sin(a), z4 - chase_dist * cos(a), y + chase_height, yaw };
    return c;
}

/* 361c's key of a face of the snapshot as seen from (cx, cy, cz) */
static double face_key_from(const EnhSnap *s, int f, double cx, double cy, double cz)
{
    const u16 *r = s->face[f];
    int n = r[0] >> 14, vv[4] = { r[0] & 0x7FF, r[1] & 0x7FF, r[2] & 0x7FF, r[3] & 0x7FF };
    double d[4];
    for (int k = 0; k < 4; k++) {
        int v = vv[k] < s->nv ? vv[k] : vv[0];
        double dx = (s16)(u16)((u16)s->vx[v] - (u16)(s32)cx), dz = (s16)(u16)((u16)s->vz[v] - (u16)(s32)cz);
        d[k] = fabs(s->vy[v] - cy) + sqrt(dx * dx + dz * dz);
    }
    if (n == 0) return d[0];
    if (r[0] & 0x2000) {
        double k = fmax(d[0], d[1]);
        if (n >= 2) k = fmax(k, d[2]);
        if (n == 3) k = fmax(k, d[3]);
        return k;
    }
    if (n == 1) return (d[0] + d[1]) / 2;
    if (n == 2) return (d[0] + d[1] + d[2]) * 11.0 / 32.0;
    return (d[0] + d[1] + d[2] + d[3]) / 4;
}

/* The car's faces: brake lights and the lamps that show by side (4db6's runtime colours, the leading faces of
 * types 4..13), seen from the chase camera; then drawn after everything else, farthest first. */
static void car_faces_finish(EnhSnap *s)
{
    ChaseCam cc = chase_cam_at(s->cam_x4, s->cam_z4, s->cam_y, s->heading);
    double facing = s->heading;                           /* the car faces the cockpit camera's heading */
    for (int f = s->car_f0; f < s->car_f0 + s->car_nf; f++) {
        u16 *r = s->face[f];
        u8 type = (u8)(r[3] >> 11);
        if (type < 4 || type > 13) break;
        if (type == 13) break;                            /* wipers: model animation, ends the scan */
        u16 ax;
        if (type == 9) ax = DSB(DS_brake) ? 0x0C0C : 0x0404;          /* brake light */
        else if (type == 8) ax = (DSB(DS_frame8) & 7) == 0 ? 0x0F0F : 0;
        else if (type >= 10) continue;                    /* beacon, traffic lamp, flasher: not on the player's car */
        else if (type == 7) continue;
        else {
            u16 bp;
            if (type == 6) { ax = 0x0E0E; bp = 0; }
            else if (type == 4) { ax = 0x0404; bp = 0x0F0F; }
            else { ax = 0x0F0F; bp = 0; }
            int v = r[0] & 0x7FF;
            if (v >= s->nv) continue;
            double dx = (s16)(u16)((u16)s->vx[v] - (u16)(s32)cc.x4), dz = (s16)(u16)((u16)s->vz[v] - (u16)(s32)cc.z4);
            double bearing = atan2(dx, dz) * (65536.0 / (2.0 * M_PI));
            u16 d = (u16)(s32)floor(facing - bearing);
            if (d < 0x8000) ax = bp;
        }
        r[4] = ax;
    }
    /* order: the car's faces drawn last (order[] is nearest first), among themselves by their key from the
     * chase camera */
    static int idx[ENH_MAX_FACES];
    static double key[ENH_MAX_FACES];
    int n = s->car_nf;
    for (int i = 0; i < n; i++) {
        idx[i] = s->car_f0 + i;
        key[i] = face_key_from(s, s->car_f0 + i, cc.x4, cc.y, cc.z4);
    }
    for (int i = 1; i < n; i++)                           /* insertion sort, nearest first */
        for (int j = i; j > 0 && key[j] < key[j - 1]; j--) {
            double tk = key[j]; key[j] = key[j - 1]; key[j - 1] = tk;
            int ti = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = ti;
        }
    memmove(s->order + n, s->order, sizeof s->order[0] * (size_t)s->car_f0);
    for (int i = 0; i < n; i++) s->order[i] = (u16)idx[i];
}

/* ------------------------------------------------------------------------------------------------------
 * Cockpit overlays: recorded while frame_draw runs, moved into the snapshot by enh_frame_drawn.
 * ------------------------------------------------------------------------------------------------------ */

static struct {
    int ncmd, npix;
    EnhOvCmd cmd[ENH_MAX_OVCMD];
    u32 pix[ENH_MAX_OVPIX];
    u8 v[0x7800];                      /* V at the last mark */
} ov;

static u8 *vbuf_ptr(void) { return mp(DSW(DS_viewbuf_seg), 0); }

void enh_ov_begin(void)
{
    if (!enabled) return;
    ov.ncmd = ov.npix = 0;
    memcpy(ov.v, vbuf_ptr(), sizeof ov.v);
}

void enh_ov_pixels(void)
{
    if (!enabled) return;
    const u8 *v = vbuf_ptr();
    int first = ov.npix;
    for (int i = 0; i < 0x7800; i++) {
        if (v[i] == ov.v[i]) continue;
        if (ov.npix < ENH_MAX_OVPIX) ov.pix[ov.npix++] = (u32)i << 8 | v[i];
        ov.v[i] = v[i];
    }
    if (ov.npix > first && ov.ncmd < ENH_MAX_OVCMD) {
        EnhOvCmd *c = &ov.cmd[ov.ncmd++];
        memset(c, 0, sizeof *c);
        c->kind = OV_PIXELS;
        c->first = first;
        c->count = ov.npix - first;
    }
}

void enh_ov_skip(void)
{
    if (!enabled) return;
    memcpy(ov.v, vbuf_ptr(), sizeof ov.v);
}

/* x of a scr_x slot in V pixels (1/32 px, the overlays stay inside the view: no wrap) */
static float ov_x(u16 bo) { return (float)(s16)DSW(DS_scr_x + bo) / 32.0f; }
static float ov_y(u16 bo) { return (float)DSS(DS_scr_y + bo); }

void enh_ov_quad(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec, u16 colour, bool or_mode)
{
    (void)a_bx; (void)b_si; (void)c_di; (void)d_bp;
    if (!enabled || ov.ncmd >= ENH_MAX_OVCMD) return;
    EnhOvCmd *c = &ov.cmd[ov.ncmd++];
    memset(c, 0, sizeof *c);
    c->kind = OV_QUAD;
    for (int k = 0; k < 4; k++) {                     /* the record's vertex cycle w0, w1, w2, w3 */
        u16 v = (u16)((DSW((u16)(rec + 2 * k)) & 0x7FF) << 1);
        c->x[k] = ov_x(v);
        c->y[k] = ov_y(v);
    }
    c->colour = colour;
    c->or_mode = or_mode;
}

void enh_ov_line(u16 p_bx, u16 q_si, u16 rec, u16 colour)
{
    if (!enabled || ov.ncmd >= ENH_MAX_OVCMD) return;
    EnhOvCmd *c = &ov.cmd[ov.ncmd++];
    memset(c, 0, sizeof *c);
    c->kind = OV_LINE;
    c->x[0] = ov_x(p_bx); c->y[0] = ov_y(p_bx);
    c->x[1] = ov_x(q_si); c->y[1] = ov_y(q_si);
    c->colour = colour;
    c->w0 = DSW(rec);
}

/* ------------------------------------------------------------------------------------------------------
 * Snapshots
 * ------------------------------------------------------------------------------------------------------ */

static s16 d16(u16 a, u16 b) { return (s16)(u16)(a - b); }

/* front: rows added per 1/32 px of (x - centre) for a roll code (sar by |code|, negated for code > 0) */
static double roll_slope(s8 r)
{
    if (r == 0) return 0.0;
    double s = ldexp(1.0, -(r < 0 ? -r : r));
    return r > 0 ? -s : s;
}

/* The camera of a snapshot as the view interpolates it. x4, z4 and heading wrap at 65536. */
typedef struct { double x4, z4, heading, y, row, pitch, rs, mrs; } Cam;
enum { CAM_N = 8, CAM_WRAPPED = 3 };

static Cam cam_of(const EnhSnap *s)
{
    int n = s->roll < 0 ? -s->roll : s->roll;
    Cam c = { s->cam_x4, s->cam_z4, s->heading, s->cam_y, s->cam_row, s->pitch, roll_slope(s->roll),
              s->roll ? ldexp(1.0, -(n + 1)) : 0.0 };
    return c;
}

/* a - b per component (the wrapped ones to -32768..32767) */
static void cam_sub(const Cam *a, const Cam *b, double *d)
{
    const double *pa = &a->x4, *pb = &b->x4;
    for (int i = 0; i < CAM_N; i++) {
        double x = pa[i] - pb[i];
        if (i < CAM_WRAPPED) x -= floor(x / 65536.0 + 0.5) * 65536.0;
        d[i] = x;
    }
}


static Cam cam_p2;                    /* the camera of the snapshot before enh_prev */
static u64 t_p2;                      /* its time */
static bool pair_ok_prev;             /* cam_p2 -> enh_prev was a continuous step */


void enh_frame_drawn(void)
{
    if (!enabled) return;
    if (enh_prev->valid) { cam_p2 = cam_of(enh_prev); t_p2 = enh_prev->t_ns; }
    pair_ok_prev = pair_ok;
    EnhSnap *t = enh_prev;
    enh_prev = enh_cur;
    enh_cur = t;
    EnhSnap *s = enh_cur;

    /* OG: the chase view: the player's car into the game's arrays for this snapshot (put back at the end) */
    chase_params();
    CarSave car_saved;
    s->chase = chase_now();
    bool car = s->chase && car_emit(&car_saved);
    s->car_v0 = car ? car_saved.vert_count : 0;
    s->car_nv = car ? DSW(DS_vert_count) - car_saved.vert_count : 0;
    s->car_f0 = car ? car_saved.face_count : DSW(DS_face_count);
    s->car_nf = car ? DSW(DS_face_count) - car_saved.face_count : 0;

    s->valid = true;
    s->t_ns = host_tick_ns();
    s->frame_counter = DSW(DS_frame_counter);
    s->w32m = DSW(DS_view_w32m);
    s->cx_hi = DSB(DS_view_cx_hi);
    s->rows = DSW(DS_view_rows);
    s->mirror_base = DSW(DS_mirror_base);
    s->mirror_on = DSB(DS_mirror_on) != 0 && DSB(DS_external_panel_on) == 0 && !s->chase;
    s->ext_view = DSB(DS_ext_view) != 0;
    s->menu_preview = DSB(DS_menu_preview) != 0;
    s->day = DSB(DS_colour_mode) != 0;

    s->cam_x4 = DSW(DS_cam_x4);
    s->cam_z4 = DSW(DS_cam_z4);
    s->heading = DSW(DS_cam_heading);
    /* The cockpit camera's heading is the car's rounded to 64 units, so the turn between two frames varied by up
     * to 64 in a steady turn and the view sped up and slowed down every few frames. The unrounded heading the
     * rounding came from is taken while it still matches (not in the chase view or a replay's own camera). */
    {
        extern double sim_view_heading;
        u16 h = (u16)floor(sim_view_heading);
        if (!s->ext_view && (u16)(h & 0xFFC0) == (u16)DSW(DS_cam_heading)) s->heading = sim_view_heading;
    }
    s->cam_y = DSS(DS_cam_y_949E);
    s->cam_row = DSS(DS_cam_row);
    s->pitch = DSS(DS_pitch);
    s->roll = DSC(DS_cam_roll_94A0);
    if (s->chase) beam_capture(s); else s->beam_n = 0;   /* OG */

    s->sky_pair = DSW(DS_sky_pair);
    s->ground_pair = DSW(DS_ground_pair);
    s->sky_base = (u8)s->sky_pair;
    s->gradient = DSB(DS_detail) != 0 && DSB(DS_sky_flash) == 0 && DSW(DS_video_mode) == 0x13;

    s->nv = DSW(DS_vert_count);
    if (s->nv > ENH_MAX_VERTS) s->nv = ENH_MAX_VERTS;
    s->nf = DSW(DS_face_count);
    if (s->nf > ENH_MAX_FACES) s->nf = ENH_MAX_FACES;
    if (s->car_f0 > s->nf) s->car_f0 = s->nf;
    if (s->car_f0 + s->car_nf > s->nf) s->car_nf = s->nf - s->car_f0;
    s->static_vert_end = DSW(DS_parked_vert_end);
    for (int v = 0; v < s->nv; v++) {
        s->vx[v] = DSS(DS_vert_x + 2 * v);
        s->vy[v] = DSS(DS_vert_y + 2 * v);
        s->vz[v] = DSS(DS_vert_z + 2 * v);
    }
    u16 fseg = DSW(DS_face_block + 2), fbase = DSW(DS_face_block);
    for (int f = 0; f < s->nf; f++)
        for (int k = 0; k < 5; k++) s->face[f][k] = rd16(fseg, (u16)(fbase + 10 * f + 2 * k));
    /* the order the game sorted them in (faces_sort_keys): drawing in it keeps coplanar faces (road markings,
     * decals) in the same order between the game's frames instead of swapping as their keys cross */
    for (int i = 0; i < s->car_f0; i++) {                 /* OG: the game's faces (the car's follow) */
        u16 idx = (u16)((u16)(rd16(fseg, (u16)(DSW(DS_order_ofs) + 2 * i)) - fbase) / 10);
        s->order[i] = idx < s->car_f0 ? idx : (u16)i;
    }
    for (int i = s->car_f0; i < s->nf; i++) s->order[i] = (u16)i;
    for (int k = 0; k < 4; k++) {
        s->point_sizes[k] = DSB(DS_point_sizes + k);
        s->line_widths[k] = DSB(DS_line_widths + k);
    }

    s->nobj = DSW(DS_obj_count);
    if (s->nobj > ENH_MAX_OBJS) s->nobj = ENH_MAX_OBJS;
    for (int o = 0; o < s->nobj; o++) {
        EnhObj *e = &s->obj[o];
        e->x = DSW(DS_obj_x + 2 * o);
        e->z = DSW(DS_obj_z + 2 * o);
        e->y8 = DSW(DS_obj_y8 + 2 * o);
        e->heading = DSW(DS_obj_heading + 2 * o);
        e->flags = DSW(DS_obj_flags + 2 * o);
        u16 b = DSW(DS_obj_vert_base + 2 * o), n = DSW(DS_obj_vert_count + 2 * o);
        /* a moving vehicle emitted this frame: its range lies in the per-frame part after the parked ones. The
         * player's car (object 0) is not emitted in the cockpit view (509b skips it) and keeps the range of the
         * last chase-view frame, which now holds another car's vertices: carried with the player's motion, that
         * car would be drawn out of place. */
        bool moving = (e->flags & 0x2000) && n && b >= s->static_vert_end && b + n <= s->nv &&
                      !(o == 0 && !s->ext_view);
        if (o == 0 && s->chase) moving = false;           /* OG: placed with the chase camera (view_compute) */
        e->vbase = moving ? b : 0;
        e->vcount = moving ? n : 0;
        e->rbase = b;
        e->rcount = n;
    }

    s->nspr = DSW(DS_sprite_vis_count);
    if (s->nspr > ENH_MAX_SPRITES) s->nspr = ENH_MAX_SPRITES;
    for (int k = 0; k < s->nspr; k++) {
        EnhSpr *e = &s->spr[k];
        e->inst = (u16)(DSW(DS_spr_inst + 2 * k) >> 1);
        e->id = DSW(DS_sprite_id + 2 * e->inst);
        e->x = DSS(DS_sprite_x + 2 * e->inst);
        e->y = DSS(DS_sprite_y + 2 * e->inst);
        e->z = DSS(DS_sprite_z + 2 * e->inst);
    }
    s->nspr_game = s->nspr;
    s->sprite_count = DSW(DS_sprite_count);
    s->world_cell = DSW(DS_last_cell);
    s->world_tab = DSW(DS_last_octab);
    s->spr_min_dist = DSW(DS_sprite_min_proj_dist);
    s->spr_far_limit = DSW(DS__4);
    for (int k = 0; k < 32; k++) s->sprite_kind[k] = DSB(DS_sprite_kind + k);
    s->sprite_max_size = DSB(DS_sprite_max_size);

    s->novcmd = s->chase ? 0 : ov.ncmd;                   /* OG: no windscreen, dashboard edge or wipers outside */
    s->novpix = s->chase ? 0 : ov.npix;
    memcpy(s->ov, ov.cmd, sizeof(EnhOvCmd) * (size_t)s->novcmd);
    memcpy(s->ovpix, ov.pix, sizeof(u32) * (size_t)s->novpix);
    ov.ncmd = ov.npix = 0;
    memcpy(s->vbuf, vbuf_ptr(), sizeof s->vbuf);
    if (car) {                                            /* OG: the game's state as it was */
        car_restore(&car_saved);
        car_faces_finish(s);
    }
    enh_far_build(s);                                      /* the cells beyond the game's own */

    /* A continuous step: same kind of view, nothing jumped (restart, replay start, layout change). */
    EnhSnap *p = enh_prev;
    pair_ok = p->valid && p->ext_view == s->ext_view && p->menu_preview == s->menu_preview && p->chase == s->chase &&
              p->w32m == s->w32m && p->rows == s->rows &&
              abs(d16(s->cam_x4, p->cam_x4)) < 0x1800 && abs(d16(s->cam_z4, p->cam_z4)) < 0x1800 &&
              abs(s->cam_y - p->cam_y) < 0x200 && abs(d16(s->heading, p->heading)) < 0x2000 &&
              s->t_ns > p->t_ns;
}

void enh_view_presented(void)
{
    if (!enabled || !enh_cur->valid) return;
    EnhPresent *e = &enh_present;
    u16 cx = (u16)(((DSW(DS_view_w32m) >> 5) + 1) >> 1);        /* words per row */
    u16 t = DSW(DS_view_top_row);
    int rows = DSW(DS_view_rows);
    int vc = (0x60 - rows) >> 1;
    e->x0 = 160 - cx + (t >> 8);
    e->y0 = (t & 0xFF) + (DSB(DS_menu_preview) ? vc : 0);
    e->w = 2 * cx;
    e->rows = rows;
    e->hole = DSB(DS_external_panel_on) == 0;
    e->msg_rows = DSB(DS_msg_protect) ? 0x15 - (DSB(DS_menu_preview) ? vc : 0) : 0;
    e->mirror = enh_cur->mirror_on && DSB(DS_lzw_mirror_dirty) == 0;
    memcpy(e->vbuf, vbuf_ptr(), sizeof e->vbuf);
    memcpy(e->fbuf, enh_cur->vbuf, sizeof e->fbuf);
    memcpy(e->mbuf, mp(DSW(DS_mirrorbuf_seg), 0), sizeof e->mbuf);
    e->active = true;
}

void enh_stop(void)
{
    enh_present.active = false;
    enh_prev->valid = enh_cur->valid = false;
    pair_ok = false;
}

/* ------------------------------------------------------------------------------------------------------
 * The smooth view state at the time of a displayed frame
 * ------------------------------------------------------------------------------------------------------ */

#define TICK_NS ((double)PIT_DIV_GAME * 1e9 / PIT_HZ)

static EnhView view;
static s16 spr_prev_index[320];

/* Developer aid: TD3_ENH_COMPARE=1 shows the game's own frame (no smoothing) enhanced on the left half of
 * the screen and the original picture on the right half; 2 the other way round. */
static int compare = -1;
static bool compare_on(void)
{
    if (compare < 0) {
        const char *e = SDL_getenv("TD3_ENH_COMPARE");
        compare = e && (*e == '1' || *e == '2') ? *e - '0' : 0;
    }
    return compare > 0;
}


/* length of a game frame for the view: the time between two snapshots, or the configured pacing when the
 * game stopped in between (message boxes, the crash sequence) */
static double frame_period(u64 t_new, u64 t_old)
{
    double period = (double)(t_new - t_old), nominal = TICK_NS * host_frame_ticks();
    return period > 2.0 * nominal || period <= 0 ? nominal : period;
}

/* OG: the chase view's camera and car at the view time. eye = the cockpit camera as the view interpolates it
 * (x4, z4, heading, y). The car's vertices keep their place relative to that eye (so the car does not shake
 * against the camera that follows it); the camera hangs behind it, swinging round with chase_yaw_tau. */
static void chase_view(EnhView *v, const EnhSnap *c, const double *eye, u64 now)
{
    static u64 last;
    double dt = last ? (double)(s64)(now - last) / 1e9 : 0.0;
    last = now;
    if (dt < 0 || dt > 0.25) dt = 0.25;

    double dh = eye[2] - c->heading;                      /* the car's turn since the snapshot */
    dh -= floor(dh / 65536.0 + 0.5) * 65536.0;
    double r = dh * (2.0 * M_PI / 65536.0), cs = cos(r), sn = sin(r);
    for (int i = c->car_v0; i < c->car_v0 + c->car_nv; i++) {
        double rx = (s16)(u16)((u16)c->vx[i] - (u16)c->cam_x4), rz = (s16)(u16)((u16)c->vz[i] - (u16)c->cam_z4);
        v->vx[i] = (float)(eye[0] + rx * cs + rz * sn);
        v->vz[i] = (float)(eye[1] + rz * cs - rx * sn);
        v->vy[i] = (float)(c->vy[i] - c->cam_y + eye[3]);
    }
    v->obj_heading[0] = eye[2] - 0x4000;                  /* objects face their heading + 4000h */
    v->beam_n = c->beam_n;
    for (int k = 0; k < c->beam_n; k++) {
        double rx = c->beam_x[k] - c->cam_x4, rz = c->beam_z[k] - c->cam_z4;
        v->beam_vx[k] = eye[0] + rx * cs + rz * sn;
        v->beam_vz[k] = eye[1] + rz * cs - rx * sn;
        v->beam_vy[k] = c->beam_y[k] - c->cam_y + eye[3];
    }

    if (!chase_yaw_set || !pair_ok) { chase_yaw = eye[2]; chase_look = chase_look_target; chase_yaw_set = true; }
    else {
        double d = eye[2] - chase_yaw;
        d -= floor(d / 65536.0 + 0.5) * 65536.0;
        chase_yaw += d * (1.0 - exp(-dt / chase_yaw_tau));
        chase_look += (chase_look_target - chase_look) * (1.0 - exp(-dt / 0.08));
    }
    ChaseCam cc = chase_cam_at(eye[0], eye[1], eye[3], chase_yaw + chase_look * 32768.0);
    /* over the ground under the camera (a crest behind the car, a bank beside it) */
    double g = enh_ground_height(c, cc.x4, cc.z4, cc.y, 150.0);
    if (cc.y < g + 3 * ENH_EYE_CLEAR) cc.y = g + 3 * ENH_EYE_CLEAR;
    v->cam_x4 = cc.x4;
    v->cam_z4 = cc.z4;
    v->cam_y = cc.y;
    v->heading = cc.yaw - floor(cc.yaw / 65536.0) * 65536.0;
    v->cam_row = chase_row;
    v->pitch = 0;
    v->roll_slope = 0;
    v->mroll_slope = 0;
    v->roll = 0;
}

static void view_compute(void)
{
    const EnhSnap *c = enh_cur, *p = enh_prev;
    u64 now = host_time_ns();
    /* x: view time in frames after cur. Past the next frame's due time the view keeps moving for half a
     * frame (a frame that runs a tick late), then holds. */
    double x = 0.0;
    if (pair_ok) {
        x = (double)(s64)(now - c->t_ns) / frame_period(c->t_ns, p->t_ns);
        if (x < 0.0) x = 0.0;
        if (x > 1.5) x = 1.5;
    }
    double lag = enh_motion_delay / 100.0, ph = x - lag;
    if (compare_on()) { ph = 0.0; x = lag; }
    EnhView *v = &view;

    /* Camera: the trajectory through the last two snapshots, shown `lag` of a frame late (extrapolated
     * when the view is ahead of the newest snapshot), blended over the frame from the trajectory through the
     * two snapshots before, so neither position nor speed jumps when a new frame arrives. */
    Cam cc = cam_of(c), cp = cam_of(p);
    double dn[CAM_N], dp[CAM_N], rel[CAM_N], out[CAM_N], ph_old = 0.0;
    cam_sub(&cc, &cp, dn);
    bool blend = pair_ok && pair_ok_prev && !compare_on();
    if (blend) {
        cam_sub(&cp, &cam_p2, dp);
        cam_sub(&cp, &cc, rel);                         /* the old trajectory relative to cur */
        double xo = (double)(s64)(now - p->t_ns) / frame_period(p->t_ns, t_p2);
        if (xo > 2.5) xo = 2.5;
        ph_old = xo - lag;
    }
    static double span = -1;                            /* developer aid TD3_ENH_BLEND=frames */
    if (span < 0) { const char *b = SDL_getenv("TD3_ENH_BLEND"); span = b ? SDL_atof(b) : ENH_BLEND_SPAN; if (span < 0.05) span = 0.05; }
    double t = x / span;
    if (t > 1.0) t = 1.0;
    double w = t * t * (3.0 - 2.0 * t);
    const double *pc = &cc.x4;
    for (int i = 0; i < CAM_N; i++) {
        double nw = pair_ok ? ph * dn[i] : 0.0;
        double o = blend ? rel[i] + ph_old * dp[i] : nw;
        out[i] = pc[i] + o + (nw - o) * w;
    }
    /* The height never below the line through the last two frames, nor guessed downwards past the newest:
     * early in a frame the blend still follows the old trajectory, which keeps falling after a landing and
     * keeps level when the road turns up a slope, and a late frame extrapolates the fall. Either took the eye
     * under the ground for a moment (the ground around the camera then drawn as a ceiling). */
    if (!compare_on()) {
        double floor_y = cc.y + (pair_ok ? fmin(ph, 0.0) * dn[3] : 0.0);
        if (out[3] < floor_y) out[3] = floor_y;
    }
    v->cam_x4 = out[0];
    v->cam_z4 = out[1];
    v->heading = out[2];
    v->cam_y = out[3];
    /* ENH: on a steep slope the game's own eye sinks to the surface and a frame or two below it (measured on a
     * 21-degree slope: 6 units above, then 6 below; about 46 on the flat), and the view showed the underside of
     * the slope. The eye is kept ENH_EYE_CLEAR over the game's ground under it (not a bridge deck overhead). */
    if (!compare_on() && !c->menu_preview) {
        double g = enh_ground_height(c, v->cam_x4, v->cam_z4, v->cam_y, ENH_EYE_CLEAR + 64);
        if (v->cam_y < g + ENH_EYE_CLEAR) v->cam_y = g + ENH_EYE_CLEAR;
    }
    /* The game's pitch (horizon row) bobs by up to 20 rows from one frame to the next over bumps and while
     * steering; a short low-pass (ENH_PITCH_SMOOTH_MS) takes the edge off without delaying anything else. */
    static double f_row, f_pitch;
    static u64 f_last;
    double dt = f_last ? (double)(s64)(now - f_last) / 1e6 : 0.0;
    f_last = now;
    if (!pair_ok || dt <= 0.0 || dt > 200.0) { f_row = out[4]; f_pitch = out[5]; }
    else {
        double k = 1.0 - exp(-dt / ENH_PITCH_SMOOTH_MS);
        f_row += (out[4] - f_row) * k;
        f_pitch += (out[5] - f_pitch) * k;
    }
    v->cam_row = compare_on() ? out[4] : f_row;
    v->pitch = compare_on() ? out[5] : f_pitch;
    v->roll_slope = out[6];
    v->mroll_slope = out[7];
    v->roll = c->roll;

    for (int i = 0; i < c->nv; i++) {
        v->vx[i] = c->vx[i];
        v->vy[i] = c->vy[i];
        v->vz[i] = c->vz[i];
    }
    for (int i = 0; i < c->nfv; i++) {                     /* the far ring: static */
        v->vx[c->nv + i] = c->fvx[i];
        v->vy[c->nv + i] = c->fvy[i];
        v->vz[c->nv + i] = c->fvz[i];
    }
    /* moving vehicles: carried to the view time and turned to their exact heading (the model is built at
     * the high byte of the heading) */
    for (int o = 0; o < c->nobj; o++) {
        const EnhObj *e = &c->obj[o];
        v->obj_heading[o] = e->heading;
        if (!e->vcount) continue;
        double dx = 0, dz = 0, dy = 0, dh = 0;
        if (pair_ok && o < p->nobj && (p->obj[o].flags & 0x2000)) {
            const EnhObj *q = &p->obj[o];
            s16 ex = d16(e->x, q->x), ez = d16(e->z, q->z);
            if (abs(ex) < 0x200 && abs(ez) < 0x200) {
                dx = ph * ex;
                dz = ph * ez;
                dy = ph * ((s16)e->y8 - (s16)q->y8) / 8.0;
                if (ph > 0 && dy < 0) dy = 0;           /* not guessed downwards into the ground (as the camera) */
                dh = ph * d16(e->heading, q->heading);
            }
        }
        double h = e->heading + dh;
        v->obj_heading[o] = h;
        double rot = (h - (double)(e->heading & 0xFF00)) * (2.0 * M_PI / 65536.0);
        double cs = cos(rot), sn = sin(rot);
        double ox = (double)(u16)(e->x << 2), oz = (double)(u16)(e->z << 2);
        for (int i = e->vbase; i < e->vbase + e->vcount; i++) {
            double rx = (s16)(u16)(c->vx[i] - (u16)ox), rz = (s16)(u16)(c->vz[i] - (u16)oz);
            double nx = rx * cs + rz * sn, nz = rz * cs - rx * sn;
            v->vx[i] = (float)(ox + 4.0 * dx + nx);
            v->vz[i] = (float)(oz + 4.0 * dz + nz);
            v->vy[i] = (float)(c->vy[i] + dy);
        }
    }
    v->beam_n = 0;
    if (c->chase && !compare_on()) {                      /* OG: the eye before the cockpit's ground clearance */
        double eye[4] = { out[0], out[1], out[2], out[3] };
        chase_view(v, c, eye, now);
    }

    /* sprites: the ones that move (the fixed instances 0..8 such as the sun and the moon, drifting clouds and
     * birds, crash debris) carried like the camera. The rest never move, and the tiles' children are handed out
     * again in a new order whenever the world is rebuilt (a new cell): matched by instance, a tree would be
     * carried from where another tree stood, a displaced copy for a game frame. */
    bool same_world = c->world_cell == p->world_cell && c->world_tab == p->world_tab;
    if (pair_ok) {
        for (int k = 0; k < 320; k++) spr_prev_index[k] = -1;
        for (int k = 0; k < p->nspr; k++) if (p->spr[k].inst < 320) spr_prev_index[p->spr[k].inst] = (s16)k;
    }
    for (int k = 0; k < c->nspr; k++) {
        const EnhSpr *e = &c->spr[k];
        double x = e->x, y = e->y, z = e->z;
        u8 n = (u8)e->id;
        bool moves = e->inst < 9 || (n & 0xC0) || (n >= 6 && n <= 8);
        if (e->inst >= c->sprite_count && !same_world) moves = false;
        if (pair_ok && moves && e->inst < 320 && spr_prev_index[e->inst] >= 0) {
            const EnhSpr *q = &p->spr[spr_prev_index[e->inst]];
            s16 ex = (s16)(e->x - q->x), ez = (s16)(e->z - q->z), ey = (s16)(e->y - q->y);
            if ((u8)e->id == (u8)q->id && abs(ex) < 0x400 && abs(ez) < 0x400 && abs(ey) < 0x400) {
                x += ph * ex;
                z += ph * ez;
                y += ph * ey;
            }
        }
        v->spr_x[k] = (float)x;
        v->spr_y[k] = (float)y;
        v->spr_z[k] = (float)z;
    }
}

/* ------------------------------------------------------------------------------------------------------
 * Composition over the VGA screen
 * ------------------------------------------------------------------------------------------------------ */

static const u32 *cpal;
static u32 haze_rgb;                  /* the haze's colour through the current DAC */

static inline u32 mix_rgb(u32 a, u32 b, u32 w)   /* w / 256 of b */
{
    u32 iw = 256 - w;
    u32 r = (((a >> 16) & 0xFF) * iw + ((b >> 16) & 0xFF) * w) >> 8;
    u32 g = (((a >> 8) & 0xFF) * iw + ((b >> 8) & 0xFF) * w) >> 8;
    u32 bl = ((a & 0xFF) * iw + (b & 0xFF) * w) >> 8;
    return r << 16 | g << 8 | bl;
}

static inline u32 pair_rgb(u32 s)
{
    u32 a = cpal[s & 0xFF], b = cpal[(s >> 8) & 0xFF], w = (s >> 16) & 0xFF;
    if (a == b || w == 0) return a;
    return mix_rgb(a, b, w);
}

/* a sample and its light (EnhTarget lt): the colours with the light's bits ORed in, blended in by its amount */
static inline u32 sample_rgb(u32 s, u16 l)
{
    u32 c = pair_rgb(s), h = s >> 24;
    if (l & 0xFF) c = mix_rgb(c, pair_rgb(s | (u32)(l >> 8) * 0x101u), l & 0xFF);
    return h ? mix_rgb(c, haze_rgb, h) : c;
}

/* OG widescreen: columns added each side of the 320-wide picture (host_wide_margin); the frame is
 * (320 + 2 EM) x 200 times the scale, the VGA picture at column EM */
static int EM;

/* Output pixel block of the view pixel (c, r) of a target: S x S pixels, each the average of A x A samples. */
static void paste_block(u32 *out, int ox, int oy, const EnhTarget *t, int c, int r)
{
    int S = enh_scale, A = enh_aa, W = (320 + 2 * EM) * S;
    for (int j = 0; j < S; j++) {
        u32 *o = out + (size_t)(oy + j) * W + ox;
        for (int i = 0; i < S; i++) {
            u32 R = 0, G = 0, B = 0;
            for (int b = 0; b < A; b++) {
                size_t at = (size_t)((r * S + j) * A + b) * t->w + (size_t)(c * S + i) * A;
                const u32 *s = t->s + at;
                const u16 *l = t->use_lt ? t->lt + at : NULL;
                for (int a = 0; a < A; a++) {
                    u32 x = sample_rgb(s[a], l ? l[a] : 0);
                    R += x >> 16 & 0xFF; G += x >> 8 & 0xFF; B += x & 0xFF;
                }
            }
            u32 n = (u32)(A * A);
            o[i] = (R / n) << 16 | (G / n) << 8 | (B / n);
        }
    }
}

typedef struct { u32 *out; const u8 *vram; } PasteCtx;

static void paste_front_row(int r, void *vctx)
{
    const PasteCtx *pc = vctx;
    const EnhPresent *e = &enh_present;
    if (r < e->msg_rows) return;
    int sy = e->y0 + r;
    if (sy < 0 || sy >= 200) return;
    int cols = enh_wide_cols;                              /* OG: the target's extra columns each side */
    for (int ct = 0; ct < e->w + 2 * cols; ct++) {
        int c = ct - cols, sx = e->x0 + c;
        if ((compare == 1 && sx >= 160) || (compare == 2 && sx < 160)) continue;
        if (c >= 0 && c < e->w) {                           /* the game's window: as the original screen allows */
            if (sx < 0 || sx >= 320) continue;
            if (e->hole && r < 14 && sx >= 168 && sx < 256) continue;
            int vi = r * 320 + c;
            if (pc->vram[sy * 320 + sx] != e->vbuf[vi] || e->vbuf[vi] != e->fbuf[vi]) continue;
        } else if (sx < -EM || sx >= 320 + EM) continue;    /* OG: the margins */
        paste_block(pc->out, (sx + EM) * enh_scale, sy * enh_scale, &enh_front, ct, r);
    }
}

/* OG widescreen: the margins of the rows the 3D view does not reach (the roof, the dashboard, panels) continue
 * the picture's outermost columns: each row's edge colour (3 columns x 3 rows averaged) drawn outwards, darkening
 * towards the frame's edge, as the dashboard and the roof going into the doors' shadow */
static void fill_margins(u32 *xrgb, int skip0, int skip1)
{
    int S = enh_scale, W = (320 + 2 * EM) * S, M = EM * S;
    if (EM <= 0) return;
    for (int y = 0; y < 200; y++) {
        if (y >= skip0 && y < skip1) continue;
        u32 rl = 0, gl = 0, bl = 0, rr = 0, gr = 0, br = 0, n = 0;
        for (int yy = y - 1; yy <= y + 1; yy++) {
            if (yy < 0 || yy >= 200 || (yy >= skip0 && yy < skip1)) continue;
            const u32 *row = xrgb + (size_t)(yy * S + S / 2) * W;
            for (int c = 0; c < 3; c++) {
                u32 l = row[(EM + c) * S + S / 2], r = row[(EM + 319 - c) * S + S / 2];
                rl += l >> 16 & 0xFF; gl += l >> 8 & 0xFF; bl += l & 0xFF;
                rr += r >> 16 & 0xFF; gr += r >> 8 & 0xFF; br += r & 0xFF;
                n++;
            }
        }
        if (!n) continue;
        for (int i = 0; i < M; i++) {
            double f = 1.0 - 0.55 * ((i + 0.5) / M);      /* i output columns out from the picture */
            u32 L = (u32)(rl / n * f) << 16 | (u32)(gl / n * f) << 8 | (u32)(bl / n * f);
            u32 R = (u32)(rr / n * f) << 16 | (u32)(gr / n * f) << 8 | (u32)(br / n * f);
            for (int jj = 0; jj < S; jj++) {
                u32 *row = xrgb + (size_t)(y * S + jj) * W;
                row[M - 1 - i] = L;
                row[W - M + i] = R;
            }
        }
    }
}

/* OG: the chase view covers the whole screen but for the cockpit's instruments, which stay on top of it as the
 * game draws them: the two top panels (compass and clock, radar detector), the rev counter and the speedometer
 * (ellipses: the VGA's pixels are tall) and the gear lever's window; and a message box while one is up. */
typedef struct { double cx, cy, rx, ry; } HudOval;
static const HudOval hud_ovals[] = { { 92.5, 154.0, 22.5, 19.0 }, { 138.5, 154.0, 22.5, 19.0 } };
typedef struct { int x0, y0, x1, y1; } HudRect;                     /* [x0, x1) x [y0, y1) */
/* OG: everything is moved to the screen's corners (with widescreen margins: the frame's corners): the top panels
 * (compass and clock, radar detector), the dials (cut out as the two ovals) bottom left, the gear lever's window
 * bottom right (only while the game shows it). Source rectangle in the 320 x 200 picture, then where its top
 * left corner goes: x from the frame's left (right = true: x is the gap to the frame's right edge). */
typedef struct { HudRect src; int dx0, dy0; bool right, ovals, lever; } HudMove;
static const HudMove hud_moves[] = {
    { { 6, 5, 65, 16 }, 6, 5, false, false, false },
    { { 263, 4, 317, 16 }, 3, 4, true, false, false },
    { { 68, 133, 164, 177 }, 3, 154, false, true, false },
    { { 248, 124, 304, 172 }, 1, 151, true, false, true },
};
#define HUD_NMOVES ((int)(sizeof hud_moves / sizeof hud_moves[0]))

/* how much of the dials shows at the source point (x, y), 0..1; aa = an output pixel in VGA pixels */
static double hud_oval_cover(double x, double y, double aa)
{
    double best = 0.0;
    for (size_t i = 0; i < sizeof hud_ovals / sizeof hud_ovals[0]; i++) {
        const HudOval *o = &hud_ovals[i];
        double dx = (x - o->cx) / o->rx, dy = (y - o->cy) / o->ry, d = sqrt(dx * dx + dy * dy);
        double c = (1.0 - d) * o->rx / aa + 0.5;          /* the edge over one output pixel */
        if (c > best) best = c > 1.0 ? 1.0 : c;
    }
    return best;
}

/* the pixel (c, r) of the 320 x 200 picture stays the VGA picture (a message box) */
static bool hud_keep(int c, int r)
{
    return msgbox.on && c >= msgbox.x0 && c <= msgbox.x1 && r >= msgbox.y0 && r <= msgbox.y1;
}

/* the moved instruments: taken from the scaled VGA picture before the view covers it, laid over it after */
static u32 *hud_buf[HUD_NMOVES];
static int hud_buf_scale;

static void hud_take(const u32 *xrgb)
{
    int S = enh_scale, W = (320 + 2 * EM) * S;
    if (hud_buf_scale != S) {
        for (int m = 0; m < HUD_NMOVES; m++) {
            const HudRect *r = &hud_moves[m].src;
            SDL_free(hud_buf[m]);
            hud_buf[m] = SDL_malloc(sizeof(u32) * (size_t)((r->x1 - r->x0) * S) * (size_t)((r->y1 - r->y0) * S));
        }
        hud_buf_scale = S;
    }
    for (int m = 0; m < HUD_NMOVES; m++) {
        const HudRect *r = &hud_moves[m].src;
        int w = (r->x1 - r->x0) * S, h = (r->y1 - r->y0) * S;
        if (!hud_buf[m]) continue;
        for (int j = 0; j < h; j++)
            memcpy(hud_buf[m] + (size_t)j * w, xrgb + (size_t)(r->y0 * S + j) * W + (size_t)(r->x0 + EM) * S,
                   sizeof(u32) * (size_t)w);
    }
}

static void hud_put(u32 *xrgb)
{
    int S = enh_scale, W = (320 + 2 * EM) * S;
    for (int m = 0; m < HUD_NMOVES; m++) {
        const HudMove *mv = &hud_moves[m];
        const HudRect *r = &mv->src;
        int w = (r->x1 - r->x0) * S, h = (r->y1 - r->y0) * S;
        if (!hud_buf[m]) continue;
        if (mv->lever && DSB(DS_look_rect_dirty) == 0) continue;    /* the lever's window: only while it is up */
        int dx = mv->right ? 320 + 2 * EM - mv->dx0 - (r->x1 - r->x0) : mv->dx0;
        for (int j = 0; j < h; j++) {
            int oy = mv->dy0 * S + j;
            if (oy < 0 || oy >= 200 * S) continue;
            for (int i = 0; i < w; i++) {
                int ox = dx * S + i;
                if (ox < 0 || ox >= W) continue;
                double k = mv->ovals ? hud_oval_cover(r->x0 + (i + 0.5) / S, r->y0 + (j + 0.5) / S, 1.0 / S) : 1.0;
                if (k <= 0.0) continue;
                u32 *o = xrgb + (size_t)oy * W + ox, src = hud_buf[m][(size_t)j * w + i];
                *o = k >= 1.0 ? src : mix_rgb(*o, src, (u32)(k * 256.0));
            }
        }
    }
}

static void paste_chase_row(int r, void *vctx)
{
    const PasteCtx *pc = vctx;
    int S = enh_scale;
    const EnhTarget *t = &enh_front;
    for (int c = 0; c < ENH_CHASE_W + 2 * enh_wide_cols; c++)   /* OG: frame column c, the picture's c - EM */
        if (!hud_keep(c - EM, r)) paste_block(pc->out, c * S, r * S, t, c, r);
}

/* the part of M that mirror_present copies (rounded top), without the frame mirror_frame_draw paints */
static bool mirror_pixel(int c, int r)
{
    static const u8 lo[5] = { 24, 8, 6, 5, 4 }, hi[5] = { 64, 80, 82, 83, 84 };
    if (r < 5 && (c < lo[r] || c >= hi[r])) return false;
    if (r >= 3 && r <= 5 && (c <= 2 || c >= 85)) return false;
    if (r >= 6 && r <= 17 && (c <= 1 || c >= 86)) return false;
    if (r == 18 && (c <= 2 || c >= 85)) return false;
    return true;
}

static void paste_mirror_row(int r, void *vctx)
{
    const PasteCtx *pc = vctx;
    const EnhPresent *e = &enh_present;
    int sy = 11 + r;
    for (int c = 0; c < 88; c++) {
        if (!mirror_pixel(c, r)) continue;
        int sx = 168 + c;
        if ((compare == 1 && sx >= 160) || (compare == 2 && sx < 160)) continue;
        if (pc->vram[sy * 320 + sx] != e->mbuf[r * 88 + c]) continue;
        paste_block(pc->out, (sx + EM) * enh_scale, sy * enh_scale, &enh_mirror, c, r);
    }
}

bool enh_compose(u32 *xrgb, const u32 pal[256])
{
    if (!enabled || !enh_present.active || !enh_cur->valid) return false;
    const EnhSnap *s = enh_cur;
    u64 t0 = host_time_ns();
    EM = host_wide_margin();
    /* OG widescreen: the chase view and a full-width game window (cockpit, F5, replays) reach into the margins */
    bool full = s->chase || (!s->menu_preview && (int)s->w32m + 0x20 == 320 * 32);
    enh_wide_cols = full && !compare_on() ? EM : 0;
    view_compute();
    enh_scene_build(s, &view);
    enh_target_raster(&enh_front, s);
    if (s->mirror_on) enh_target_raster(&enh_mirror, s);

    cpal = pal;
    haze_rgb = pair_rgb(enh_haze_colour(s));
    PasteCtx pc = { xrgb, mp(0xA000, 0) };
    if (s->chase && !compare_on()) {                      /* OG: the whole screen but the instruments */
        hud_take(xrgb);
        host_parallel_for(ENH_CHASE_ROWS, paste_chase_row, &pc);
        hud_put(xrgb);
    } else {
        int rows = enh_present.rows < s->rows ? enh_present.rows : s->rows;
        host_parallel_for(rows, paste_front_row, &pc);
        if (enh_present.mirror && s->mirror_on && enh_present.hole) host_parallel_for(19, paste_mirror_row, &pc);
        if (!s->menu_preview) {                           /* OG: the margins of the rows the 3D does not cover */
            int y0 = enh_present.y0 + enh_present.msg_rows, y1 = enh_present.y0 + rows;
            if (enh_wide_cols == 0) y0 = y1 = 0;
            fill_margins(xrgb, y0, y1);
        }
    }

    /* Developer aid: TD3_ENH_LOG=file appends one line per displayed frame (time, phase, camera, cost). */
    static FILE *log;
    static int log_state = -1;
    if (log_state < 0) {
        const char *path = SDL_getenv("TD3_ENH_LOG");
        log = path ? fopen(path, "w") : NULL;
        log_state = log != NULL;
    }
    if (log) {
        fprintf(log, "%.4f frame %u x4 %.2f z4 %.2f h %.1f row %.2f prims %d us %u speed %d keys %02X brake %d thr %d wheel %d\n",
                (double)t0 / 1e9, s->frame_counter, view.cam_x4, view.cam_z4, view.heading, view.cam_row,
                enh_front.nprim, (unsigned)((host_time_ns() - t0) / 1000), DSS(DS_car_speed), DSB(DS_kbd_bits), DSB(DS_brake), DSB(DS_throttle), DSB(DS_steer_wheel));
        fflush(log);
    }
    return true;
}
