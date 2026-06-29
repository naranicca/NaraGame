# Lua Scripting (Scene Editor)

The scene editor embeds a Lua 5.4 interpreter through `ScriptHost`
(`tools/scene-editor/src/scripting/`) and lets user `.lua` files
drive entity logic the same way a Unity `MonoBehaviour` does.
Vendor Lua sources into `third_party/lua/` (see
`third_party/lua/README.md`) to enable the real interpreter -- the
editor still builds with a stub fallback when Lua isn't present so
the scripting subsystem isn't a hard build dependency.

Every script in `Assets/Scripts/*.lua` loads automatically when the
editor opens a project. Edits to those files trigger a 1 s polling
watcher (`TickAssetFileWatcher`) that re-runs the file through
`ScriptHost::LoadFile`, so external editors (VS Code, Sublime,
etc.) hot-reload without leaving the editor. The same watcher
catches `.nanim` / `.ncontroller` / `.png` / `.ntileset` /
`.autotile` changes and evicts the matching `AssetDatabase` cache.

The Console pane (`DrawConsolePanel`) shows three sections:
1. **Animation events** -- the M5 event log, fed by the runtime as
   AnimationEvents fire.
2. **Script output** -- everything `print()` produces from Lua
   land, captured through `LuaPrintRedirect` and tagged with the
   issuing scope.
3. **`>` REPL prompt** -- single-line input that runs through
   `ScriptHost::LoadString`, with Up/Down arrow history (last 64
   submissions).

## Attaching a script to an entity

Drag a `.lua` file onto a `ScriptComponent`'s Script field (or use
the inspector combo). The optional `Entry function` field changes
the routing rule: every lifecycle / event dispatch first tries
`<entryFunction>_<hook>` (`Player_Start`, `Hero_Update`,
`Captain_Damage`) and falls back to the bare global
(`OnStart`, `OnUpdate`, `OnDamage`) when the prefixed name doesn't
exist. An empty entry function skips straight to the bare global.

The pattern lets a single `.lua` file host multiple "components":
declare `Player_Start`, `Enemy_Start`, `Pickup_Start` in one
file + point each entity's `entryFunction` at the matching prefix.

## Lifecycle hooks

Every hook receives the entity name as its first argument. The
sequence matches Unity's MonoBehaviour:

| Hook                                                | When                                                          |
| --------------------------------------------------- | ------------------------------------------------------------- |
| `OnEnable(entity)`                                  | `Entity.enabled` flips false -> true (incl. on scene load)    |
| `OnStart(entity)`                                   | First tick the entity is enabled (one-shot per session)       |
| `OnUpdate(entity, dt)`                              | Every editor frame while the play toggle is on + enabled      |
| `OnDisable(entity)`                                 | `Entity.enabled` flips true -> false (incl. destroy teardown) |
| `OnDestroy(entity)`                                 | Entity is being destroyed (after OnDisable)                   |
| `OnAnimationEvent(entity, anim, event, s, f, i, b, vx, vy, r, g, bc, a)` | An animation event crosses the play cursor                    |
| `OnTriggerEnter(entity, other)`                     | A trigger collider just started overlapping `other`           |
| `OnTriggerStay(entity, other)`                      | A trigger collider continues to overlap `other` (every frame) |
| `OnTriggerExit(entity, other)`                      | A trigger collider stopped overlapping `other`                |
| `OnCollisionEnter(entity, other)`                   | A solid collider just contacted `other` (push-out applied)    |
| `OnCollisionStay(entity, other)`                    | A solid collider continues to contact `other` (every frame)   |
| `OnCollisionExit(entity, other)`                    | A solid collider just separated from `other`                  |

`OnAnimationEvent` carries the M5c19 payload: a string, a float,
an int, a bool, a `(vec2.x, vec2.y)` pair, and a
`(color.r, color.g, color.b, color.a)` quad.

`OnTrigger*` fires when **either** collider in the pair is
flagged `isTrigger`; `OnCollision*` fires when **both** are
solid (`isTrigger=false`). Each side of the pair receives its
own dispatch with the *other* entity name as the second
argument.

## API surface

The host registers eleven global tables: `animator`,
`transform`, `sprite`, `rigidbody`, `input`, `time`, `audio`,
`particle`, `camera`, `scene`, `math` (extension methods on
the stdlib `math` table). Every API entry takes the target
entity's name as its first argument (when applicable). Missing
entities and missing components are silent no-ops that return
a sensible default rather than raising errors.

### `animator.*`

Reads and writes `AnimatorComponent` parameters.

| Function | Returns | Notes |
| -------- | ------- | ----- |
| `animator.set_bool(entity, name, value)`    | -    | `paramBools[name] = value` |
| `animator.get_bool(entity, name)`           | bool | false default              |
| `animator.set_float(entity, name, value)`   | -    | `paramFloats[name]`        |
| `animator.get_float(entity, name)`          | num  | 0.0 default                |
| `animator.set_int(entity, name, value)`     | -    |                            |
| `animator.get_int(entity, name)`            | int  | 0 default                  |
| `animator.set_trigger(entity, name)`        | -    | Pulses bool -> runtime auto-resets after consume |
| `animator.set_vec2(entity, name, x, y)`     | -    |                            |
| `animator.get_vec2(entity, name)`           | x, y | (0, 0) default             |
| `animator.set_color(entity, name, r, g, b, a)` | - |                            |
| `animator.get_color(entity, name)`          | r, g, b, a | (0, 0, 0, 0) default |
| `animator.set_string(entity, name, value)`  | -    |                            |
| `animator.get_string(entity, name)`         | str  | "" default                 |
| `animator.current_state(entity)`            | str  | Current AnimatorController state name; "" when no controller / unknown |
| `animator.in_state(entity, name)`           | bool | Shorthand `current_state(...) == name` |

### `transform.*`

Reads and writes `TransformComponent`. Rotation is the same
z-axis-degrees float `TransformComponent::rotation` already uses.

| Function | Returns |
| -------- | ------- |
| `transform.set_position(entity, x, y, z)` | -        |
| `transform.get_position(entity)`          | x, y, z  |
| `transform.set_rotation(entity, deg)`     | -        |
| `transform.get_rotation(entity)`          | deg      |
| `transform.set_scale(entity, x, y, z)`    | -        |
| `transform.get_scale(entity)`             | x, y, z  |
| `transform.translate(entity, dx, dy, dz)` | -        |

