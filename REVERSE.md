# Reversing (fork og3)

With the automatic gearbox (skill levels 1 and 2) the car reverses as in modern driving games. Marked `OG:` in
`src/game/sim_controls.c` (`auto_reverse`, `auto_shift`).

* **Into reverse:** brake to a standstill and keep the brake held (Down, the pad's LT, the left mouse button with
  mouse steering): after 0.3 s R goes in and the brake key is the gas.
* **Reversing:** the brake key accelerates backwards. Let go, the car keeps its speed as it does forwards (the
  game's throttle stays where it was; the original lowers it only to what holds the speed).
* **Forward again:** the gas key (Up, RT) puts 1st in at once. Its drive brakes the rolling back (in R the brakes
  alone hardly hold the engine's pull), and once stopped the car drives forward.

The original reverses only through the gear keys: Z (the pad's X or LB) down past N to R, then the gas. Its
automatic also shifts R to N as soon as the gas is let go; that now waits until the car no longer rolls backwards.
The gear keys still work as before, and the manual gearbox is unchanged.
