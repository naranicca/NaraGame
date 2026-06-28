-- Sample 2D platformer player controller. Demonstrates the
-- M5-M7 stack end-to-end: input + Rigidbody2D physics +
-- AnimatorController integration + Tilemap per-tile colliders
-- + Trigger pickup mechanic.
--
-- Attach to an entity with:
--   - Transform
--   - SpriteRenderer
--   - CircleCollider (layer = "Player")
--   - Rigidbody2D (gravity = (0, -9.8), friction ~ 0.5)
--   - Animator (controller with idle/run + "running" bool)
--   - ScriptComponent (script = this file, entryFunction = "Player")
--
-- Coins are separate entities with CircleCollider isTrigger = true.
-- The level is a Tilemap pointing at a .ntileset with mixed
-- collider shapes (Box / Slope / OneWay).
--
-- Controls:
--   A / D : move left / right
--   Space : jump (cooldown-gated)

local MOVE_SPEED = 4.0
local JUMP_VY    = 6.5
local JUMP_CD    = 0.3
local lastJumpT  = -1.0
local coinsCollected = 0

function Player_Start(entity)
    print("[platformer] Player ready. Use A / D to move + Space to jump.")
end

function Player_Update(entity, dt)
    local vx, vy = rigidbody.get_velocity(entity)

    -- Horizontal: A / D set vx directly so the player
    -- accelerates instantly without losing the gravity-driven
    -- vertical component.
    local move = 0.0
    if input.is_key_down("A") then move = move - 1.0 end
    if input.is_key_down("D") then move = move + 1.0 end
    rigidbody.set_velocity(entity, move * MOVE_SPEED, vy)

    -- Jump: cooldown + apex guard so Space doesn't double-fire.
    if input.is_key_pressed("Space") and
       (time.elapsed() - lastJumpT) > JUMP_CD and
       vy < 1.0 then
        rigidbody.add_force(entity, 0, JUMP_VY)
        lastJumpT = time.elapsed()
        print(string.format("[platformer] Jump @ t=%.2f",
                             time.elapsed()))
    end

    -- Drive the AnimatorController's "running" bool param so the
    -- attached controller transitions idle <-> run on input.
    animator.set_bool(entity, "running", math.abs(move) > 0.1)
end

function Player_TriggerEnter(entity, other)
    -- Trigger entities here are Coins -- collect + destroy.
    if other ~= "" and scene.find(other) then
        coinsCollected = coinsCollected + 1
        print(string.format("[platformer] Coin %s collected (%d total)",
                             other, coinsCollected))
        scene.destroy_entity(other)
    end
end

function Player_CollisionEnter(entity, other)
    print(string.format("[platformer] Landed on %s", other))
end

print("[platformer.lua] loaded")