### `rigidbody.*`

Reads and writes `Rigidbody2DComponent`. The host's
`TickRigidbodies` integrates velocity with semi-implicit Euler
(`vel += gravity * dt; vel *= max(0, 1 - drag * dt);
pos += vel * dt`); collisions then reflect / damp velocity
based on `restitution` / `friction`. Entities without a
Rigidbody2D are treated as infinite-mass static colliders --
they don't move and don't carry impulse.

| Function | Returns |
| -------- | ------- |
| `rigidbody.set_velocity(entity, x, y)`  | -    |
| `rigidbody.get_velocity(entity)`        | x, y |
| `rigidbody.add_force(entity, fx, fy)`   | -    (impulse: `vel += force`) |
| `rigidbody.set_gravity(entity, x, y)`   | -    |
| `rigidbody.get_gravity(entity)`         | x, y |
| `rigidbody.set_drag(entity, d)`         | -    |
| `rigidbody.get_drag(entity)`            | num  |

Inspector-only fields (no Lua getters/setters yet): `mass`
(splits MTV inversely by mass; 0 invalid, falls back to 1),
`restitution` (0..1; 0 absorbs, 1 fully reflects), `friction`
(0..1; per-contact tangential damping).

### `sprite.*`

Reads and writes `SpriteRendererComponent`. Sprite GUID swaps,
`drawMode`, and `sortingLayer` stay editor-side -- the inspector
covers those and runtime scripts rarely need to touch either.

| Function | Returns |
| -------- | ------- |
| `sprite.set_color(entity, r, g, b, a)` | -       |
| `sprite.get_color(entity)`             | r, g, b, a |
| `sprite.set_alpha(entity, a)`          | -       |
| `sprite.get_alpha(entity)`             | num     |
| `sprite.set_sub_sprite(entity, name)`  | -       |
| `sprite.get_sub_sprite(entity)`        | str     |
| `sprite.set_flip_x(entity, bool)`      | -       |
| `sprite.get_flip_x(entity)`            | bool    |
| `sprite.set_flip_y(entity, bool)`      | -       |
| `sprite.get_flip_y(entity)`            | bool    |
| `sprite.set_size(entity, x, y)`        | -       |
| `sprite.get_size(entity)`              | x, y    |
| `sprite.set_order(entity, n)`          | -       |
| `sprite.get_order(entity)`             | int     |

### `input.*`

Routes through ImGui's already-collected input. Key names are
either single uppercase characters (`"A"`-`"Z"`, `"0"`-`"9"`) or
one of the named keys: `Space`, `Enter`/`Return`, `Escape`/`Esc`,
`Tab`, `Backspace`, `Delete`, `Home`, `End`, `PageUp`,
`PageDown`, `Up`/`UpArrow`, `Down`, `Left`, `Right`,
`LeftShift`/`RightShift`, `LeftCtrl`/`RightCtrl`,
`LeftAlt`/`RightAlt`. Mouse buttons are 0 = left, 1 = right,
2 = middle.

| Function | Returns |
| -------- | ------- |
| `input.is_key_down(name)`        | bool |
| `input.is_key_pressed(name, repeat?)` | bool (this frame) |
| `input.is_key_released(name)`    | bool |
| `input.mouse_position()`         | x, y (screen px) |
| `input.is_mouse_down(button)`    | bool |
| `input.is_mouse_pressed(button)` | bool |
| `input.is_mouse_released(button)`| bool |
| `input.mouse_wheel()`            | num  |
| `input.bind(action, key)`        | -- (rebinds the action to the named key; replaces any prior binding) |
| `input.unbind(action)`           | bool (true when the action existed) |
| `input.binding(action)`          | str (current key, or `""` when unbound) |
| `input.is_action_down(action)`   | bool |
| `input.is_action_pressed(action, repeat?)` | bool |
| `input.is_action_released(action)` | bool |

M9c18 action bindings: pair a script-side name (`"jump"`, `"fire"`, `"pause"`) with a key string. `input.is_action_*` then dispatches through the binding so a user-facing rebind UI can swap controls without touching gameplay code. Persist a binding set with `savegame.save` -- walk `input.binding(name)` for each action you care about, stash the strings in `state.input_bindings`, and replay them with `input.bind` on the next boot.

### Splash screens

No dedicated component; use the M9c15 scene swap. Author a `Scenes/Splash.nscene` whose root entity carries a SpriteRenderer (full-screen logo) plus a Script:

```lua
function Splash_Update(entity, dt)
    if time.elapsed() > 2.0 then
        scene.load("Scenes/MainMenu.nscene")
    end
end
```

Set `mainScene = "Scenes/Splash.nscene"` (File > Set Current Scene as Main) and a packed `game.exe` boots into the splash automatically + transitions to the menu after two seconds.

### Settings panels

Same pattern -- a `Scenes/Settings.nscene` with sprite + script entities, an `input.bind`-driven rebind row per action, and `state.audio_master` / `audio.set_bus_volume` for volume sliders. Persist with `savegame.save("settings")` so the panel reapplies on the next boot.

### `time.*`

| Function | Returns |
| -------- | ------- |
| `time.delta()`       | This frame's dt (ImGui::GetIO().DeltaTime) |
| `time.elapsed()`     | Running double, bumped by the host each frame |
| `time.frame_count()` | Running long long |
| `time.fps()`         | ImGui::GetIO().Framerate (smoothed) |

### `audio.*`

