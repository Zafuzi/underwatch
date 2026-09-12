# The Plan
Make a simple version of a multiplayer hero shooter that work in 2D and runs using SDL3. It will be a 3v3 game with bots filling empty roles.


## Controls
WASD to move UP, DOWN, LEFT, and RIGHT
Mouse to move aim cursor
Left click to shoot
Q to use ultimate
Right click or SHIFT for secondary ability

## The Heros

NOTE: ultimates charge over time slowly and by doing damage / healing. Tanks get charge by absorbing damage as well at a reduced rate

### Tank

#### Role
Absorbs damage and makes space for the team to execute enemies

#### Abilities
- Shield (500 health)
- Shoot (cannon like shooting, big damage on direct hit, but slow fire rate and slow projectile)

#### Ultimate
Warden (hunkers in place and generates a barrier with 2000 health, push enemies out of the barriers radius)


### Damage

#### Role
Contributes by dealing lots of damage

#### Abilities
- Sprint (increased move speed for a short duration, recharges over time and by doing damage)
- Shoot (fast projectiles, lower damage)

#### Ultimate
Pierce (projectiles move through enemies and barriers)


### Healer

#### Role
Heals teammates and saves them from death

#### Abilities
- Pulse (pulse of healing energy in a radius around the user, hefty cooldown but large AOE heal)
- Shoot (medium speed projectile that heals teammates and damages enemies, small damage, medium heal)

#### Ultimate
Aura of Light (spams pulse over ten seconds)


## The arena
This is a smaller area with two spawn points, one for each team. The goal is to raise a flag in the center of the arena. Progress proceeds when a team "controls" the area by being the only team members in the area for 10 seconds. Progress is saved if the enemy team takes over. Progress increases 1 tick per second the team has control up to 100%

If the point is contested (meaning one team has control and an enemy team member is within range of the flag) progress continues for the controlling team.

NOTE: respawn takes 5 seconds










