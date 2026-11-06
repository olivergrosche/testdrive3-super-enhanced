#pragma once
/* OG: the pistol's sampled sounds (WEAPON.md), synthesised on first use (pcm_sfx.c). */
#include "types.h"

enum { PCM_SHOT, PCM_HIT_METAL, PCM_HIT_DIRT, PCM_HIT_WATER, PCM_EMPTY, PCM_RELOAD, PCM_WRECK,
       PCM_SHOT_FOE, PCM_FLESH, PCM_HURT, PCM_RICO,
       PCM_UZI, PCM_SHOTGUN, PCM_PUMP, PCM_SHELL, PCM_PICKUP,
       PCM_CASH, PCM_SPRAY, PCM_DENY, PCM_TICK,         /* TOWNS.md: the till, the spray gun, no, the menu's cursor */
       PCM_SWISH, PCM_PUNCH,                            /* WEAPON.md: a fist through the air, a fist landing */
       PCM_COUNT };

/* the samples of a sound (built on the first call) */
const s16 *pcm_sfx(int id, int *n);
/* plays it (host_pcm_play): vol 0 .. 1, pan -1 .. 1 */
void pcm_sfx_play(int id, double vol, double pan);
