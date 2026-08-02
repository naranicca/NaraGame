# Nightfall Arena 만들기 — 자동 공격 아레나 생존 게임

> `samples/nightfall-arena/Assets`에는 필요한 PNG와 WAV가 모두 있습니다.
> 새 프로젝트의 `Assets`로 복사한 뒤 동일한 파일명을 선택하십시오.

이 튜토리얼은 넓은 맵을 이동하며 몰려오는 적을 피하고, 가장 가까운
적을 자동으로 공격하며 성장하는 아레나 생존 게임을 만듭니다. 특정
상용 게임의 캐릭터·명칭·에셋은 사용하지 않고 장르의 시스템만
구현합니다.

완성할 기능은 다음과 같습니다.

* WASD 이동과 플레이어 추적 카메라
* 화면 가장자리에서 계속 생성되는 적
* 가장 가까운 적을 향한 자동 공격
* 투사체·적·경험치 보석 오브젝트 풀
* 체력, 접촉 피해, 무적 시간
* 경험치와 레벨업
* 공격력·공격 속도·이동 속도 성장
* 시간에 따른 생성 속도와 적 체력 증가
* 처치 수, 생존 시간, 게임 오버와 재시작

---

## 0. 프로젝트와 에셋

프로젝트 이름은 `NightfallArena`로 정합니다.

| 종류 | 파일 | 권장 사양 |
|---|---|---|
| 플레이어 | `Assets/Sprites/ranger.png` | 32×32 또는 48×48 |
| 적 | `Assets/Sprites/shade.png` | 32×32 |
| 투사체 | `Assets/Sprites/spark.png` | 8×8 또는 16×16 |
| 경험치 | `Assets/Sprites/gem.png` | 12×12 또는 16×16 |
| 바닥 | `Assets/Sprites/arena_floor.png` | 반복 가능한 정사각 타일 |
| 숫자 | `Assets/Sprites/digits.png` | `digit_0`~`digit_9` HUD 시트 |
| 피격음 | `Assets/Sounds/hit.wav` | 짧은 WAV |
| 발사음 | `Assets/Sounds/shoot.wav` | 짧은 WAV |
| 레벨업음 | `Assets/Sounds/levelup.wav` | 짧은 WAV |
| 배경 음악 | `Assets/Sounds/arena.wav` | 반복 가능한 WAV |

1. 새 프로젝트를 생성합니다.
2. `Assets/Scenes/Arena.nscene`을 만들고 저장합니다.
3. 현재 씬을 Main Scene으로 지정합니다.

---

## 1. 아레나와 카메라

### 1.1 바닥

`ArenaFloor` 엔티티에 SpriteRenderer를 추가합니다.

* Position: `(0, 0, 0)`
* Draw Mode: `Tiled`
* Size: `(40, 40)`
* Order in Layer: `-20`

또는 Tilemap으로 40×40 정도의 바닥을 그려도 됩니다. 첫 버전에서는
벽을 만들지 않아 적과 플레이어 이동 로직을 단순하게 유지합니다.

### 1.2 카메라

`MainCamera` 엔티티를 만들고 Camera 컴포넌트를 추가합니다.

* Position: `(0, 0, 0)`
* Zoom: `48`
* Priority: `0`

카메라는 스크립트에서 플레이어를 부드럽게 따라갑니다.

---

## 2. 플레이어

`Player` 엔티티를 만들고 SpriteRenderer를 추가합니다.

* Sprite: `ranger.png`
* Position: `(0, 0, 0)`
* Order in Layer: `10`

이 튜토리얼은 수십 개체를 Lua에서 직접 이동시키므로 Rigidbody2D를
사용하지 않습니다. 충돌도 원의 거리 계산으로 처리합니다.

---

## 3. 오브젝트 풀 준비

실행 중에 자산이 붙은 엔티티를 매번 생성하는 대신 필요한 최대
개수를 씬에 미리 만들고 enabled를 켜고 끕니다.

### 3.1 적 30개

`Enemy1`을 만든 뒤 SpriteRenderer에 `shade.png`를 지정합니다.
`Enemy30`까지 Duplicate합니다.

* 이름: `Enemy1`~`Enemy30`
* Order in Layer: `8`
* enabled: 모두 해제

### 3.2 투사체 24개

`Bullet1`~`Bullet24`를 만듭니다.

* Sprite: `spark.png`
* Order in Layer: `12`
* enabled: 모두 해제

### 3.3 경험치 보석 30개

`Gem1`~`Gem30`을 만듭니다.

* Sprite: `gem.png`
* Order in Layer: `7`
* enabled: 모두 해제

풀 엔티티에는 Rigidbody2D와 Collider가 필요하지 않습니다.

---

## 4. HUD

카메라 자식으로 `HUD` 엔티티를 만들고 그 아래에 다음 항목을 둡니다.

```text
HUD
├─ HpBack
├─ HpFill
├─ XpBack
├─ XpFill
├─ LevelD0
├─ LevelD1
├─ KillD0
├─ KillD1
└─ KillD2
```

