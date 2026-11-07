# Villages, shops and money (fork og8)

The houses along a leg become villages with names, and some of them hold a service: a garage, a gun shop, a pub
or a police station. Money is found, earned and paid. Marked `OG:` in `src/enhanced/enh_town.c` (the villages,
the services, the money, the menus), with hooks in `enh_foes.c` (cash, the vest, the bail), `enh_gun.c` (the
shop's stock), `host.c` (the menu's controls), `weapon.c` / `flow.c` (a new race, a leg finished) and
`pcm_sfx.c` (the till, the spray gun); the planned villages' added houses in `src/game/village.c`, emitted by
`render_world.c` (`world_build_visible`) and `enh_far.c`.

## The villages

Nothing is added to the scenery: the houses are the game's own. At the start of every leg its objects are read
(as `enh_far.c` does: static objects and parked ones, their object models): an object is a house when its solid
faces (type 1) make a footprint of at least 1.8 m a side and 6 m², at most 25 m long, and stand at least 90 height
units (2 m) high, but not higher than 2.5 times its narrow side: a tower is no house (the power line along Cape Cod
- Niagara's fourth leg, eighteen pylons of model 27, 2.6 m wide and 12 m high, was a "village" with a gun shop, a
pub and a garage). Its footprint is turned as `model_place` turns it (the heading's high byte; statics by quarter
turns). Nothing standing over the road is a house: more than a quarter of the walk grid's points under the footprint
road faces, and it is none. The traffic lights' gantry over the crossroads (model 5, 8.5 m square, its posts and beam
solid) stood on the road on eight of the ten legs and, as the houses nearest the road, it was a pub, a gun shop, a
garage or a police station sixteen times: the sign hung on the lights, the ring lay on the crossroads (Cape Cod -
Niagara's fifth leg, 2026-11-07). Under the houses the road's share is 0 on every leg, under the gantry 0.90 to 0.98.
Houses within 45 m of each other are one village; every village gets a name from a list, the same for the same leg
every time ("OAK HOLLOW", "SAGE JUNCTION" ...). Of their own, Cape Cod - Niagara's legs have 8 to 27 houses, Pacific
- Yosemite's 3 to 9; with the planned villages ("More villages" below) 27 to 34 and 12 to 24, and every leg has two
of each service.

All the services and the houses of the ten legs were looked at from the road (`TD3_TOWN_TOUR`, 2026-11-07): the
house models are the game's houses, barns, sheds (model 50, a long grey shed: no roof faces of type 1) and a ranch
house each; every service stands by the road with its sign and ring before the door. QUAKER HILL's garage on Cape Cod
- Niagara's fifth leg stands 9 m from the road behind a rise: from the road its roof and silo show, the sign hardly.

### More villages

At a leg's start (`plan_villages`, before the leg's houses are grouped) houses of the leg's own models are planned
where the main road runs straight east-west or north-south (the statics turn by quarter turns) for at least 32 m:
on both sides, their fronts to the road, 3.5 m back from its edge, 4.5 to 9.5 m apart. A house goes only on level
open ground (no water, road, wall or hole under it or 1.5 m round it, the ground within 10 height units), not on an
object of the leg (a church, its tower, a pylon, a boat; 1.5 m round it) nor on a tree's, rock's or sign's trunk
(the tiles' sprite children, thinned by the vegetation seed as the game does, and the leg's own sprites), not on
another house, not within 30 m of the start. A stretch gives a village of at most seven houses within 80 m; the
three with the most houses, 300 m apart (a hamlet of the leg's own may grow; the nearer to the start first), are
built. `game/village.c` places them as PILGRIM COVE's (level ground checked again) and the game builds them as its
own. The models and their fronts (from the leg's own houses that face the road, and seen): Cape Cod - Niagara the
porch house 55 (front -z), the small house 54 (-z), the long house 50 (+x), the barn 51 (+x); Pacific - Yosemite the
white house 52 (+x), the blue house 53 (+x), the ranch house 54 (-z), the round-roofed barn 51 (+z, its doors).

The game's vertex buffer holds 1584: the added houses of a map cell together have at most 420 vertices (PILGRIM
COVE's fullest cell has 446; a Pacific house has 105 to 156, a Cape Cod one 24 to 128), and `village_cell` builds one
only while the buffer has room for the cells built after it (`VERT_ROOM`, 1100): otherwise the far ring draws it, not
solid. Measured on the tour of all the added houses (2026-11-07): at most 1434 vertices, every house built solid.
Looked at from the road: every front to the road, none on a tree or by a church. Two to three new villages a leg
(Cape Cod's first one: by PILGRIM COVE's only stretch, five houses).

The door is on the side of the house with the shortest way on foot from the main road (below), not into another
house and not on a narrow end. A house with no such way to any side (across a river, below a cliff, by a road of its
own) gets no service and is no job's goal.

### On foot from the main road

The leg's road network (map cells with road faces, joined as for the way by road, see "Jobs") falls into parts; the
biggest is the main road, the leg's course (on all ten legs of both scenes the start and the finish are on it); the
others are bits of road with no way to it: the figure-eight track on Pacific - Yosemite's third leg, side roads
that end. From the main road the way on foot is searched over the leg's ground: the map tiles sampled every 1.28 m,
at each point the highest face that is not steep (as `foot.c` takes the ground) and not a zone; water (0Eh) and holes
are not walked on, the walls (steep faces, type-1 lines, as `foot.c`'s `resolve`) block the points whose ground they
meet between 18 and 78 units over it, a step between neighbouring points rises at most 160 units, and a diagonal step
does not cut a corner. Dijkstra over the eight neighbours, out to 70 m; each point keeps the way's length and the road
cell it starts at (a job's way by road goes from there). 30 to 50 ms at the leg's start (on this PC). The old rule,
40 m straight to the nearest road face, took a road across a river or a bit of road with no way to it.

## The services

Each leg gets each service once, on a leg with 12 houses or more twice (the second at least 300 m from the first):
in the bigger villages first, few services in one village, the door near the road, apart from the same service.
The same leg always has the same services in the same houses.

| Service | Colour | Inside |
|---|---|---|
| GARAGE | cyan | car armour plating $500 (shots hurt less than half as much in the car); the respray (below) |
| GUNS | red | pistol rounds 24 $30; uzi with 64 rounds $350, then 64 rounds $80; shotgun with 16 shells $400, then 16 shells $60; body armour $150 |
| PUB | amber | beer $5 (+10 health, and the view sways for a while), burger and fries $15 (+35), steak dinner $40 (full health) |
| POLICE | blue | pay your fine: $150 a star, the wanted level goes |

A service shows a sign over its door (a board in its colour with its name, as large as fits, only from the front)
and a ring painted on the ground before the door. Nothing else points to it: no arrows, no glow, the services are
found by looking (as in the games of the time). Coming within 45 m of a village shows its name.

**Shopping.** Walk into the ring: the menu opens and the walker stands at the counter. Up / down (left stick,
d-pad, W S, arrows) choose, A / Space / Enter buys, B / Y / E / Backspace / Esc leaves (or LEAVE). What is held
while walking in (the stick, W) counts only after it was let go. A purchase that is not possible says why ("NOT
ENOUGH MONEY", "YOU CAN'T CARRY MORE", "YOU'RE NOT HUNGRY"). A hit at the counter closes the menu. The shops (not
the police) stay closed while gunmen on the alert are within 60 m or an officer sees the player ("CLOSED -
TROUBLE OUTSIDE").

**The respray.** With a wanted level, drive into the garage's bay (the dashed ring, further out than the door's)
and stop there: $100 a star, the screen mists over, the stars go ("NEW PAINT - NOBODY KNOWS YOU"). Not while the
police see the player (a police car's or an officer's line of sight) and not without the money. The car's colour
is not changed.

## Money

| In | Out |
|---|---|
| $150 at the start of a race (`TD3_MONEY` for another amount) | the shops |
| a gunman's cash, $30 to $120, dropped with his gun (a green bundle, picked up like the guns; no glint) | a ticket from the original's pull-over: $50 |
| an ambush cleared: $250 | shot dead: $100 (hospital) |
| a civilian down drops a wallet ($5 to $40) half the time | busted: $200 bail (below) |
| a leg finished: $300 | the respray, the fine |

The money is shown top right on foot, and in the car for a while after it changed, with the last changes under
it. It lasts the race (lives lost and legs included) and starts afresh with a new race.

**Bail.** Busted ("BUSTED!" at one star) with $200 or more, the player pays the bail instead of losing a life:
the weapons are taken (the start's pistol again), the police let go ("BAILED OUT - WEAPONS TAKEN"). Without the
money a life is lost as before.

**The vest** (body armour, 0 to 100, a blue bar over the health) takes three quarters of every hit until it is
used up; it is lost with a life.

## Jobs (og9)

Made up at random, one at a time. The pub's barkeeper offers parcels, a race, a getaway or a fare (the first line of
the pub's menu), the police station a bounty or a stolen car (the line after the fine); the line shows what it pays,
the line under the menu the where (and how long). An offer stays until it is taken; the next visit after brings a new
one, a new leg new offers. Goals are houses without a service in other villages whose door is at most 40 m on foot
from the main road ("On foot from the main road" above): every goal can be driven to and walked to, none lies across
a river or by a road that leads nowhere (a getaway's loot was by a pylon below a cliff by the river before). With
none of those, any reachable house 60 m on, a service too but never a police station: with the gantry gone, Pacific -
Yosemite's first and third legs put a getaway's drop at the police station, whose car saw the drop for ever. A job
that cannot be made there (a getaway on a leg with no drop) is another kind of job. Before the planned villages,
Pacific's fifth leg had no getaway and no bounty, and only its fourth leg a race ahead; now every leg has them.

| Job | From | What | Time | Pay |
|---|---|---|---|---|
| PARCEL | pub | to a house 120 to 700 m away, on foot into the ring at its door | 40 s + 1 s per 10 m by road + 1 s per 3 m on foot | $60 + $0.30 a metre by road, at most $500 |
| EXPRESS PARCEL | pub | the same | 30 s + 1 s per 13 m + 1 s per 4 m | 1.6 times |
| HOT GOODS | pub | the same; taking it, the police know at once: two stars | as a parcel | 2.2 times |
| TAXI | pub | the fare gets in with you; stop by the goal's door (within the road's distance + 25 m, slower than 5); a crash on the way and the fare jumps out | 1.15 times a parcel's (from getting in) | 1.2 times |
| RACE | pub | a local races you to a village ahead (300 to 1300 m by road, nearer the leg's finish): get in, a rival is taken within 60 m (the race's opponent first, else a car of the traffic heading there) and waits on the road, the arrow on it ("RIVAL"; before, the nearest car within 150 m was taken wherever it was and was never seen); pull up beside it (15 m) slowly: ON YOUR MARKS, 3 2 1 GO; the first within 30 m of the goal's road wins (a traffic rival gets the fastest speed class; a rival shot to a wreck loses) | no rival in 90 s: off | $200 to $400 |
| GETAWAY | pub | pick up the loot at one house (three stars at once), drop it at another; the drop takes it only with the police lost (hidden, or resprayed) | none | $500 to $900 |
| BOUNTY | police | a gang of three at a house of another village, 200 m on, comes out within 90 m; all three down, back to the police station that gave the job: walk into its ring | none | $300 to $500 |
| STOLEN CAR | police | at the village of the theft (within 150 m) a car of the traffic nearby drives off at the fastest speed class; two hits stop it (five wreck it: failed), the thief runs; walk (or drive) to it. More than 500 m away: it got away | none | $250 to $400 |

The way by road is measured on the leg's road network: map cells with road faces, joined where the road of both
reaches their common edge at the same place; a breadth-first search, 41 m a cell. On the first leg of Cape Cod -
Niagara it is 1 to 3.5 times the straight distance (the road goes round the bays); the car does about 19 m/s on the
road at full speed (measured), a parcel asks about 8 m/s on average, an express parcel about 13. The leg's finish
(the cell with the gas station) tells which villages are ahead for a race.

While a job runs, a line at the top says what and where, with the time left (red the last 15 s; a race also the
rival's distance under it), and an arrow round the centre shows the way with its distance ("DROP", "LOOT",
"GANG", "POLICE", "FINISH", "STOLEN", "CAR", "THEFT"): the only arrow in the game, as the player asked for the job.
It follows the road: from the player's road cell the way by road to the goal's (a breadth-first search back from the
goal over the road network), the arrow at the place where that way crosses a cell's edge 50 m on or more, the
distance the way's length; within two cells of the goal by road, 120 m straight, or off the road network it points
straight at the goal. On Cape Cod's first leg a fare to SALT MARSH is 652 m west as the crow flies and 1.9 km by road
(round the bay): the arrow first points east, along the road. It reckons in true map units: the camera's place
wraps every 655 m east-west, and the first arrow, reckoned from it, turned a goal over 327 m east or west round (the
fare above showed "DROP 3M", or no arrow at all).
A pale yellow ring marks the door to walk into. A job is lost when the time runs out ("TOO LATE - JOB FAILED"), when
the player is shot dead, and with a new leg ("JOB LOST"). A job's door at a service (its ring, the bounty's police
station) opens no shop while the job wants it; walked out and in again, the shop opens. Pacific - Yosemite's fifth
leg has only services left for goals: a getaway's loot at the gun shop opened the shop's menu, and the menu holds
the walker.

Feasibility, 2026-11-07 (all ten legs, headless with `TD3_JOB_GOTO` / `TD3_JOB_SHOOT`, and `TD3_JOB_CHECK`): every
job type comes to its end on every leg that offers it. Pacific - Yosemite's second leg has no race ahead (a parcel
instead), its fifth no police station (no bounty, no stolen car).

Developer aids: `TD3_JOB=n` (1 parcel, 2 express, 3 hot goods, 4 bounty, 5 race, 6 getaway, 7 stolen car, 8 taxi:
one at once at the start of a leg, from the nearest pub or police station), `TD3_JOB_TYPE=n` (every offer made is
that one), `TD3_JOB_GOTO=1` (out of the car, the walker is put before the goal, once a stage; a gang's 40 m off, a
theft's village 60 m off; the taxi and the race: the car too), `TD3_JOB_SHOOT=1` (the stolen car takes two hits, the
gang falls, 2 s after), `TD3_JOB_TIME=s` (a parcel's time); `TD3_FOES_DEBUG=1` logs the jobs and every offer made
(way by road, straight, on foot, time); `TD3_POS_LOG=1` the player's place and the car's speed once a second.

## Real villages (planned, `src/game/village.c`)

Where a village is planned, houses are added: copies of the leg's own house models, each placed by the middle of its
walls with its front to the road, on the ground the leg's tiles have there. Only on level ground: where the ground
under the footprint and 1.5 m round it (a 5 x 5 grid) rises more than 12 height units (28 cm), or meets water or a
road, the house is left out (a house on a slope was half sunk into the hill). The fronts come from
the leg's own houses whose doors face the road: the porch house (model 55) and the long house (50) are parked
objects' models (an 8-byte header), the barn (51), the small house (54) and the silo (52) statics' (4 bytes). A plan
entry with model 0 gives a service to the leg's own house at that place.

`world_build_visible` emits them in the cells it builds as `model_place` emits the static objects, so the original
renderer draws them, the car crashes into their walls (`near_face_hits`) and the walker meets them. The game's vertex
buffer holds 1584 and its own scenery round a village already takes about 750, so it takes only the houses in the
camera's cell (entering a cell builds anew); the enhanced view draws the others in its far ring (`enh_far.c`). Measured
at the first village: at most 988 vertices and 793 faces (1452 when the game took all of them).

**PILGRIM COVE** (Cape Cod - Niagara, Pilgrim Way, by the start): the main street runs 75 m east from the bridge
between two dunes at either end and bends south; the village takes its level middle. North of it, east of the village
pond, the gun shop (the long house), the pub (a porch house) and a small house; south of it two small houses, a porch
house and the garage (the barn), its bay at the road; a farm (barn and silo) on the level ground beyond the bend, by
the leg's own house there, the police station. Every leg's other services are spread as before (a planned one counts as the first round's).

The villages' names are New England ones on Cape Cod - Niagara, Western ones on Pacific - Yosemite.

Developer aids: `TD3_TOWN_MAP=file.ppm` (the leg from above: tiles by type, houses, doors, the car's start; 1 px = 32
units), `TD3_TOWN_FLAT=x0,z0,x1,z1` (the ground's level there as characters, 2 m each, with
`TD3_TOWN_DUMP`, which also lists each planned house's ground), `TD3_FACES_LOG=1` (the game's vertex and face counts once a second), `TD3_NO_VILLAGE=1` (no houses added).

## Developer aids

* `TD3_TOWN_DUMP=1`: the leg's houses, villages and services at the start of every leg; the road network's parts
  and the main road (the start and the finish on it?), how much of the leg is within 70 m on foot of it, the walk
  grid's time, and for each house its road straight and on foot ("NO GOAL (was one)": a goal by the old rule only),
  its roof's share of the footprint (its solid faces seen from above) and the road's share under it; the header
  counts the models over the road that are no houses. With `TD3_TOWN_MAP` the map shows the ground within reach
  lighter, the walls black, the houses with no way magenta.
* `TD3_TOWN_TOUR=dir`: once out of the car, the walker goes to every house of the leg in turn (20 m a frame) and a
  picture of each is saved: `tour_sS_lL_hNNa.bmp` from the road nearest its door (at least 7 m from the house),
  `...b.bmp` 30 m out, 40 degrees to the side. `TD3_TOWN_TOUR_FROM=n` starts at house n, `TD3_TOWN_TOUR_R` is the
  distance of a view from the door when no road is near. A shop's menu does not open on the way. Terrain hides some
  houses from the side view: MAPLE CORNERS' pub on Cape Cod's third leg stands in a dip by the road.
  `TD3_TOWN_TOUR_NEW=1`: only the planned villages' houses.
* `TD3_FACES_LOG=1` also logs every added house the game builds (at how many vertices) and any it leaves to the far
  ring for want of room ("not solid").
* `TD3_SCENE=n`: the race on scene n (0 Pacific - Yosemite, 1 Cape Cod - Niagara) without the course selection;
  not written to the play disk. With `TD3_START_LEG` every leg can be dumped headless.
* `TD3_JOB_CHECK=1`: at every leg's start, every offer each pub and police station can make (the same choice of goals
  as the game's): each goal's way between the doors on the walk grid (the road preferred, a step off it counted 8
  times; road to road at any height step, a bridge's deck lies 190 units over its ramps), on the road and off it,
  against the job's time: the speed it asks on the road (15 s to get to the car, 5 m/s on foot at the end), over 13
  m/s TIGHT, over 17 TOO SHORT; the races ahead, every loot and its drops, the gang's place. `=2` also traces the
  ways. The grid does not see tunnels (their road lies under the hill's faces) nor pass the portals' walls: the two
  TIGHT ones on Cape Cod (its third and fifth legs) go through them, the road network does not (it counts road faces in a cell
  whatever lies over them). 2026-11-07 on all ten legs: every service reachable, every goal reachable, the way by
  road 0.69 to 1.13 times the cells' estimate (median 0.92: the times are on the generous side).
* `TD3_WG_DUMP=x0,z0,x1,z1`: the walk grid there as characters (road, ground, water, walls, within reach) and the
  heights in tens.
* `TD3_TOWN_GOTO=n` (1 garage, 2 guns, 3 pub, 4 police): once out of the car, the walker is put before the nearest
  such door; `TD3_TOWN_GOTO=1,bay`: at the garage's bay, with the car in it (Y / E gets in).
* `TD3_MONEY=n`: the money a race starts with.
* `TD3_FOES_DEBUG=1` also logs the shops opened.
