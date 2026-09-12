# Underwatch

> **⚠️ AI-generated project.** This entire codebase, including this
> README, was written by an AI coding agent (Claude Code). No part of
> the code, art, audio, or documentation was hand-authored by a human.

A minimal 2D hero-shooter prototype built with SDL3. One human plays a
3v3 control-point match; the other five slots are filled by bots. Every
visual is drawn as vector shapes (lines/circles/polygons) and a built-in
blocky font — no sprites, images, or font files. All sound effects are
synthesized tones at runtime — no audio files either.

See [BUILD.md](BUILD.md) for how to build/run on Linux, Windows, and macOS.

## Controls

| Input | Action |
|---|---|
| WASD | Move up/down/left/right |
| Mouse | Aim |
| Left click | Shoot |
| R | Reload (only relevant for heroes with limited ammo) |
| Q | Use ultimate (once charge is full) |
| Right click or Shift | Use secondary ability |
| Tab | Switch which of your team's heroes you're controlling |
| Esc | Pause / resume |
| Backspace | Quit |

You always control a hero on Team A (blue). Tab cycles control among
that team's three heroes; the ones you're not controlling play
themselves as bots, same as the entire enemy team.

## The heroes

Every hero has a primary shot (left click) and one secondary ability
(right click/Shift) on a cooldown. Ultimate charge (Q) builds up from
dealing damage and healing (both at the same rate), and — for the
Tank specifically — from absorbing damage with its shield, at a
reduced rate. Charge is capped and the ultimate fires the instant you
press Q once it's full.

### Tank

- **Role**: absorb damage and force space for the team.
- **Health**: 250 (highest), **move speed**: 90 (slowest), unlimited ammo.
- **Shoot**: slow, heavy cannon shot — 50 damage, slow projectile, ~1s between shots.
- **Secondary — Shield**: raises a 500-point personal shield for 6 seconds (10s cooldown). Shield absorbs damage before health does.
- **Ultimate — Warden**: plants a stationary 2000-HP barrier at your position for 6 seconds. It blocks all enemy projectiles (including Pierce shots) and physically pushes enemies out of its radius. You can't move while it's active.

### Damage

- **Role**: deal the most damage.
- **Health**: 150, **move speed**: 125 (fastest), 20-round magazine (reload with R, or automatically once empty).
- **Shoot**: fast, low-damage projectile — 18 damage, quick fire rate (~0.28s).
- **Secondary — Sprint**: 1.8x move speed for 3 seconds (7s cooldown).
- **Ultimate — Pierce**: for 5 seconds, your shots pass straight through enemies instead of stopping on the first hit (barriers and enemy shields still stop them).

### Healer

- **Role**: keep the team alive.
- **Health**: 130, **move speed**: 110, 12-round magazine.
- **Shoot**: a single projectile that damages the first enemy it hits (12 damage) or heals the first ally it hits (12 healing) — one shot type serves both purposes depending on what it connects with.
- **Secondary — Pulse**: instantly heals every ally within 150 units for 25 (9s cooldown).
- **Ultimate — Aura of Light**: automatically re-casts Pulse (at reduced 15 healing) every 1.2 seconds for 10 seconds — no further input needed once activated.

## The arena

A rectangular arena with a capture point (a 100-unit-radius circle) in
the exact center. Each team spawns in a strip along its own side of
the map; that strip is off-limits to the enemy team and their
projectiles.

**Capturing the point:**

- A team must be the *only* team with a living player standing in the
  point's radius, continuously, for 5 seconds to gain (or steal)
  control of it. If the other team enters during that window, the
  timer resets.
- Once a team controls the point, progress fills at a fixed rate
  (full 0→100% takes 25 seconds) regardless of what happens
  afterward — an enemy stepping onto a controlled point does **not**
  stop progress, it just contests it.
- If progress hits 100% while contested, the match doesn't end yet —
  it's overtime until the point becomes uncontested.
- The moment progress is at 100% and no enemy is on the point, that
  team wins the round.

**Respawning:** dying takes you out for 5 seconds, then you respawn
at full health at your team's spawn point.

**Rounds:** a countdown (10 seconds for the very first round, 5
seconds after that) plays before each round starts. When a round
ends, the arena resets and a new round begins automatically after a
short victory pause — the game keeps running rounds back-to-back
rather than stopping.

## Bots

Bots aim at and shoot the nearest visible enemy (healers prioritize
whichever ally nearby needs healing most over attacking). They use
their secondary ability and ultimate on their own judgement — e.g. a
Tank bot shields up when an enemy closes in or its health drops low,
a Damage bot sprints in or out of a fight, a Healer bot pulses when a
nearby ally is hurt. For movement, bots contest the capture point when
their team doesn't hold it, otherwise they engage enemies (Tank
pushes in, Damage flanks) or regroup to help a teammate in trouble
(Healer trails the team and falls back toward its own spawn).