`HpBack/XpBack`은 어두운 단색 스프라이트, `HpFill`은 빨간색,
`XpFill`은 청록색으로 설정합니다. Fill 스프라이트의 Pivot은
BottomLeft로 두는 것이 크기 변경에 편리합니다.

Fill 엔티티의 Transform Scale은 `(1, 1)`로 유지하십시오.
아래 스크립트가 `sprite.set_size`로 폭을 바꾸므로 Transform Scale까지
크게 설정하면 두 크기가 곱해져 막대가 화면을 덮게 됩니다.

번들된 `Assets/Sprites/digits.png`를 10등분하여 만든
`digit_0`~`digit_9` 서브 스프라이트를 사용합니다.

---

## 5. 전체 게임 스크립트

`Assets/Scripts/ArenaGame.lua`를 만듭니다. 이 스크립트 하나가 풀과
게임 상태를 관리합니다.

```lua
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
```

`GameController` 엔티티를 만들고 Script 컴포넌트를 붙입니다.

* Script: `Scripts/ArenaGame.lua`
* Entry function: `Arena`

---

## 6. HUD 위치 고정

HUD가 월드와 함께 움직이지 않게 `HUD`를 `MainCamera`의 자식으로
만듭니다. 카메라 기준 로컬 위치를 사용합니다.

예시:

| 엔티티 | 로컬 Position |
|---|---:|
| `HpBack/HpFill` | `(-4.2, 6.7)` |
| `XpBack/XpFill` | `(-4.2, 6.3)` |
| `LevelD0/1` | `(-4.2, 5.9)` 부근 |
| `KillD0/1/2` | `(3.2, 6.5)` 부근 |

카메라 Zoom이나 게임 화면 비율에 따라 위치를 조정합니다.

---

## 7. 게임 오버 표시

`GameOverPanel` 엔티티와 자식 스프라이트를 카메라 아래에 만들고
처음에는 disabled로 둡니다. 위 코드에서 모드가 바뀔 때 표시하려면
다음을 추가합니다.

```lua
-- resetGame() 안
scene.set_enabled("GameOverPanel", false)

-- hp가 0이 되는 부분
game.mode = "dead"
scene.set_enabled("GameOverPanel", true)
```

패널에는 `R TO RESTART` 같은 직접 제작한 텍스트 스프라이트를 사용할
수 있습니다.

---

## 8. 선택형 레벨업으로 확장

첫 버전은 테스트하기 쉽게 업그레이드를 자동 순환합니다. 선택 창을
사용하려면 `applyUpgrade()` 대신 다음 흐름을 사용합니다.

```lua
dialog.choices("Choose an upgrade", {
  "Power +4", "Attack speed +12%", "Move speed +0.35"
})
```

`dialog.last_choice()`가 1~3을 반환하면 해당 효과를 적용하고
`dialog.hide()`를 호출합니다. 선택 중에는 `Arena_Update`의 적·투사체
업데이트를 건너뛰어 게임을 일시 정지시키는 것이 좋습니다.

---

## 9. 테스트 체크리스트

1. WASD 대각선 이동 속도가 직선 이동과 같은지 확인합니다.
2. 카메라가 플레이어를 부드럽게 따라오는지 확인합니다.
3. 적이 플레이어 주변 바깥쪽에서 생성되는지 확인합니다.
4. 가장 가까운 적을 향해 자동 발사되는지 확인합니다.
5. 투사체가 적에게 한 번만 피해를 주고 풀로 돌아가는지 확인합니다.
6. 적이 죽으면 보석이 나오고 가까이 가면 끌려오는지 확인합니다.
7. 경험치가 가득 차면 레벨과 능력치가 증가하는지 확인합니다.
8. 적 접촉 후 0.75초 동안 연속 피해를 받지 않는지 확인합니다.
9. 시간이 지날수록 적 생성이 빨라지고 체력이 증가하는지 확인합니다.
10. HP가 0이면 게임이 멈추고 R로 초기화되는지 확인합니다.

---

## 10. 성능과 확장

### 풀이 부족할 때

적이나 투사체가 더 필요하면 엔티티를 Duplicate하고 코드의
`ENEMY_COUNT`, `BULLET_COUNT`, `GEM_COUNT`도 같은 값으로 늘립니다.

### 다음에 추가할 시스템

* 여러 투사체와 부채꼴 발사
* 관통, 치명타, 넉백
* 일정 시간마다 발동하는 범위 공격
* 보스와 엘리트 적
* 적끼리 겹치지 않게 하는 간단한 분리 힘
* 경험치 선택 창과 무기 진화
* 10분 생존 승리 조건
* `savegame`으로 최고 생존 시간과 해금 저장
* ParticleSystem으로 피격·레벨업 연출
* 모바일 가상 스틱 또는 터치 이동

이제 자동 공격 아레나 생존 게임의 핵심 루프가 완성되었습니다.