XAudio2-backed playback (M8). Three playback flavours -- one-
shot SFX (fire-and-forget), pooled SFX with handles (per-voice
control), and streaming music (decoded chunk by chunk so multi-
minute clips don't blow up memory). The engine boots lazily on
the first call so projects that don't use audio pay no cost.

**Path resolution** is consistent across every binding that
takes a sound: a 36-char dashed GUID is looked up in the asset
DB (must point at an `Audio` asset); an absolute path is used
verbatim; everything else is joined to the project's
`Assets/` folder. So `audio.play("Sounds/coin.wav")` and
`audio.play("a1b2c3d4-...")` both work and survive a file
rename.

**Bus structure** (M8c9 + M8c13):

```
                       Master
                         ^
            +-----+------+------+
            |     |             |
          Music  SFX  ------>  Reverb (wet send)
            ^     ^             ^
            |     |             |
        play_music play / spawn / play_positional
```

Master / SFX / Music are per-bus volumes (multiplicative on
top of per-voice volume); Reverb is a wet send level on the
SFX submix that feeds an I3DL2 Living-Room preset. All four
are edited through **Window > Audio Mixer...** (M8c12) and
persist in `ProjectSettings/project.json` under `audioMixer`.

#### One-shot SFX

| Function | Returns |
| -------- | ------- |
| `audio.play(path_or_guid, volume?)`                          | bool (volume default 1.0) |
| `audio.stop()`                                                | -- (silences every active SFX voice) |
| `audio.play_positional(path_or_guid, vol, pan, freq_ratio?)` | bool. `pan` in [-1, 1] (mono sources only; stereo skips the matrix). `freq_ratio` default 1.0; M8c13 doppler -- clamped to [0.25, 2.0] |

#### Pooled voices (per-voice control, M8c11)

A 64-slot voice pool plus a generation counter on each slot --
recycled slots get a fresh generation so a stale handle silently
rejects instead of controlling the wrong voice. Handle `0` is
reserved for failure (full pool, missing file, etc.) and maps to
`nil` in Lua.

| Function | Returns |
| -------- | ------- |
| `audio.spawn(path_or_guid, volume?)`        | int handle (truthy) on success; `nil` on failure |
| `audio.stop_voice(h)`                       | bool |
| `audio.pause_voice(h)`                      | bool |
| `audio.resume_voice(h)`                     | bool |
| `audio.set_voice_volume(h, v)`              | bool |
| `audio.is_voice_playing(h)`                 | bool |

#### Streaming music (M8c5 / M8c10)

WAV / OGG / MP3 streamed through a 3-buffer ring (~93 ms each
at 44.1 kHz). One slot only -- a second `play_music` cleanly
stops the previous one. Pre-fill at start time gives ~280 ms
of slack before `UpdateMusic` per-frame matters.

| Function | Returns |
| -------- | ------- |
| `audio.play_music(path_or_guid, volume?, loop?)` | bool |
| `audio.stop_music()`                              | -- |

#### Mixer + reverb (M8c12 / M8c13)

| Function | Returns |
| -------- | ------- |
| `audio.set_bus_volume(name, v)` | bool. `name` is `"master"`, `"sfx"`, or `"music"`. Unknown names return `false` so callers can probe support. |
| `audio.get_bus_volume(name)`    | num (or `nil` on unknown name) |
| `audio.set_reverb_wet(wet)`     | -- (clamped to [0, 1]; live-applied via SetOutputMatrix) |
| `audio.get_reverb_wet()`        | num |

The mixer values that Lua writes are **live engine state**,
not project state -- they survive until the next
`ApplyProjectAudioMixer()` (project re-open / new project)
pushes the persisted JSON values back in. Wire a settings UI
through the modal if you want persistence; use `audio.*` from
script land for transient ducking / fades.

### `particle.*`

Controls a `ParticleSystemComponent` (M9c2) at runtime. The
component's inspector fields (emission rate, lifetime range,
colour ramp, gravity, ...) edit the steady-state behaviour;
this table is the script-side hook for explicit bursts, runtime
rate changes, and play/stop toggles.

| Function | Returns | Notes |
| -------- | ------- | ----- |
| `particle.emit(entity, count?)`             | int (actually emitted) | Adds up to `count` particles (default 1) to the live pool *now*, sampled from the same min/max ranges the auto-emit path uses. Cap is the component's `maxParticles`; over-cap requests return what fit. |
| `particle.clear(entity)`                    | bool | Wipes the live pool without touching emission settings. |
| `particle.set_emission_rate(entity, rate)`  | bool | Live-tune the per-second auto-emission rate. 0 disables continuous emission while leaving `particle.emit` bursts untouched. Negative values clamp to 0. |
| `particle.get_count(entity)`                | int  | Current live particle count -- useful for pacing the next burst or detecting "finished". |
| `particle.set_playing(entity, bool)`        | bool | Toggles the auto-emit + integrate path. `false` freezes new emissions; `true` resumes from where the emitter left off without re-firing `playOnStart`. `particle.clear` is the "wipe to zero" sibling. |
| `particle.is_playing(entity)`               | bool | True when the emitter's auto-play gate is still on. |

Script-driven bursts (explosion on hit, sparkle on pickup):
emit on demand with `particle.emit` and leave `emissionRate =
0` in the inspector so the component only fires when Lua asks.
Steady-state ambience (smoke, dust, fountains): use a non-zero
`emissionRate` and let `TickParticles` handle it; reach for
`set_emission_rate` only when a runtime event needs to thin
or thicken the stream.

### `camera.*`

Drives a `CameraComponent` (M9c11) at runtime. The standalone
runtime resolves the highest-priority enabled `CameraComponent`
every frame + sets the viewport's centre to that entity's
Transform position + the viewport zoom to the component's
`zoom`. Editor mode keeps user pan/zoom; use the inspector's
"Preview in Viewport" button to peek what a camera would frame.

| Function | Returns | Notes |
| -------- | ------- | ----- |
| `camera.set_zoom(entity, zoom)`        | bool | Pixels per world unit. Clamped to >= 0.01. |
| `camera.get_zoom(entity)`              | num or `nil` | nil when the entity has no CameraComponent. |
| `camera.set_priority(entity, p)`       | bool | Higher wins; ties break in scene-traversal order. |
| `camera.get_priority(entity)`          | int or `nil` |
| `camera.get_active()`                  | str | Name of the highest-priority enabled CameraComponent entity. `""` when none. |

Typical patterns:

- **Player follow**: a camera entity with a `Script` that
  lerps its Transform toward the player each `OnUpdate`. The
  standalone viewport picks up the lerped position on the
  same frame.
