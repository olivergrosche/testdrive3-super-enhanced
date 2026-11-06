/* OG: the pistol's sounds (WEAPON.md), made here (no recordings): mono 16-bit at HOST_AUDIO_RATE, built once on
 * first use and kept for host_pcm_play. */
#include "sound/pcm_sfx.h"
#include "host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RATE ((double)HOST_AUDIO_RATE)

static s16 *buf[PCM_COUNT];
static int len[PCM_COUNT];

static u32 rng = 0x12345678u;
static double noise(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (double)(rng & 0xFFFF) / 32768.0 - 1.0;
}

/* to s16 with its peak at `peak` */
static void finish(int id, const double *x, int n, double peak)
{
    double m = 1e-9;
    for (int i = 0; i < n; i++) if (fabs(x[i]) > m) m = fabs(x[i]);
    buf[id] = malloc(sizeof(s16) * (size_t)n);
    if (!buf[id]) return;
    for (int i = 0; i < n; i++) buf[id][i] = (s16)lround(x[i] / m * peak);
    len[id] = n;
}

/* a click: a few samples of a decaying resonance (metal parts meeting) */
static void click(double *x, int n, double at, double f, double tau, double amp)
{
    int i0 = (int)(at * RATE);
    for (int i = i0; i < n; i++) {
        double t = (i - i0) / RATE;
        if (t > 8 * tau) break;
        x[i] += amp * (sin(2 * M_PI * f * t) * 0.7 + noise() * 0.3) * exp(-t / tau);
    }
}

static void make_shot(void)
{
    int n = (int)(0.75 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0, lp2 = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        double env = (t < 0.0015 ? t / 0.0015 : 1.0) * (0.80 * exp(-t / 0.030) + 0.20 * exp(-t / 0.14));
        double fc = 1500 + 9000 * exp(-t / 0.025);                 /* the bang darkens as it dies */
        double a = 1 - exp(-2 * M_PI * fc / RATE);
        lp += (noise() - lp) * a;
        lp2 += (lp - lp2) * a;
        double f = 120 - 55 * fmin(t / 0.12, 1.0);                  /* the thump under it */
        double thump = sin(2 * M_PI * f * t) * exp(-t / 0.055) * 0.9;
        x[i] = lp2 * env * 2.2 + thump;
        if (i < 40) x[i] += (i < 12 ? 0.45 : -0.25) * (1 - i / 40.0);   /* the crack */
    }
    /* the slap back from the surroundings: two late, dark, quieter copies */
    for (int k = 0; k < 2; k++) {
        int d = (int)((k ? 0.19 : 0.085) * RATE);
        double g = k ? 0.16 : 0.26, sm = 0;
        for (int i = n - 1; i >= d; i--) {
            sm += (x[i - d] - sm) * 0.25;
            x[i] += sm * g;
        }
    }
    finish(PCM_SHOT, x, n, 21000);
    free(x);
}

static void make_hit_metal(void)
{
    int n = (int)(0.30 * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        x[i] = (sin(2 * M_PI * 1870 * t) * 0.6 + sin(2 * M_PI * 2930 * t) * 0.45 + sin(2 * M_PI * 4410 * t) * 0.3) *
               exp(-t / 0.045) + noise() * exp(-t / 0.006) * 1.2;
    }
    finish(PCM_HIT_METAL, x, n, 11000);
    free(x);
}

static void make_hit_dirt(void)
{
    int n = (int)(0.22 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        lp += (noise() - lp) * 0.18;
        x[i] = lp * exp(-t / 0.035) + sin(2 * M_PI * 90 * t) * exp(-t / 0.03) * 0.3;
    }
    finish(PCM_HIT_DIRT, x, n, 8000);
    free(x);
}

static void make_hit_water(void)
{
    int n = (int)(0.35 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        lp += (noise() - lp) * 0.45;
        double gurgle = sin(2 * M_PI * (420 + 300 * sin(2 * M_PI * 23 * t)) * t) * exp(-t / 0.06) * 0.4;
        x[i] = lp * (t < 0.01 ? t / 0.01 : 1) * exp(-t / 0.07) + gurgle;
    }
    finish(PCM_HIT_WATER, x, n, 7000);
    free(x);
}

