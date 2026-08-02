-- Assets/Scripts/ArenaGame.lua

local ENEMY_COUNT  = 30
local BULLET_COUNT = 24
local GEM_COUNT    = 30

local PLAYER_RADIUS = 0.35
local ENEMY_RADIUS  = 0.32
local BULLET_RADIUS = 0.12
local GEM_RADIUS    = 0.18

local game = {
  mode = "playing",
  elapsed = 0,
  kills = 0,
  hp = 100,
  maxHp = 100,
  hurtTimer = 0,
  level = 1,
  xp = 0,
  xpNext = 5,
  moveSpeed = 5.0,
  damage = 10,
  fireDelay = 0.65,
  fireTimer = 0,
  spawnTimer = 0,
  upgradeIndex = 0,
  enemies = {},
  bullets = {},
  gems = {}
}

local function entityName(prefix, i)
  return prefix .. i
end

local function distanceSq(ax, ay, bx, by)
  local dx, dy = bx - ax, by - ay
  return dx * dx + dy * dy
end

local function normalize(x, y)
  local len = math.sqrt(x * x + y * y)
  if len < 0.0001 then return 0, 0 end
  return x / len, y / len
end

local function hidePools()
  for i = 1, ENEMY_COUNT do
    scene.set_enabled(entityName("Enemy", i), false)
    game.enemies[i] = { active = false, hp = 0 }
  end
  for i = 1, BULLET_COUNT do
    scene.set_enabled(entityName("Bullet", i), false)
    game.bullets[i] = { active = false, vx = 0, vy = 0, life = 0 }
  end
  for i = 1, GEM_COUNT do
    scene.set_enabled(entityName("Gem", i), false)
    game.gems[i] = { active = false }
  end
end

local function resetGame()
  game.mode = "playing"
  game.elapsed = 0
  game.kills = 0
  game.hp = 100
  game.maxHp = 100
  game.hurtTimer = 0
  game.level = 1
  game.xp = 0
  game.xpNext = 5
  game.moveSpeed = 5.0
  game.damage = 10
  game.fireDelay = 0.65
  game.fireTimer = 0
  game.spawnTimer = 0
  game.upgradeIndex = 0
  transform.set_position("Player", 0, 0, 0)
  transform.set_position("MainCamera", 0, 0, 0)
  sprite.set_alpha("Player", 1)
  hidePools()
  state.score = 0
  state.level = 1
end

local function spawnEnemy()
  local slot = nil
  for i = 1, ENEMY_COUNT do
    if not game.enemies[i].active then slot = i break end
  end
  if not slot then return end

  local px, py = transform.get_position("Player")
  local angle = math.random() * math.pi * 2
  local radius = 8.0 + math.random() * 2.0
  local x = px + math.cos(angle) * radius
  local y = py + math.sin(angle) * radius
  local hp = 20 + math.floor(game.elapsed / 30) * 6

  local name = entityName("Enemy", slot)
  transform.set_position(name, x, y, 0)
  scene.set_enabled(name, true)
  game.enemies[slot] = { active = true, hp = hp, maxHp = hp }
end

local function nearestEnemy(px, py)
  local best, bestDist = nil, 1000000
  for i = 1, ENEMY_COUNT do
    if game.enemies[i].active then
      local x, y = transform.get_position(entityName("Enemy", i))
      local d = distanceSq(px, py, x, y)
      if d < bestDist then best, bestDist = i, d end
    end
  end
  return best
end

local function fireBullet()
  local px, py = transform.get_position("Player")
  local target = nearestEnemy(px, py)
  if not target then return end

  local slot = nil
  for i = 1, BULLET_COUNT do
    if not game.bullets[i].active then slot = i break end
  end
  if not slot then return end

  local ex, ey = transform.get_position(entityName("Enemy", target))
  local dx, dy = normalize(ex - px, ey - py)
  local name = entityName("Bullet", slot)
  transform.set_position(name, px, py, 0)
  transform.set_rotation(name, math.atan(dy, dx) * 180 / math.pi)
  scene.set_enabled(name, true)
  game.bullets[slot] = {
    active = true, vx = dx * 11, vy = dy * 11, life = 1.4
  }
  audio.play("Sounds/shoot.wav", 0.35)
end

local function spawnGem(x, y)
  for i = 1, GEM_COUNT do
    if not game.gems[i].active then
      local name = entityName("Gem", i)
      transform.set_position(name, x, y, 0)
      scene.set_enabled(name, true)
      game.gems[i].active = true
      return
    end
  end
end

local function killEnemy(i)
  local name = entityName("Enemy", i)
  local x, y = transform.get_position(name)
  game.enemies[i].active = false
  scene.set_enabled(name, false)
  game.kills = game.kills + 1
  state.score = game.kills
  spawnGem(x, y)
end

local function applyUpgrade()
  -- 첫 버전은 레벨마다 세 업그레이드를 순환 적용한다.
  -- 이후 dialog.choices로 플레이어가 직접 고르게 확장할 수 있다.
  game.upgradeIndex = (game.upgradeIndex % 3) + 1
  if game.upgradeIndex == 1 then
    game.damage = game.damage + 4
  elseif game.upgradeIndex == 2 then
    game.fireDelay = math.max(0.18, game.fireDelay * 0.88)
  else
    game.moveSpeed = game.moveSpeed + 0.35
  end
  audio.play("Sounds/levelup.wav", 0.8)
end

local function gainXp(amount)
  game.xp = game.xp + amount
  while game.xp >= game.xpNext do
    game.xp = game.xp - game.xpNext
    game.level = game.level + 1
    game.xpNext = math.floor(game.xpNext * 1.35 + 2)
    state.level = game.level
    applyUpgrade()
  end