- **Cutscene override**: a parked camera entity at
  `priority = 10`. Enable it when the cutscene starts;
  disable it when done -- the player-follow camera at
  `priority = 0` automatically reclaims the viewport.
- **Zoom punch**: `camera.set_zoom("MainCam", 96)` for a hit,
  lerp back to 64 over half a second on `OnUpdate`.
- **Screen shake**: drive the camera entity's Transform with
  a damped random offset in `OnUpdate`; the viewport tracks
  every nudge.

### `scene.*`

Twenty-four entries spanning entity / component / hierarchy /
picking / broadcast / physics queries / pathfinding. Every
callback resolves the entity by name through the bound `Scene`.

| Function | Returns |
| -------- | ------- |
| `scene.create_entity(name?, parent_name?)` | str (assigned name) |
| `scene.destroy_entity(name)`               | bool |
| `scene.destroy_self()`                     | bool (no-op outside a dispatch) |
| `scene.find(name)`                         | bool |
| `scene.entity_count()`                     | int |
| `scene.all_entities()`                     | table (1-indexed strings) |
| `scene.entity_at(x, y)`                    | str ("" on miss; topmost orderInLayer wins) |
| `scene.find_by_component(type_name)`       | table (1-indexed strings) |
| `scene.add_component(entity, type)`        | bool |
| `scene.has_component(entity, type)`        | bool |
| `scene.remove_component(entity, type)`     | bool |
| `scene.set_enabled(entity, bool)`          | bool |
| `scene.is_enabled(entity)`                 | bool |
| `scene.parent_of(entity)`                  | str ("" for root / missing) |
| `scene.children_of(entity)`                | table (1-indexed strings) |
| `scene.set_parent(entity, parent?)`        | bool (empty parent = detach to root; missing parent name = false) |
| `scene.broadcast(method, ...)`             | int (count of successful dispatches) |
| `scene.raycast(x0, y0, x1, y1)`            | str, hx, hy, dist  ("" on miss; nearest collider wins along the segment) |
| `scene.overlap_box(cx, cy, w, h)`          | table (1-indexed strings) of every collider intersecting a virtual AABB |
| `scene.overlap_circle(cx, cy, r)`          | table (1-indexed strings) of every collider intersecting a virtual circle |
| `scene.find_path(sx, sy, ex, ey, smooth?)` | table of `{x=..., y=...}` waypoints (1-indexed); `nil` when no walkable Tilemap exists or no path was found. `smooth` defaults to `true` (M7c30 line-of-sight string-pulling); pass `false` to inspect the raw A* cell path. |

Component type names match the C++ `Component::TypeName()`
strings: `"Transform"`, `"SpriteRenderer"`, `"Tilemap"`,
`"Animator"`, `"Script"`, `"BoxCollider"`, `"CircleCollider"`,
`"CapsuleCollider"`, `"PolygonCollider"`, `"EdgeCollider"`,
`"Rigidbody2D"`, `"AudioSource"`, `"AudioListener"`,
`"ParticleSystem"`, `"Camera"`.

### Physics components

Six collider components + Rigidbody2D + Tilemap plug into the
same overlap / event plumbing. Layer filtering, push-out, and
velocity reflection all happen in `TickColliders` before any
Lua dispatch fires. Every shape interacts with every other
through one common ColliderInfo + SAT path -- there's no
N x M dispatch matrix to keep in sync as new shapes ship.

| Component | Key fields | Notes |
| --------- | ---------- | ----- |
| `BoxCollider` | `size`, `offset`, `isTrigger`, `layer` | Oriented bounding box -- inherits the owning Transform's rotation (M7c13) |
| `CircleCollider` | `radius`, `offset`, `isTrigger`, `layer` | Centre + radius; rotation-invariant |
| `CapsuleCollider` | `radius`, `halfLength`, `offset`, `isTrigger`, `layer` | Vertical spine + 2 hemispherical caps (M7c27). Local height = 2 * (halfLength + radius); width = 2 * radius. Decomposed into a core OBB + 2 endcap circles for collision; rotates with Transform |
| `PolygonCollider` | `vertices` (CCW Vec2 list), `offset`, `isTrigger`, `layer` | Convex polygon in local space (M7c28). Default = regular pentagon, radius 0.4. Runs through the same n-gon SAT used by slope tiles -- no upper vertex count beyond what the inspector can edit |
| `EdgeCollider` | `vertices`, `closed`, `offset`, `isTrigger`, `layer` | Polyline / chain collider (M7c32). Each adjacent pair contributes one segment; `closed = true` adds the wrap-around. Implemented as per-segment thin-OBB decomposition (0.02 unit thickness); a ~r residual penetration at the deepest contact is the known cost, swap-out for closed-form segment-vs-shape distance is future-work |
| `Rigidbody2D` | `velocity`, `gravity`, `drag`, `mass`, `restitution`, `friction` | See `rigidbody.*` |
| `Tilemap` | `tileset`, `cellPixelW/H`, `cells`, `sortingLayer`, `orderInLayer`, `collisionLayer` | Per-tile collider synthesis when `tileset` points at a `.ntileset` asset; layer + cell shapes drive collision (M7c18 / c19 / c20 / c21 / c22 / c23 / c25) |

Box pairs use SAT (Separating Axis Theorem) so the collider
follows whatever rotation `Transform.rotation` carries -- a
sprite rotated 45 degrees gets a diamond hitbox, not its
axis-aligned bounding box. Box-vs-circle routes the circle's
centre into the box's local frame so the same nearest-point
clamp the M7c1 path used still applies even when the box is
rotated. The runtime fires `OnTrigger*` when **either** side
is flagged trigger and `OnCollision*` when both are solid;
the solid path also push-outs along the MTV + reflects
velocity by the combined restitution.

Physics runs at 4x the render rate: `TickPhysics` (M7c14)
splits the frame's dt into four sub-steps and runs the
integrator + collision pass on each. Lua scripts still get
one `OnUpdate(dt)` per render frame, but `OnTriggerStay` /
`OnCollisionStay` now fire **four times** per frame -- a
damage-tick counter that accumulates in `Stay` will see 4x
the ticks per render frame compared to a Update-driven
counter. Stay handlers that need once-per-frame semantics
should drive from a frame counter in `OnUpdate` instead.

