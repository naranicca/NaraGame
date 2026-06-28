-- Top-down RPG player controller. Demonstrates the M7c29
-- pathfinding stack alongside M7c2-c10 physics + M6 input
-- bindings; the Lua side stays small because the grid +
-- collider math lives in the runtime.
--
-- Attach to an entity with:
--   - Transform
--   - SpriteRenderer
--   - CircleCollider (layer = "Player", radius ~0.25)
--   - Rigidbody2D (gravity = (0, 0), drag/friction = 0)
--   - ScriptComponent (script = this file, entryFunction = "Player")
--
-- Controls:
--   W / A / S / D : 4-directional move (diagonal normalised)
--
-- Triggers an event handler if the player overlaps any
-- entity named "Goal".

local SPEED = 3.0

function Player_Start(entity)
    print("[rpg] Player ready. WASD to move.")
end

function Player_Update(entity, dt)
    local vx, vy = 0.0, 0.0
    if input.is_key_down("W") then vy = vy + 1.0 end
    if input.is_key_down("S") then vy = vy - 1.0 end
    if input.is_key_down("A") then vx = vx - 1.0 end
    if input.is_key_down("D") then vx = vx + 1.0 end
    local len = math.sqrt(vx * vx + vy * vy)
    if len > 0.001 then
        vx = vx / len * SPEED
        vy = vy / len * SPEED
    end
    rigidbody.set_velocity(entity, vx, vy)
end

function Player_TriggerEnter(entity, other)
    if other == "Goal" then
        print("[rpg] Player reached the Goal! Win.")
    end
end

print("[rpg_player.lua] loaded")
