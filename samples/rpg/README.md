# Top-down RPG sample

A small top-down dungeon scene that exercises the M7c29 +
M7c30 navigation stack alongside the rest of M5-M7. Use it
as a smoke test for pathfinding + AI movement + per-tile
collider walls, or copy the scripts as a starting point for
a roguelike or RPG.

## What it shows

| Subsystem | How the sample uses it |
| --------- | ---------------------- |
| **M6** Lua scripting | `rpg_player.lua` + `rpg_enemy.lua` drive both bodies. |
| **M7c2-c10** Physics | Both bodies use `CircleCollider` + `Rigidbody2D` (gravity zeroed for top-down). |
| **M7c11** Layers | Player uses layer `Player`; Enemy uses layer `Enemy`. Walls (Tilemap) stay on `Default`. |
| **M7c18** Per-tile colliders | The dungeon's walls come from a `Tilemap` painting one solid-Box tile -- no per-wall entities. |
| **M7c19** Triggers | The Goal is a `CircleCollider isTrigger = true` entity that fires `Player_TriggerEnter`. |
| **M7c29** A* pathfinding | The enemy queries `scene.find_path(ex, ey, px, py)` every 0.5 s. |
| **M7c30** Path smoothing | The waypoints come back string-pulled, so the enemy hugs corners instead of zig-zagging through cells. |

## Tileset configuration

The sample reuses the existing `dungeon.ntileset` graphic.
Patch tile 4 (or any visually-solid tile) so it carries the
Box collider shape:

| Tile index | `shape` |
| ---------- | ------- |
| 4 | `Box` |

The Tileset editor's click cycle reaches `Box` in one click
from blank.

## Scene layout

`DungeonRPG.nscene` lives at the project root. Four entities:

- **Dungeon** -- `Tilemap` at world `(-5, -4)` with a 10x8
  cell footprint:
    - Frame walls (rows + columns at the boundary)
    - An L-shape wall at `(2, 3..5)` + `(3..5, 5)`
    - A vertical wall at `(4, 1..2)` blocking the bottom-row
      direct line
    - A horizontal wall at `(4..7, 3)`
- **Player** -- size 0.6 green sprite at `(-3.5, -2.5)`,
  `CircleCollider r=0.25 layer=Player`, `Rigidbody2D
  gravity=(0,0)`, runs `rpg_player.lua`.
- **Enemy** -- size 0.6 red sprite at `(3.5, -2.5)`,
  `CircleCollider r=0.25 layer=Enemy`, same Rigidbody2D
  shape, runs `rpg_enemy.lua`.
- **Goal** -- size 0.5 yellow sprite at `(3.5, 2.5)` with
  `CircleCollider r=0.3 isTrigger=true layer=Item`.

## Controls

| Key | Action |
| --- | ------ |
| `W` / `A` / `S` / `D` | Move the player. Diagonal input normalises so movement speed stays constant. |

## Expected behaviour

On scene start the Console shows the scripts loading:

```
[rpg_player.lua] loaded
[rpg_enemy.lua] loaded
[rpg] Player ready. WASD to move.
[rpg] Enemy patrolling.
```

The enemy walks straight toward the player by querying
`scene.find_path` every 500 ms. The pathfinder routes
around every wall in the dungeon -- the bottom-row wall at
`(4, 1..2)` forces the enemy to climb up + over to reach
the player when they spawn on opposite sides of the
corridor. With the player standing still the enemy reaches
them in ~4 s and prints:

```
[rpg] Enemy caught Player at t=4.12
```

Steer the player toward `Goal` and the trigger handler
prints:

```
[rpg] Player reached the Goal! Win.
```

## Where to go next

- Add a vision check: `scene.raycast(ex, ey, px, py)` and
  only re-path when the raycast hits the player (vs. when
  it hits a wall). The enemy stops chasing through walls
  and reacts only when in line-of-sight.
- Bump `PATH_INTERVAL` to 0.1 for jittery, twitchy AI; cut
  it to 1.0 for lazier patrols. Drives the chase feel.
- Swap the goal trigger for a sequence of waypoints +
  print which one the player crossed -- a checkpoint /
  ring-collection mode.
- See [`docs/LUA-SCRIPTING.md`](../../docs/LUA-SCRIPTING.md)
  for the full API reference including `scene.find_path`'s
  raw-vs-smoothed mode (M7c30).