#### Per-tile colliders (`.ntileset` assets)

A `Tilemap` whose `tileset` GUID points at a `.ntileset`
asset gets per-cell collision synthesised automatically --
no `BoxCollider` / `CircleCollider` component required on
the tilemap entity. The runtime walks every painted cell,
looks the tile index up in the tileset's `tiles[]` map,
and generates one synthetic collider per flagged cell. The
synthetic id encodes `(tilemap.id, gx, gy)` so the same
cell hashes to the same pair across frames, and
`OnTriggerEnter` / `OnCollisionEnter` events report the
**tilemap entity's name** as `other` -- not a synthetic
cell name.

Each `TilesetTile` carries three flags that compose to
seven distinct collider behaviours:

| `collides` | `isTrigger` | `shape` | Behaviour |
| ---------- | ----------- | ------- | --------- |
| false | false | -- | Decorative; no collider |
| true | false | `Box` | Solid box; pushes bodies out (M7c18) |
| true | true | `Box` | Trigger; fires `OnTrigger*` only (M7c19) |
| true | false | `OneWay` | Jump-through platform; blocks only when MTV.y > 0 (M7c21) |
| true | false | `SlopeRight` / `SlopeLeft` | 45-degree triangular slope; Ball rolls down + Box rests on the diagonal face (M7c22) |
| true | false | `SlopeRightHalf` / `SlopeLeftHalf` | 22.5-degree gentle slope, half-height triangle (M7c23) |
| true | false | `SlopeRightHigh` / `SlopeLeftHigh` | 22.5-degree gentle slope, upper-half trapezoid -- pairs with the matching Half tile to build a two-cell ramp (M7c25) |

Tileset editor: open a `.ntileset` from the Project pane.
Click a tile to cycle through the 10 states (blank ->
Box -> OneWay -> the four slope variants -> Trigger ->
blank). The overlay paints solid Box red, OneWay orange,
trigger blue, and slope tiles as the filled triangle or
trapezoid that matches the runtime's collision shape.

Slope collision math:
- **Circle vs slope**: closest-point-on-polygon clamp (M7c25
  for trapezoid; M7c22 segment clamp for triangles). The
  push direction is the polygon's outward edge normal on
  interior contacts and the radial direction from the
  nearest vertex on corner contacts.
- **Box vs slope**: 5- or 6-axis SAT against the slope's
  filled triangle or trapezoid (M7c24 + M7c25). Box bodies
  rest on the diagonal face the same way a Circle does,
  with the box's nearest corner pinned to the slope.

The synthesised colliders honour the tilemap's
`collisionLayer` (M7c20), so a `Tilemap.collisionLayer =
"Enemy"` will not collide with `BoxCollider.layer =
"Player"` when the matrix turns off that pair. Per-tile
colliders also inherit the tilemap's `Transform.rotation`
(M7c17), so a rotated tilemap rotates every cell collider
in lockstep -- the SAT continues to use the actual rotated
faces.

#### Collision layers + matrix

Each collider tags itself with a project-level layer (default
`"Default"` plus `"Player"` / `"Enemy"` / `"Item"` out of the
box). The **Window > Collision Matrix...** modal renders an
NxN checkbox grid -- un-ticking a row/column pair tells the
runtime to silently skip that layer combination. Filtered
pairs cost no per-shape math and never fire events.

### Audio components (M8c7 / M8c8)

| Component | Key fields | Notes |
| --------- | ---------- | ----- |
| `AudioSource` | `clip` (Audio GUID), `volume`, `playOnStart`, `loop`, `is3D`, `minDistance`, `maxDistance` | Auto-plays its clip on rising-edge play-mode entry. `loop = true` routes through `play_music` (streaming + loop); `loop = false` uses the SFX cache. `is3D` toggles distance attenuation + stereo pan against the active AudioListener (M8c8). `is3D + loop` is silently ignored -- streaming voices don't update as the entity moves yet |
| `AudioListener` | -- (marker component) | Attach to the player / camera / follow-target. The runtime takes the listener's Transform position as the reference point for 3D source attenuation + stereo pan. First enabled entity carrying the component wins -- only one active per scene |

### Camera component (M9c11)

| Component | Key fields | Notes |
| --------- | ---------- | ----- |
| `Camera` | `zoom`, `priority` | Game-runtime viewpoint. The standalone runtime resolves the highest-priority enabled `Camera` every frame + locks the viewport centre to that entity's Transform position + zoom to `zoom` (pixels per world unit). Editor mode keeps user pan/zoom; an inspector "Preview in Viewport" button snaps the editor camera to match. The editor viewport overlays a yellow CAM-labelled frustum rectangle showing what each camera would render at a 16:9 window; suppressed in standalone. See `camera.*` for script hooks |

### VFX components (M9c2)

