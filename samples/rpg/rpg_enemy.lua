-- Top-down RPG enemy AI. Re-pathfinds toward the Player
-- every 0.5 s + walks the smoothed waypoint list with
-- set_velocity. The M7c29 A* + M7c30 string puller pick
-- the corner-hugging route through the dungeon; this
-- script never touches grid math directly.
--
-- Attach to an entity with:
--   - Transform
--   - SpriteRenderer
--   - CircleCollider (layer = "Enemy", radius ~0.25)
--   - Rigidbody2D (gravity = (0, 0), drag/friction = 0)
--   - ScriptComponent (script = this file, entryFunction = "Enemy")
--
-- Looks for an entity named "Player" in the scene each
-- frame; spins down to zero velocity when the player is
-- absent so the enemy doesn't drift.

local SPEED         = 1.5
local PATH_INTERVAL = 0.5
local WAYPOINT_EPS  = 0.15
local lastPathT     = -10.0
local waypoints     = nil
local wpIndex       = 1

function Enemy_Start(entity)
    print("[rpg] Enemy patrolling.")
end

function Enemy_Update(entity, dt)
    if not scene.find("Player") then
        rigidbody.set_velocity(entity, 0, 0)
        return
    end
    local px, py = transform.get_position("Player")
    local ex, ey = transform.get_position(entity)

    -- Re-plan periodically. Re-pathing every frame would
    -- pay the A* cost on every tick + most frames don't
    -- change the navigation answer.
    if (time.elapsed() - lastPathT) > PATH_INTERVAL then
        lastPathT = time.elapsed()
        waypoints = scene.find_path(ex, ey, px, py)
        wpIndex   = 1
        -- Skip the start cell -- find_path includes the
        -- enemy's current cell, but heading toward it
        -- means standing still for one tick.
        if waypoints and #waypoints > 1 then
            wpIndex = 2
        end
    end

    if not waypoints or wpIndex > #waypoints then
        rigidbody.set_velocity(entity, 0, 0)
        return
    end

    local target = waypoints[wpIndex]
    local dx = target.x - ex
    local dy = target.y - ey
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < WAYPOINT_EPS then
        wpIndex = wpIndex + 1
        if wpIndex > #waypoints then
            rigidbody.set_velocity(entity, 0, 0)
            return
        end
        target = waypoints[wpIndex]
        dx = target.x - ex
        dy = target.y - ey
        dist = math.sqrt(dx * dx + dy * dy)
    end
    rigidbody.set_velocity(entity, dx / dist * SPEED,
                                    dy / dist * SPEED)
end

function Enemy_TriggerEnter(entity, other)
    if other == "Player" then
        print(string.format("[rpg] Enemy caught Player at t=%.2f",
                             time.elapsed()))
    end
end

print("[rpg_enemy.lua] loaded")
