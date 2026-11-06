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
units (2 m) high. Its footprint is turned as `model_place` turns it (the heading's high byte; statics by quarter
turns). Houses within 45 m of each other are one village; every village gets a name from a list, the same for the
same leg every time ("OAK HOLLOW", "SAGE JUNCTION" ...). The first leg of the first scene has 19 houses, the others
14 to 28 (the fourth two villages of eight).

The door is on the side of the house nearest a road face (types 10h..14h of the map tiles around it), not into
another house and not on a narrow end.

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
one, a new leg new offers. Goals are houses without a service in other villages, at most 40 m from a road and
reachable by road.

| Job | From | What | Time | Pay |
|---|---|---|---|---|
| PARCEL | pub | to a house 120 to 700 m away, on foot into the ring at its door | 40 s + 1 s per 10 m by road + 1 s per 3 m on foot | $60 + $0.30 a metre by road, at most $500 |
| EXPRESS PARCEL | pub | the same | 30 s + 1 s per 13 m + 1 s per 4 m | 1.6 times |
| HOT GOODS | pub | the same; taking it, the police know at once: two stars | as a parcel | 2.2 times |
| TAXI | pub | the fare gets in with you; stop by the goal's door (within the road's distance + 25 m, slower than 5); a crash on the way and the fare jumps out | 1.15 times a parcel's (from getting in) | 1.2 times |
| RACE | pub | a local races you to a village ahead (300 to 1300 m by road, nearer the leg's finish): get in, a rival is taken (the race's opponent within 150 m first, else a car of the traffic heading there), 3 2 1 GO; the first within 30 m of the goal's road wins (a traffic rival gets the fastest speed class; a rival shot to a wreck loses) | no rival in 45 s: off | $200 to $400 |
| GETAWAY | pub | pick up the loot at one house (three stars at once), drop it at another; the drop takes it only with the police lost (hidden, or resprayed) | none | $500 to $900 |
| BOUNTY | police | a gang of three at a house of another village, 200 m on, comes out within 90 m; all three down, back to the police station that gave the job: walk into its ring | none | $300 to $500 |
| STOLEN CAR | police | at the village of the theft (within 150 m) a car of the traffic nearby drives off at the fastest speed class; two hits stop it (five wreck it: failed), the thief runs; walk (or drive) to it. More than 500 m away: it got away | none | $250 to $400 |

The way by road is measured on the leg's road network: map cells with road faces, joined where the road of both
reaches their common edge at the same place; a breadth-first search, 41 m a cell. On the first leg of Cape Cod -
Niagara it is 1 to 3.5 times the straight distance (the road goes round the bays); the car does about 19 m/s on the
road at full speed (measured), a parcel asks about 8 m/s on average, an express parcel about 13. The leg's finish
(the cell with the gas station) tells which villages are ahead for a race.

While a job runs, a line at the top says what and where, with the time left (red the last 15 s; a race also the
rival's distance under it), and an arrow round the centre points to the goal with its distance ("DROP", "LOOT",
"GANG", "POLICE", "FINISH", "STOLEN", "CAR", "THEFT"): the only arrow in the game, as the player asked for the job.
A pale yellow ring marks the door to walk into. A job is lost when the time runs out ("TOO LATE - JOB FAILED"), when
the player is shot dead, and with a new leg ("JOB LOST").

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

* `TD3_TOWN_DUMP=1`: the leg's houses, villages and services at the start of every leg.
* `TD3_TOWN_GOTO=n` (1 garage, 2 guns, 3 pub, 4 police): once out of the car, the walker is put before the nearest
  such door; `TD3_TOWN_GOTO=1,bay`: at the garage's bay, with the car in it (Y / E gets in).
* `TD3_MONEY=n`: the money a race starts with.
* `TD3_FOES_DEBUG=1` also logs the shops opened.