static void make_empty(void)
{
    int n = (int)(0.08 * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    click(x, n, 0.0, 3100, 0.006, 1.0);
    finish(PCM_EMPTY, x, n, 7000);
    free(x);
}

static void make_reload(void)
{
    int n = (int)(1.30 * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    click(x, n, 0.12, 1900, 0.010, 0.8);          /* the magazine out */
    click(x, n, 0.14, 900, 0.020, 0.5);
    click(x, n, 0.62, 1500, 0.012, 1.0);          /* the magazine in */
    click(x, n, 0.63, 420, 0.030, 0.7);
    click(x, n, 0.98, 2600, 0.008, 0.9);          /* the slide back */
    click(x, n, 1.08, 2200, 0.010, 1.0);          /* and home */
    click(x, n, 1.09, 700, 0.025, 0.6);
    finish(PCM_RELOAD, x, n, 9000);
    free(x);
}

static void make_wreck(void)
{
    int n = (int)(1.4 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0, hp = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        lp += (noise() - lp) * 0.06;                                 /* the whump */
        double hiss = noise() - hp;                                 /* and the hiss after it */
        hp += hiss * 0.5;
        x[i] = lp * 3.0 * exp(-t / 0.18) * (t < 0.01 ? t / 0.01 : 1) + hiss * 0.12 * exp(-t / 0.6) * fmin(t / 0.1, 1.0)
               + sin(2 * M_PI * 55 * t) * exp(-t / 0.12) * 0.5;
    }
    finish(PCM_WRECK, x, n, 16000);
    free(x);
}

/* the gangsters' gun: the shot played 1.25 times as fast (higher, shorter), with its own echo */
static void make_shot_foe(void)
{
    int n0;
    const s16 *b = pcm_sfx(PCM_SHOT, &n0);
    if (!b) return;
    int n = (int)(n0 / 1.25);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double p = i * 1.25;
        int k = (int)p;
        double f = p - k, a = b[k], c = k + 1 < n0 ? b[k + 1] : 0;
        x[i] = a + (c - a) * f;
    }
    finish(PCM_SHOT_FOE, x, n, 21000);
    free(x);
}

static void make_flesh(void)
{
    int n = (int)(0.18 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        lp += (noise() - lp) * 0.10;
        x[i] = lp * 2.5 * exp(-t / 0.025) + sin(2 * M_PI * (140 - 60 * t / 0.18) * t) * exp(-t / 0.04);
    }
    finish(PCM_FLESH, x, n, 12000);
    free(x);
}

static void make_hurt(void)
{
    int n = (int)(0.30 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE;
        lp += (noise() - lp) * 0.05;
        x[i] = sin(2 * M_PI * (85 - 30 * t / 0.3) * t) * exp(-t / 0.07) + lp * 2.0 * exp(-t / 0.05);
    }
    finish(PCM_HURT, x, n, 15000);
    free(x);
}

static void make_rico(void)
{
    int n = (int)(0.45 * RATE);
    double *x = calloc((size_t)n, sizeof *x), ph = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE, f = 3800 * exp(-t / 0.25) + 900;      /* the whine falling */
        ph += 2 * M_PI * f / RATE;
        x[i] = sin(ph) * exp(-t / 0.14) * (t < 0.004 ? t / 0.004 : 1) + noise() * exp(-t / 0.008) * 0.8;
    }
    finish(PCM_RICO, x, n, 7000);
    free(x);
}

/* another sound from the shot: played `speed` times as fast, after `keep` seconds dying away with tau, plus a low
 * thump of `thump` (0 none) */
static void from_shot(int id, double speed, double keep, double tau, double thump, double peak)
{
    int n0;
    const s16 *b = pcm_sfx(PCM_SHOT, &n0);
    if (!b) return;
    int n = (int)(n0 / speed);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double p = i * speed, t = i / RATE;
        int k = (int)p;
        double f = p - k, a = b[k], c = k + 1 < n0 ? b[k + 1] : 0;
        x[i] = (a + (c - a) * f) / 32768.0 * (t < keep ? 1 : exp(-(t - keep) / tau));
        if (thump > 0) x[i] += sin(2 * M_PI * (70 - 25 * fmin(t / 0.2, 1)) * t) * exp(-t / 0.13) * thump;
    }
    finish(id, x, n, peak);
    free(x);
}

