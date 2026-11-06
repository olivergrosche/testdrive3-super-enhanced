# Civilians (fork og6)

People walk along the roads and stroll round the buildings. They get out of the way of a fast car, run from
gunfire and ambushes with their hands on their heads, and can (alas) be shot or run over. Marked `OG:` in
`src/enhanced/enh_foes.c` (they share the gunmen's body, `body()`).

## Where they come from

Up to six at a time (`TD3_CIVS=n` for fewer), one new one every 1.5 s while there are fewer, from the newest
snapshot's faces; the country roads stay quiet: at most two walk along roads, and a new one goes there only 30 % of
the time:

* **along a road**: a road face (types 10h..14h) 3500 to 9000 units from the player; from its middle the
  nearest edge (eight directions, 60-unit steps, up to 900) and 90 units beyond it, walking along the road one way
  or the other, the road on the side it came from;
* **round a building** (the rest): a tall (25 or more) steep face of type 3 (the buildings' walls) 2500 to 9000 away; a
  spot of open ground (not road, not water) 300 to 650 from it.

Not in sight and nearer than 5000 (no one appears out of nowhere in front of the player), not within 3000 of an
ambush. They go when 15000 away (the dead when 9000 away), and with a new leg or a death.

## How they walk

1.15 to 1.55 m/s (115 to 155 units a second), three looks a second: the ground under them (`surf_at`: the highest
walkable face, not a wall, not a vehicle), 220 ahead (water, a drop of more than 45 or no ground: they turn back) and
a line of sight 160 ahead at chest height (a wall, a car, a tree: they turn aside). Along a road they keep its edge:
on the road they turn off it, with no road 160 to their road side they turn towards it (more when it is not even
340 away). Round a building they walk to a spot within 250 to 650 of it, stand 2 to 6 s, and pick another.

## What frightens them

* a shot from the player within 5000, a gunman's ambush alerted within 7000, or someone near being run over: they
  run (3.8 m/s, hands on their heads, bent forwards) away for 7 to 13 s, catch their breath 1.5 to 3.5 s and go on;
* the player's car faster than 25 coming at them (within 1700 ahead and 260 to the side): they jump aside for
  1.4 s;
* hit (the weapons' damage, `WEAPON.md`; 100 health; the fists: three punches) they flinch and run; at nothing
  they fall, and half the time leave a wallet ($5 to $40, `TOWNS.md`). A car faster than 8 within 190 runs them
  over.

## How they look

The gunmen's body (`FOES.md`) with a bare head: skin (five tones), brows, hair (black, brown, blonde, grey, red;
long for some), a cap with a peak for a quarter of the men; a shirt (ten colours) and trousers (six), or a dress
(30 %) down to the knees with bare shins; shoes black or brown; 90 to 105 % of 176 cm. Walking, the arms swing
against the legs; running, the elbows are bent.

## Developer aids

* `TD3_CIVS=n`: at most n of them (0: none).
* `TD3_CIVS_NEAR=1`: new ones 600 to 2500 away, also in sight (to look at them).
* `TD3_FOES_DEBUG=1`: also every civilian placed, and why none could be.