| Component | Key fields | Notes |
| --------- | ---------- | ----- |
| `ParticleSystem` | `maxParticles`, `emissionRate`, `lifetimeMin/Max`, `startSpeedMin/Max`, `startSizeMin/Max`, `endSizeMin/Max` (M9c8), `dirAngleMinDeg/MaxDeg`, `startColor`, `endColor`, `gravity`, `texture` (M9c4), `subSprite` (M9c5), `animFrames`/`animFps`/`animLoop` (M9c6), `startRotationMinDeg/MaxDeg` + `angularSpeedMinDeg/MaxDeg` + `velocityAlign` (M9c7), `duration` + `bursts[{time, count}]` (M9c9), `shape` + `shapeRadius`/`shapeBoxSize`/`shapeLineEnd` (M9c10), `playOnStart`, `loop` | Entity-attached 2D emitter. Ticks under the same Play-mode gate as audio + animators; renders coloured quads in `DrawEntities` after sprite/tilemap. An invalid `texture` GUID (the default) keeps the M9c2 untextured `AddRectFilled` path -- sharp ember / spark look. A valid Texture asset routes through `AddImageQuad` with the whole texture mapped onto each particle quad, tinted by the lerped colour, so an alpha-channel PNG of a smoke puff + `endColor.a = 0` gives a soft particle. `subSprite` (M9c5) picks a single sheet cell out of the texture's `spriteSheet.sprites`. `animFrames` (M9c6) is a list of sub-sprite names the renderer cycles through per-particle at `animFps` -- each particle's `age` indexes its frame independently, so two particles spawned a fraction of a second apart cycle out of phase. `animLoop=true` wraps via modulo (sparkle / fire loops); `false` clamps at the last frame (one-shot explosion sequences). M9c7 rotation: each particle samples an initial rotation from `[startRotationMin, startRotationMax]` (degrees, CCW) + an angular speed from `[angularSpeedMin, angularSpeedMax]` (degrees/sec) on emit; the renderer projects the 4 quad corners through cos/sin every frame. `velocityAlign = true` overrides both -- the quad rotates to face the particle's current velocity direction (good for sparks / debris streaks). M9c8 size-over-lifetime: when `endSizeMin/Max >= 0` the renderer lerps each particle's draw size from its sampled `startSize` to its sampled `endSize` across age/lifetime -- shrink to nothing (`end = 0`) for fade-outs, grow for puffballs / explosions. Sentinel `-1` (the default) means "no lerp -- stay at start size" so legacy scenes load unchanged. M9c9 burst events: `bursts` is a list of `{time, count}` entries that fire `count` particles when the emitter's elapsed time first crosses each `time` (seconds since the cycle started). `duration` sets the cycle length; with `loop = true` the emitter wraps `elapsed - duration` + resets the next-burst cursor every `duration` so bursts re-fire. Bursts compose on top of the continuous `emissionRate` -- both routes share the same per-emit sampling helper + cap against `maxParticles`. Set `emissionRate = 0` for a pure event-driven emitter (cannon shots, footstep dust, periodic sparkle). The inspector list has a Sort button that orders entries by time -- fire order = list order, so sort for predictability. M9c10 emission shape: `shape` picks `Point` (the default, every particle spawns at the Transform position), `CircleEdge` / `CircleVolume` (sampled on the boundary / uniform inside, controlled by `shapeRadius`), `BoxEdge` / `BoxVolume` (rectangle boundary / interior, controlled by `shapeBoxSize`), or `Line` (uniform from origin to `shapeLineEnd`). Velocity is still chosen from `dirAngleMin/Max` -- shape only sets where the particle starts. A cyan editor-only gizmo traces the shape outline in the viewport (suppressed in standalone) so the user can size it by eye. UV lookups cache per emitter per frame, so a 200-particle / 8-frame sheet costs 8 cell walks + 200 indexings per draw. See `particle.*` for script hooks |

For script-driven SFX (footsteps, gunfire, jump SFX) use
`audio.play` / `audio.spawn` from Lua directly. `AudioSource`
is the declarative auto-play path -- ambient sound, BGM that
follows a scene, looping environment loops, one-shot SFX on
spawn. The two paths share the same SFX cache and submix bus.

### `math.*` (extensions)

The stdlib `math` table gains pure-Lua game-dev helpers.
Existing entries (`math.floor`, `math.sin`, `math.deg`,
`math.rad`, etc.) stay untouched. User scripts can override any
of these by reassigning back into `math.*`.

| Function | Returns |
| -------- | ------- |
| `math.lerp(a, b, t)`                  | num |
| `math.clamp(x, lo, hi)`                | num |
| `math.sign(x)`                         | -1, 0, or 1 |
| `math.smoothstep(e0, e1, x)`           | num (clamped 0..1) |
| `math.distance(x1, y1, x2, y2)`        | num |
| `math.length(x, y)`                    | num |
| `math.normalize(x, y)`                 | nx, ny ((0, 0) for zero-length) |
| `math.dot(x1, y1, x2, y2)`             | num |

## Patterns

### Per-entity entry function

```lua
-- player.lua
function Player_Start(entity)
    animator.set_string(entity, "weapon", "sword")
end

function Player_Update(entity, dt)
    if input.is_key_pressed("Space") then
        animator.set_trigger(entity, "jump")
    end
end

function Player_Damage(entity, hp, source)
    animator.set_int(entity, "hp",
        animator.get_int(entity, "hp") - hp)
end
```

Attach the script to an entity + set `entryFunction = "Player"` in
the inspector. The handlers route through the M6c5 routing rule;
no other entity in the project running this script is affected.

### Broadcast pattern

```lua
-- captain.lua: shouts at the world
function Captain_Start(entity)
    scene.broadcast("RallyUp", 50)
end
```

Every entity with a `ScriptComponent` receives the message:

```lua
-- soldier.lua
function Soldier_RallyUp(entity, morale_boost)
    animator.set_int(entity, "morale",
        animator.get_int(entity, "morale") + morale_boost)
end
```

### Click-to-select

```lua
function Picker_Update(entity, dt)
    if not input.is_mouse_pressed(0) then return end
    local mx, my = input.mouse_position()
    -- mx, my are screen pixels; convert via your viewport's
    -- own transform if needed. Once you're in world space:
    local hit = scene.entity_at(world_x, world_y)
    if hit ~= "" then
        sprite.set_color(hit, 1, 1, 0, 1)  -- highlight yellow
    end
end
```

### Self destruction

```lua
function Bullet_Update(entity, dt)
    transform.translate(entity, 5.0 * dt, 0, 0)
    if time.elapsed() > spawned[entity] + 2.0 then
        scene.destroy_self()
    end
end
```

`scene.destroy_self()` fires `OnDisable` + `OnDestroy` on the
current entity (and recursively on every child) the same way
the editor's UI delete does.

### Dynamic spawning + composition

```lua
function Spawner_Update(entity, dt)
    if input.is_key_pressed("S") then
        local bullet = scene.create_entity("Bullet", entity)
        scene.add_component(bullet, "SpriteRenderer")
        transform.set_position(bullet, 0, 0, 0)
        spawned[bullet] = time.elapsed()
    end
end
```

`scene.create_entity` auto-adds a `TransformComponent`, so the
spawned entity is immediately ready for `transform.*` calls.

### Collision-driven AnimatorController transitions

