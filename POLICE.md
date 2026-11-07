# The police and the car thief (fork og7)

Crimes seen by someone bring a wanted level of one to three stars; the police chase the player with the original's
pursuit, get out when they catch up, and search on foot where they last saw the player. Out of their sight long
enough, the player is lost to them. A car left alone long enough is stolen. Marked `OG:` in `src/enhanced/enh_foes.c`
(the wanted level, the cops, the thief), `src/game/weapon.c` (the police cars) and `police_update`
(`sim_traffic.c`, the caught-up hook); `foot.c` moves the stolen car.

## Crimes

| Crime | Stars |
|---|---|
| Shots within 5000 of police; a civilian hit; a vehicle wrecked | 1 |
| A civilian killed or run over; a police officer or a police car hit | 2 |
| A police officer killed or run over; a police car wrecked | 3 |

A crime counts only when someone saw it: police (a car or an officer) within 9000 units of the player, or a civilian
within 5000. The new level is the crime's, or one more when the player already has that many (from two up). The
police then know where the player is. Shooting the ambushes' gunmen is no crime.

## The pursuit

* **Units.** Once a second every police car within 25000 gives chase (the original's chase: flag 4000h, speed
  class 3, fast along the lanes and overtaking; a parked one is woken). When fewer than stars + 1 are chasing, a
  traffic car 6000 to 16000 away and out of sight (not within 12000 straight ahead) becomes a police car (the model
  of this leg's police, `weapon_make_police`) and joins.
* **Caught up.** When a chasing car has been close for 30 frames (the original's pull-over), with a wanted level
  it does not hand out a ticket: it stops (`weapon_police_stop`: it stays, `weapon_frozen`) and two officers get
  out at its sides.
* **The officers** (navy uniforms, caps, pistols) see the player within 8000 along a line of sight. At one star they
  run to arrest: an officer within 150 of the walker, or 260 of the stopped car, for 1.2 s: "BUSTED!", and a life
  is lost the game's way a second later (TOWNS.md: with $200, the bail is paid instead and the weapons are taken). From two stars they shoot (bursts of two, a little less sure than the
  gunmen). They can be shot (100 health) and run over.

## The search

* The police see the player: an officer, or a police car with a line of sight from its roof within 7000 (four
  checks a second). Seen, the last known place is where the player is. Every check of a car against the player or
  the last known place first asks `car_true_near`: the car within 320 m east-west in true map units. The wrapped
  places (the snapshot's, a car's `(u16)(x << 2)`) part every 655 m on a map 1310 m wide, and a police car 632 m
  east of the player looked 22 m off: it "saw" the player for ever and a getaway could not end (Pacific - Yosemite's
  second leg, drop at RED MESA, 2026-11-07). The same guard on the witnesses, the cars that join the chase, the ones
  made police and the ambush's car across the road.
* The stars flash while nobody sees the player. Once the police have been near the last known place (a car within
  4000, any officer, or 45 s after the crime), the time out of sight counts: 12 s at one star, 20 at two, 30 at
  three, and they lose the player ("LOST THEM"): the chases end (`police_pursuit_clear`), the stopped cars drive on,
  the officers walk back to their cars and go.
* Out of sight for more than 3 s, a chasing car within 2500 of the last known place stops there and its crew
  searches on foot: they run to the place and then walk from spot to spot within 300 to 1500 of it.

TOWNS.md: a garage's respray ($100 a star, not in the police's sight) and a police station's fine ($150 a star)
also end it.

The police cars keep to the lanes as in the original: off the road, behind a hill or a house, in the trees, a hidden
player can only be found by the officers on foot.

## The car thief

Once the walker has been more than 2500 from the parked car for 40 s (`TD3_THIEF=s` for another time), a civilian
within 6000 of the car, if there is one (so it happens now and then, not always), walks to it, gets in ("YOUR CAR
IS BEING STOLEN!") and drives off along the car's heading (up to 16 m/s) until it is out of the player's view or 60 m
away (at most 7 s); then it is left on the road face nearest to 70 m on from where it was taken (45 to 110 m, the
game's cells and the far ring searched; with none, 50 to 70 m straight on), `foot_move_car`: the saved car state and
object 0. On foot a yellow chevron round the centre points to the car with its distance ("CAR 95M")
whenever it is more than 15 m away. Once a leg.

## Developer aids

* `TD3_WANTED=n`: n stars from the start of every leg.
* `TD3_THIEF=s`: the thief after s seconds away from the car.
* `TD3_FOES_DEBUG=1`: crimes, police cars joining and stopping, the officers every 2 s, the thief.
* `TD3_LOS_DEBUG=1`: every officer's line of sight.

Test the pursuit with the race clock running (the traffic, the police included, stands until it runs).