static void make_clicks(int id, const double (*c)[4], int nc, double len, double peak)
{
    int n = (int)(len * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    for (int i = 0; i < nc; i++) click(x, n, c[i][0], c[i][1], c[i][2], c[i][3]);
    finish(id, x, n, peak);
    free(x);
}

/* TOWNS.md: the till: the drawer's clunk, then a bell (two partials, the second inharmonic) */
static void make_cash(void)
{
    int n = (int)(0.9 * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    click(x, n, 0.0, 600, 0.02, 0.8);
    click(x, n, 0.012, 1900, 0.008, 0.5);
    for (int i = (int)(0.09 * RATE); i < n; i++) {
        double t = i / RATE - 0.09;
        x[i] += 0.55 * sin(2 * M_PI * 2093 * t) * exp(-t / 0.22) + 0.30 * sin(2 * M_PI * 5370 * t) * exp(-t / 0.09) +
                0.20 * sin(2 * M_PI * 3140 * t) * exp(-t / 0.15);
    }
    finish(PCM_CASH, x, n, 14000);
    free(x);
}

/* the spray gun: bursts of hiss (high-passed noise), the last one long */
static void make_spray(void)
{
    int n = (int)(2.2 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0, prev = 0;
    if (!x) return;
    static const double burst[][2] = { { 0.0, 0.35 }, { 0.5, 0.35 }, { 1.0, 1.1 } };
    for (int i = 0; i < n; i++) {
        double t = i / RATE, env = 0;
        for (int b = 0; b < 3; b++) {
            double u = t - burst[b][0];
            if (u < 0 || u > burst[b][1]) continue;
            env += fmin(1.0, u / 0.03) * fmin(1.0, (burst[b][1] - u) / 0.06);
        }
        double w = noise();
        lp += (w - lp) * 0.35;
        double hp = lp - prev;                                     /* a hiss, not a roar */
        prev = lp;
        x[i] = hp * env;
    }
    finish(PCM_SPRAY, x, n, 9000);
    free(x);
}

/* a fist through the air: noise through a band that rises and falls, swelling and fading */
static void make_swish(void)
{
    int n = (int)(0.20 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0, lp2 = 0, prev = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE, u = t / 0.20;
        double env = sin(M_PI * pow(u, 0.7)), fc = 700 + 2600 * sin(M_PI * u);
        double a = 1 - exp(-2 * M_PI * fc / RATE);
        lp += (noise() - lp) * a;
        lp2 += (lp - lp2) * a;
        double bp = lp2 - prev;                                    /* the band: low-passed, then its change */
        prev = lp2;
        x[i] = bp * env * env;
    }
    finish(PCM_SWISH, x, n, 7000);
    free(x);
}

/* a fist landing: a dull low thump with a slap on top */
static void make_punch(void)
{
    int n = (int)(0.22 * RATE);
    double *x = calloc((size_t)n, sizeof *x), lp = 0, ph = 0;
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE, f = 55 + 70 * exp(-t / 0.03);
        ph += 2 * M_PI * f / RATE;
        lp += (noise() - lp) * 0.25;
        x[i] = 0.9 * sin(ph) * exp(-t / 0.06) + 0.7 * lp * exp(-t / 0.012) + 0.35 * noise() * exp(-t / 0.004);
    }
    finish(PCM_PUNCH, x, n, 16000);
    free(x);
}

/* no: a short low buzz, twice */
static void make_deny(void)
{
    int n = (int)(0.32 * RATE);
    double *x = calloc((size_t)n, sizeof *x);
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double t = i / RATE, on = (t < 0.12 || (t > 0.17 && t < 0.29)) ? 1 : 0;
        double s = sin(2 * M_PI * 150 * t), sq = s > 0 ? 1 : -1;
        x[i] = on * (0.6 * sq + 0.4 * sin(2 * M_PI * 300 * t));
    }
    finish(PCM_DENY, x, n, 6000);
    free(x);
}

const s16 *pcm_sfx(int id, int *n)
{
    if (id < 0 || id >= PCM_COUNT) { *n = 0; return NULL; }
    if (!buf[id]) {
        switch (id) {
        case PCM_SHOT: make_shot(); break;
        case PCM_HIT_METAL: make_hit_metal(); break;
        case PCM_HIT_DIRT: make_hit_dirt(); break;
        case PCM_HIT_WATER: make_hit_water(); break;
        case PCM_EMPTY: make_empty(); break;
        case PCM_RELOAD: make_reload(); break;
        case PCM_WRECK: make_wreck(); break;
        case PCM_SHOT_FOE: make_shot_foe(); break;
        case PCM_FLESH: make_flesh(); break;
        case PCM_HURT: make_hurt(); break;
        case PCM_RICO: make_rico(); break;
        case PCM_UZI: from_shot(PCM_UZI, 1.45, 0.025, 0.06, 0, 17000); break;
        case PCM_SHOTGUN: from_shot(PCM_SHOTGUN, 0.78, 0.12, 0.30, 0.7, 23000); break;
        case PCM_PUMP: {
            static const double c[][4] = { { 0.00, 1300, 0.014, 1.0 }, { 0.01, 500, 0.03, 0.6 }, { 0.17, 2100, 0.010, 1.0 },
                                           { 0.18, 800, 0.025, 0.8 } };
            make_clicks(PCM_PUMP, c, 4, 0.35, 10000);
            break;
        }
        case PCM_SHELL: {
            static const double c[][4] = { { 0.0, 2500, 0.009, 1.0 }, { 0.005, 950, 0.03, 0.6 } };
            make_clicks(PCM_SHELL, c, 2, 0.15, 7000);
            break;
        }
        case PCM_PICKUP: {
            static const double c[][4] = { { 0.0, 1800, 0.012, 1.0 }, { 0.07, 2700, 0.012, 1.0 }, { 0.075, 700, 0.03, 0.5 } };
            make_clicks(PCM_PICKUP, c, 3, 0.25, 8000);
            break;
        }
        case PCM_CASH: make_cash(); break;
        case PCM_SWISH: make_swish(); break;
        case PCM_PUNCH: make_punch(); break;
        case PCM_SPRAY: make_spray(); break;
        case PCM_DENY: make_deny(); break;
        case PCM_TICK: {
            static const double c[][4] = { { 0.0, 3200, 0.004, 1.0 } };
            make_clicks(PCM_TICK, c, 1, 0.05, 5000);
            break;
        }
        default: break;
        }
    }
    *n = len[id];
    return buf[id];
}

/* developer aid: TD3_PCM_DUMP=dir writes every sound as dir/pcmN.wav (mono, 16 bit) at the first one played */
static void dump(void)
{
    static int done;
    const char *dir = getenv("TD3_PCM_DUMP");
    if (done || !dir) return;
    done = 1;
    for (int id = 0; id < PCM_COUNT; id++) {
        int n;
        const s16 *x = pcm_sfx(id, &n);
        char path[512];
        snprintf(path, sizeof path, "%s/pcm%d.wav", dir, id);
        FILE *f = fopen(path, "wb");
        if (!f || !x) { if (f) fclose(f); continue; }
        u32 bytes = (u32)n * 2, rate = HOST_AUDIO_RATE, brate = rate * 2, v32;
        u16 v16;
        fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        v32 = 16; fwrite(&v32, 4, 1, f); v16 = 1; fwrite(&v16, 2, 1, f); fwrite(&v16, 2, 1, f);
        fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); v16 = 2; fwrite(&v16, 2, 1, f); v16 = 16; fwrite(&v16, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(x, 2, (size_t)n, f);
        fclose(f);
    }
}

void pcm_sfx_play(int id, double vol, double pan)
{
    dump();
    int n;
    const s16 *s = pcm_sfx(id, &n);
    host_pcm_play(s, n, vol, pan);
}