```lua
function Runner_Start(entity)
    animator.set_bool(entity, "running", true)
end

function Runner_CollisionEnter(entity, other)
    -- Hit something solid: pop back to idle.
    animator.set_bool(entity, "running", false)
end

function Runner_Update(entity, dt)
    -- React to the state machine rather than driving it blind.
    if animator.in_state(entity, "idle") then
        sprite.set_color(entity, 0.6, 0.6, 0.6, 1)
    else
        sprite.set_color(entity, 1, 1, 1, 1)
    end
end
```

`animator.current_state` walks the controller asset to resolve
the current `state.name`. Useful for "I just entered Hit"
branches or "tint sprite while in Stunned" loops without
keeping a parallel state machine in Lua.

### Gravity + bounce + ground contact

```lua
-- Ball has Rigidbody2D{gravity=(0, -9.8), restitution=0.8} +
-- a solid CircleCollider. Floor is a static solid BoxCollider.
function Ball_CollisionEnter(entity, other)
    if other == "Floor" then
        local _, vy = rigidbody.get_velocity(entity)
        print(("Bounce vy=%.2f"):format(vy))   -- already reflected
    end
end

-- Apply an upward "jump" impulse on Space.
function Ball_Update(entity, dt)
    if input.is_key_pressed("Space") then
        rigidbody.add_force(entity, 0, 6)
    end
end
```

The runtime applies the bounce + push-out **before** firing
`OnCollisionEnter`, so `rigidbody.get_velocity` inside the
handler reads the post-bounce value. `add_force` is treated as
an instantaneous impulse (the integrator already handles
gravity each tick).

### Line-of-sight + AoE queries

```lua
function Enemy_Update(entity, dt)
    -- Can I see the Player from here?
    local ex, ey = transform.get_position(entity)
    local px, py = transform.get_position("Player")
    local who, hx, hy, dist = scene.raycast(ex, ey, px, py)
    if who == "Player" then
        animator.set_trigger(entity, "Attack")
    end
end

-- Explosion damages everything in a radius around the bomb.
function Bomb_OnAnimationEvent(entity, _, evt)
    if evt.name == "explode" then
        local bx, by = transform.get_position(entity)
        local hits = scene.overlap_circle(bx, by, 3)
        for _, target in ipairs(hits) do
            scene.broadcast("Damage", 25, "fire")
        end
    end
end
```

`scene.raycast` returns the nearest collider hit along the
segment; `scene.overlap_box` / `scene.overlap_circle` return
every collider that intersects a virtual shape, without
needing a permanent sensor entity in the scene. All four
spatial queries -- raycast, overlap_box, overlap_circle, and
`entity_at` -- honour `Transform.rotation` on Box colliders
(M7c15). A ray fired at a rotated Box collider hits the
actual rotated face, not the AABB envelope; `entity_at`
picks a point that's inside the rotated outline even when
it sits outside the AABB. The price for that precision is
roughly one extra dot-product per box-pair / box-point
test -- still O(1) per candidate.

### Tilemap pathfinding (M7c29 / M7c30)

```lua
-- Enemy walks along an A*-smoothed path to the player.
function Enemy_Update(entity, dt)
    local ex, ey = transform.get_position(entity)
    local px, py = transform.get_position("Player")
    local path = scene.find_path(ex, ey, px, py)  -- smoothed
    if not path or #path < 2 then return end
    -- Step toward the next waypoint (skip path[1] = our cell).
    local next = path[2]
    local dx = next.x - ex
    local dy = next.y - ey
    local len = math.sqrt(dx * dx + dy * dy)
    if len > 1e-3 then
        local speed = 2.0
        transform.translate(entity,
            (dx / len) * speed * dt,
            (dy / len) * speed * dt, 0)
    end
end
```

`find_path` runs A* (octile heuristic + corner-cut prevention)
across the first walkable Tilemap it finds in the scene. The
smoothed path drops the cell-step jitter via line-of-sight
string pulling -- visually you get diagonals and long
straightaways instead of zig-zags. Pass `false` as the 5th arg
when you want to inspect the raw cell path (debugging /
visualisation).

### Doppler-shifted SFX

```lua
-- Per-entity prev-position store for velocity sampling.
local lastPos = {}
local SPEED_OF_SOUND = 8.0  -- units / second, game-tuned

function Car_Update(entity, dt)
    if dt <= 0 then return end
    local ex, ey = transform.get_position(entity)
    local lx, ly = transform.get_position("Listener")
    -- Source velocity from frame-over-frame delta.
    local prev = lastPos[entity] or { x = ex, y = ey }
    local svx = (ex - prev.x) / dt
    local svy = (ey - prev.y) / dt
    lastPos[entity] = { x = ex, y = ey }
    -- Approach speed along the source -> listener axis.
    local dx = lx - ex
    local dy = ly - ey
    local len = math.sqrt(dx * dx + dy * dy) + 1e-6
    local approach = (svx * dx + svy * dy) / len
    local ratio = (SPEED_OF_SOUND + approach) / SPEED_OF_SOUND
    audio.play_positional("Sounds/engine.wav",
        0.8, dx / len, ratio)
end
```

Approaching sources get `ratio > 1` (raised pitch); receding
sources get `ratio < 1`. Clamped to `[0.25, 2.0]` internally
so a runaway delta won't blow the source voice.

## IDE tools

