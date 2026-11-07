#pragma once
/* OG: (TOWNS.md "Real villages") houses added to a leg, copies of the leg's own house models, placed where a village
 * was planned. world_build_visible emits them in the cells it builds (village_cell), as model_place emits the static
 * objects; the enhanced view's far ring and its villages (enh_far.c, enh_town.c) read them with village_house. */
#include "types.h"

#include <stdbool.h>

enum { VSV_NONE, VSV_GARAGE, VSV_GUNS, VSV_PUB, VSV_POLICE };   /* the same as enh_town.c's services */

typedef struct {
    u8 model;           /* object model (objects set) */
    bool parked;        /* the parked objects' model format (8-byte header), else the statics' (4) */
    u16 px, pz, py;     /* the origin: position units (x4 / 4), height units (the ground under the footprint) */
    u8 rot;             /* model_place's turn (quarter turns) */
    int sv;             /* a service planned there (VSV_*) */
} VillageHouse;

/* the current leg's added houses (none in the menus) */
int village_count(void);
const VillageHouse *village_house(int i);
/* a service given to one of the leg's own houses (the one nearest (x, z), true x4): VSV_NONE past the last */
int village_tag(int i, double *x, double *z);
/* the planned village's name on this leg, or NULL */
const char *village_name(void);
/* world_build_visible: a new build (the camera's cell now), then the added houses of each cell it builds (ch_x, cz:
 * its x / z high bytes & 7Ch / 3Ch). The game's vertex buffer (1584) holds little more than its own scenery, so it
 * takes only the houses in the camera's cell (what the car and the walker can meet: entering a cell builds anew); the
 * enhanced view draws the others (village_emitted false) in its far ring. */
void village_build_begin(void);
void village_cell(u8 ch_x, u8 cz);
bool village_emitted(int i);
/* a village planned at the leg's start (enh_town.c, TOWNS.md "More villages"): its houses, each the middle of its
 * walls (true x4) and its turn; placed (level ground only) with the planned ones from the next build */
typedef struct { u8 model; bool parked; double x, z; u8 rot; } VillageAuto;
void village_set_auto(const VillageAuto *h, int n);
/* a model's half extents (its walls, its own frame) and vertex count */
bool village_model_size(u8 model, bool parked, double *hx, double *hz, int *nv);
/* the ground of the leg's tiles at (X, Z) (true x4 units), height units; -1e9 none (developer aids) */
double village_ground(double X, double Z);
/* the face type of the last village_ground (0Eh water, 10h..14h road), -1 none */
int village_ground_type(void);
