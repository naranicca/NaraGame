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

`OnAnimationEvent` carries the M5c19 payload: a string, a float,
an int, a bool, a `(vec2.x, vec2.y)` pair, and a
`(color.r, color.g, color.b, color.a)` quad.

## API surface

The host registers seven global tables: `animator`, `transform`,
`sprite`, `input`, `time`, `scene`, `math` (extension methods on
the stdlib `math` table). Every API entry takes the target
entity's name as its first argument (when applicable). Missing
entities and missing components are silent no-ops that return a
sensible default rather than raising errors.

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

### `time.*`

| Function | Returns |
| -------- | ------- |
| `time.delta()`       | This frame's dt (ImGui::GetIO().DeltaTime) |
| `time.elapsed()`     | Running double, bumped by the host each frame |
| `time.frame_count()` | Running long long |
| `time.fps()`         | ImGui::GetIO().Framerate (smoothed) |

### `scene.*`

Twenty entries for entity / component / hierarchy / picking +
broadcast. Every callback resolves the entity by name through
the bound `Scene`.

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

Component type names match the C++ `Component::TypeName()`
strings: `"Transform"`, `"SpriteRenderer"`, `"Tilemap"`,
`"Animator"`, `"Script"`.

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

## IDE tools

| Tool | Where |
| ---- | ----- |
| **Console pane** | Bottom dock. Shows animation events + script print output + REPL prompt. |
| **Script Editor panel** | Project pane > double-click a `.lua` row. 64 KB text buffer; Save writes + re-runs `LoadFile`; Reload reads from disk. |
| **REPL** | Console pane bottom row. Single-line Lua; Up/Down history. |
| **Edit > Reload Scripts** | Force re-scan + reload every Script asset. |
| **File watcher** | Polls `last_write_time` every 1 s; covers Script + Animation + AnimatorController + Texture + Tileset + Autotile. |

## Limitations + future work

- `transform.rotation` round-trips fine, but the M5 SpriteRenderer
  draw path doesn't honor rotation yet -- the picker (`entity_at`)
  uses axis-aligned bounds for the same reason.
- Sprite GUID swaps + draw mode + sorting layer aren't in the
  `sprite.*` table; use the inspector instead.
- `entity_at` returns one entity. A future `entities_at(x, y)`
  could return the full overlap list when the use case shows up.
- Custom components (beyond Transform / SpriteRenderer / Tilemap /
  Animator / Script) aren't registered yet; `add_component` only
  works for the built-in set.
- Scene + Prefab files don't hot-reload through the M6c24 watcher
  -- the data flow is too entangled with the editor's live state
  (selection, undo stack, runtime ScriptComponent flags) for a
  silent reload to do the right thing.