| Tool | Where |
| ---- | ----- |
| **Console pane** | Bottom dock. Shows animation events + script print output + REPL prompt. |
| **Script Editor panel** | Project pane > double-click a `.lua` row. 64 KB text buffer; Save writes + re-runs `LoadFile`; Reload reads from disk. |
| **REPL** | Console pane bottom row. Single-line Lua; Up/Down history. |
| **Edit > Reload Scripts** | Force re-scan + reload every Script asset. |
| **File watcher** | Polls `last_write_time` every 1 s; covers Script + Animation + AnimatorController + Texture + Tileset + Autotile. |
| **Window > Sorting Layers...** | Edit the SpriteRenderer draw order layers (rename / add / reorder). |
| **Window > Collision Matrix...** | Edit collision layers + the NxN collide/skip matrix. |
| **Window > Audio Mixer...** | Master / SFX / Music bus sliders + Reverb (Wet) send. Live-applied to the engine + persisted into `ProjectSettings/project.json` under `audioMixer` (M8c12 / M8c13). |
| **File > Set Current Scene as Main** | Writes the open scene's `Assets/`-relative path into `project.json.mainScene` (M9c12). The packaged game.exe boots into this scene when launched with no `--scene` argument. |
| **File > Build Game...** | Pick a destination folder; the editor copies `scene-editor.exe` to `<dest>/game.exe`, packs `Assets/` into `<dest>/assets.pak` (M9c13 NPAK archive; M9c14 bumped to v2 with per-entry deflate when miniz is vendored), and copies `ProjectSettings/` to `<dest>/ProjectSettings/` (M9c12). The destination must be empty -- the editor refuses to overwrite an arbitrary folder. Launch the dropped `game.exe` directly: with no arguments it auto-detects the project tree alongside it, extracts `assets.pak` to a sibling `Assets/` folder if needed, and falls into standalone mode loading `mainScene`. Headless equivalent: `scene-editor.exe --project <p> --build-game <dest>` runs the same build and exits without opening a window. Drop miniz.h + miniz.c into `third_party/miniz/` to enable pak compression (see that folder's README); the build still works without it -- entries just stay stored. |
| **Viewport collider gizmos** | Every Box / Circle collider renders an outline; the M7c2 gizmo colour shifts from green to amber while the pair is overlapping. M7c13 traces the rotated rect for Box colliders so the gizmo matches the SAT footprint. |
| **Tileset editor** | Project pane > double-click a `.ntileset` row. Click a cell to cycle through the 10 collider shapes (blank / Box / OneWay / 6 slope variants / Trigger). Overlay paints solid red, OneWay orange, trigger blue, slope filled triangles + trapezoids. |

## Rotation summary

`Transform.rotation` (degrees, z-axis CCW) flows through the
entire stack consistently as of M7c17:

| System | Rotation handling | Commit |
| ------ | ----------------- | ------ |
| SpriteRenderer Simple draw | 4 corners rotated, AddImageQuad | M7c12 |
| SpriteRenderer Sliced 9-zone | Each of the 9 tiles rotates as part of one rigid quad | M7c16 |
| SpriteRenderer Tiled mode | Same -- TileQuad threads the rotation matrix through each repeated tile | M7c16 |
| Tilemap draw | Each painted cell rotates around the entity origin via AddImageQuad | M7c17 |
| BoxCollider physics (Trigger / Solid) | SAT (Separating Axis Theorem) on the 4 candidate axes | M7c13 |
| Per-tile collider | Same SAT path -- synthesised cell colliders carry the tilemap's cosR/sinR | M7c17 + M7c18 |
| Collider viewport gizmo | Rotated rect traced as AddPolyline | M7c13 |
| `scene.raycast` | Ray transformed into box local frame | M7c15 |
| `scene.overlap_box` | AABB-vs-OBB SAT | M7c15 |
| `scene.overlap_circle` | Circle centre rotated into box local frame | M7c15 |
| `scene.entity_at` | Point rotated into entity local frame | M7c15 |

CircleCollider is rotation-invariant; SortingLayer + draw
order ignore rotation; the AnimatorController + animation
event payload don't carry rotation themselves (scripts can
read it via `transform.get_rotation`).

## Limitations + future work

- `rigidbody.*` doesn't expose mass / restitution / friction
  yet; tweak those through the inspector or scene JSON. The
  runtime reads them every tick so live edits work without
  reloads.
- Sprite GUID swaps + draw mode + sorting layer aren't in the
  `sprite.*` table; use the inspector instead.
- `entity_at` returns one entity. A future `entities_at(x, y)`
  could return the full overlap list when the use case shows up.
- Custom components (beyond Transform / SpriteRenderer / Tilemap /
  Animator / Script / BoxCollider / CircleCollider / Rigidbody2D)
  aren't registered yet; `add_component` only works for the
  built-in set.
- Scene + Prefab files don't hot-reload through the M6c24 watcher
  -- the data flow is too entangled with the editor's live state
  (selection, undo stack, runtime ScriptComponent flags) for a
  silent reload to do the right thing.
- The M7c14 sub-step ratio is hardcoded (`kPhysicsSubSteps = 4`);
  fast bodies that move more than four times their collider
  thickness per render frame can still tunnel. A project-level
  setting + per-body "is bullet" hint can address this when the
  use case shows up.
- Per-tile colliders fire per-cell `Enter`/`Stay`/`Exit` events
  rather than aggregating to "Body hit Tilemap" once -- a body
  overlapping three flagged cells triggers three Enter events
  back-to-back with the same `other` name. Lua handlers that
  want once-per-tilemap semantics need to dedupe themselves.
- Only slope `Box` / `OneWay` / `Trigger` modes pair with the
  `isTrigger` flag (M7c21). `isTrigger = true` is silently
  ignored on slope shapes; a future commit may add OneWay +
  slope and trigger + slope composites if a use case shows up.
- Slope-vs-slope contacts are silently skipped. Two tilemaps
  whose flagged cells overlap each other won't generate
  events -- the runtime assumes the level designer makes
  cells exclusive.
- Fast bodies can tunnel through thin colliders within a single
  60-FPS step; the M7c10 inside-box fallback resolves bodies
  caught penetrating but a CCD / sub-step pass would prevent
  the penetration in the first place.
- `EdgeCollider` uses a thin-OBB decomposition per segment
  (0.02 unit thickness) routed through the M7c13 SAT path; the
  geometry leaves a ~r residual penetration at the deepest
  contact. A closed-form segment-vs-shape distance pass would
  remove the bump entirely.
- `AudioSource.is3D + loop` is silently ignored. The streaming
  voice path doesn't recompute pan / attenuation as the entity
  moves yet, so a looping 3D source would stay frozen at its
  fire-time position. Wire X3DAudio + per-frame voice updates
  when "music that tracks a 3D object" shows up as a real use
  case.
- Doppler (M8c13) lives only on `audio.play_positional`'s
  optional `freq_ratio` arg; per-voice updates (Spawn + retune)
  aren't exposed yet. Game-driven doppler that follows a
  long-running sound has to re-Spawn on each pitch change.
- Reverb (M8c13) is a fixed I3DL2 Living-Room preset routed off
  the SFX submix. Music + per-voice spawns bypass it; switching
  presets / wiring per-source send levels is future-work.