end

local function updatePlayer(dt)
  local dx, dy = 0, 0
  if input.is_key_down("A") then dx = dx - 1 end
  if input.is_key_down("D") then dx = dx + 1 end
  if input.is_key_down("W") then dy = dy + 1 end
  if input.is_key_down("S") then dy = dy - 1 end
  dx, dy = normalize(dx, dy)

  local px, py = transform.get_position("Player")
  px = math.clamp(px + dx * game.moveSpeed * dt, -19, 19)
  py = math.clamp(py + dy * game.moveSpeed * dt, -19, 19)
  transform.set_position("Player", px, py, 0)
  if dx ~= 0 then sprite.set_flip_x("Player", dx < 0) end

  -- 카메라 스무딩. 프레임률과 무관하도록 dt를 반영한다.
  local cx, cy = transform.get_position("MainCamera")
  local follow = math.clamp(dt * 7, 0, 1)
  transform.set_position("MainCamera",
    math.lerp(cx, px, follow), math.lerp(cy, py, follow), 0)

  game.hurtTimer = math.max(0, game.hurtTimer - dt)
  if game.hurtTimer > 0 then
    local blink = math.floor(game.hurtTimer * 12) % 2 == 0
    sprite.set_alpha("Player", blink and 1 or 0.35)
  else
    sprite.set_alpha("Player", 1)
  end
end

local function updateEnemies(dt)
  local px, py = transform.get_position("Player")
  local enemySpeed = 1.7 + math.min(1.5, game.elapsed / 120)
  for i = 1, ENEMY_COUNT do
    if game.enemies[i].active then
      local name = entityName("Enemy", i)
      local x, y = transform.get_position(name)
      local dx, dy = normalize(px - x, py - y)
      x, y = x + dx * enemySpeed * dt, y + dy * enemySpeed * dt
      transform.set_position(name, x, y, 0)
      sprite.set_flip_x(name, dx < 0)

      local touchR = PLAYER_RADIUS + ENEMY_RADIUS
      if game.hurtTimer == 0 and distanceSq(px, py, x, y) < touchR * touchR then
        game.hp = game.hp - 12
        game.hurtTimer = 0.75
        audio.play("Sounds/hit.wav", 0.65)
        if game.hp <= 0 then
          game.hp = 0
          game.mode = "dead"
        end
      end
    end
  end
end

local function updateBullets(dt)
  for i = 1, BULLET_COUNT do
    local b = game.bullets[i]
    if b.active then
      local name = entityName("Bullet", i)
      local x, y = transform.get_position(name)
      x, y = x + b.vx * dt, y + b.vy * dt
      b.life = b.life - dt
      transform.set_position(name, x, y, 0)

      local hit = false
      for e = 1, ENEMY_COUNT do
        if game.enemies[e].active then
          local ex, ey = transform.get_position(entityName("Enemy", e))
          local hitR = BULLET_RADIUS + ENEMY_RADIUS
          if distanceSq(x, y, ex, ey) < hitR * hitR then
            game.enemies[e].hp = game.enemies[e].hp - game.damage
            if game.enemies[e].hp <= 0 then killEnemy(e) end
            hit = true
            break
          end
        end
      end

      if hit or b.life <= 0 then
        b.active = false
        scene.set_enabled(name, false)
      end
    end
  end
end

local function updateGems(dt)
  local px, py = transform.get_position("Player")
  for i = 1, GEM_COUNT do
    if game.gems[i].active then
      local name = entityName("Gem", i)
      local x, y = transform.get_position(name)
      local d2 = distanceSq(px, py, x, y)
      if d2 < 9 then
        local dx, dy = normalize(px - x, py - y)
        x, y = x + dx * 7 * dt, y + dy * 7 * dt
        transform.set_position(name, x, y, 0)
      end
      local pickupR = PLAYER_RADIUS + GEM_RADIUS
      if distanceSq(px, py, x, y) < pickupR * pickupR then
        game.gems[i].active = false
        scene.set_enabled(name, false)
        gainXp(1)
      end
    end
  end
end

local function updateHud()
  local hpRatio = game.hp / game.maxHp
  local xpRatio = game.xp / game.xpNext
  sprite.set_size("HpFill", 3.2 * hpRatio, 0.24)
  sprite.set_size("XpFill", 3.2 * xpRatio, 0.16)

  local function digits(prefix, value, count)
    for i = 0, count - 1 do
      local d = math.floor(value / 10 ^ i) % 10
      sprite.set_sub_sprite(prefix .. i, "digit_" .. d)
    end
  end
  digits("LevelD", game.level, 2)
  digits("KillD", game.kills, 3)
end

function Arena_Start(self)
  math.randomseed(math.floor(time.elapsed() * 1000) + 91)
  resetGame()
  audio.play_music("Sounds/arena.wav", 0.45, true)
end

function Arena_Update(self, dt)
  dt = math.min(dt, 1 / 30) -- 긴 프레임에서 충돌을 건너뛰지 않게 제한

  if game.mode == "dead" then
    updateHud()
    if input.is_key_pressed("R") then resetGame() end
    return
  end

  game.elapsed = game.elapsed + dt
  updatePlayer(dt)

  game.spawnTimer = game.spawnTimer - dt
  if game.spawnTimer <= 0 then
    spawnEnemy()
    game.spawnTimer = math.max(0.18, 0.9 - game.elapsed / 180)
  end

  game.fireTimer = game.fireTimer - dt
  if game.fireTimer <= 0 then
    fireBullet()
    game.fireTimer = game.fireDelay
  end

  updateEnemies(dt)
  updateBullets(dt)
  updateGems(dt)
  updateHud()
end