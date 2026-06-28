# Platformer sample

Minimal 2D platformer scene that exercises the M5-M7 scripting
+ physics stack end-to-end. Run it as a smoke test for the
combined feature set, or copy + extend the script as a
starting point for a real game.

## What it shows

| Subsystem | How the sample uses it |
| --------- | ---------------------- |
| **M5** AnimatorController | Player switches between `idle` + `run` states via a `running` bool the script drives from input. |
| **M6** Lua scripting | `platformer.lua` reads input + drives velocity + handles pickup events. |
| **M7c2-c10** Physics | Player has a `CircleCollider` + `Rigidbody2D` with gravity + friction; movement uses `rigidbody.set_velocity` + `rigidbody.add_force`. |
| **M7c18** Per-tile colliders | The Tilemap points at a `.ntileset` so flagged cells become collision automatically -- no `BoxCollider` on the tilemap entity. |
| **M7c19** Trigger tiles | Coin entities use `CircleCollider isTrigger = true` so the Player passes through but fires `OnTriggerEnter` for pickup. |
| **M7c20** Collision layers | Player uses layer `Player`; Coins use layer `Item`. The Tilemap defaults to `Default`. |
| **M7c21** OneWay | A 3-cell jump-through platform sits above the ground row. |
| **M7c22-c25** Slope tiles | The plateau is built from a `SlopeRight` ramp, two `Box` tiles, and a `SlopeLeft` descent. |

## Tileset configuration

The sample reuses the existing `dungeon.ntileset` graphic. Patch
its `tiles[]` array so the four indices below carry the right
collider shapes:

| Tile index | `shape` |
| ---------- | ------- |
| 4 | `Box` (ground + plateau body) |
| 5 | `SlopeRight` |
| 6 | `SlopeLeft` |
| 7 | `OneWay` |

(Any solid-coloured tileset works; the indices are
illustrative. Edit them in the Tileset editor by clicking each
cell to cycle through the M7c22-c25 shape states.)

## Scene layout

The `Platformer` scene contains four entities:

- **Level** -- `Tilemap` (tileset above) at world `(-5, -3)`.
  Cells:
    - Ground row (`gy = 0`, `gx = 0..9`): tile 4
    - Ramp up (`gy = 1`, `gx = 3`): tile 5 (`SlopeRight`)
    - Plateau body (`gy = 1`, `gx = 4..5`): tile 4
    - Ramp down (`gy = 1`, `gx = 6`): tile 6 (`SlopeLeft`)
    - Jump-through platform (`gy = 2`, `gx = 7..9`): tile 7 (`OneWay`)
- **Player** -- `Transform (-3, 0, 0)`,  size-0.6 sprite,
  `CircleCollider r=0.3 layer=Player`,
  `Rigidbody2D gravity=(0, -9.8) friction=0.5`,
  `Animator controller=player.ncontroller`,
  `Script platformer.lua entry=Player`.
- **Coin1** -- size-0.4 yellow sprite at `(-0.5, -1)` with
  `CircleCollider r=0.2 isTrigger=true layer=Item`.
- **Coin2** -- same shape at `(3, -0.5)`.

## Controls

| Key   | Action |
| ----- | ------ |
| `A`   | Move left  |
| `D`   | Move right |
| Space | Jump (300 ms cooldown, apex-guarded) |

## Expected console output

The Console pane shows the script's lifecycle hooks firing
through the M5-M7 stack:

```
[platformer.lua] loaded
[platformer] Player ready. Use A / D to move + Space to jump.
[platformer] Landed on Level
[platformer] Jump @ t=...
[platformer] Coin Coin1 collected (1 total)
[platformer] Coin Coin2 collected (2 total)
```

`Landed on Level` reports the **tilemap entity name** because
the M7c18 per-tile collider synthesis pass routes events
through the parent tilemap, not the synthetic cell IDs.

## Where to go next

- Switch the OneWay row to `SlopeRightHigh` to build a steeper
  two-cell ramp (M7c25).
- Toggle `Tilemap.collisionLayer` and the Collision Matrix to
  let the player walk through specific tilemaps (M7c20).
- Replace the input-driven `move` variable with a state machine
  that observes `animator.current_state` (M7c9).
- See [`docs/LUA-SCRIPTING.md`](../../docs/LUA-SCRIPTING.md)
  for the full API reference.
