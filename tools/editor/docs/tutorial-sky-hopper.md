# Sky Hopper 만들기 — 원버튼 횡스크롤 비행 게임

이 튜토리얼은 NaraGame 에디터와 Lua 스크립트로 작은 새를 조종해
장애물 사이를 통과하는 원버튼 게임을 만듭니다. 특정 상용 게임의
이름·캐릭터·그래픽은 사용하지 않고 장르의 핵심 규칙만 구현합니다.

완성할 기능은 다음과 같습니다.

* Space 또는 마우스 클릭으로 날갯짓
* 중력과 상승 충격
* 오른쪽에서 생성되어 왼쪽으로 흐르는 장애물
* 장애물 통과 점수
* 충돌, 게임 오버, 재시작
* 속도 증가와 간격 축소
* 배경 패럴랙스와 효과음

---

## 0. 준비할 파일

새 프로젝트 이름은 `SkyHopper`로 정합니다.

| 종류 | 경로 | 권장 사양 |
|---|---|---|
| 주인공 | `Assets/Sprites/glider.png` | 투명 배경, 32×24 또는 48×32 |
| 장애물 | `Assets/Sprites/tower.png` | 세로로 반복 가능한 이미지 |
| 지면 | `Assets/Sprites/ground.png` | 가로 반복 이미지 |
| 배경 | `Assets/Sprites/sky.png` | 480×720 이상 |
| 구름 | `Assets/Sprites/clouds.png` | 투명 배경의 넓은 레이어 |
| 날갯짓 효과음 | `Assets/Sounds/flap.wav` | 짧은 WAV |
| 점수 효과음 | `Assets/Sounds/score.wav` | 짧은 WAV |
| 충돌 효과음 | `Assets/Sounds/hit.wav` | 짧은 WAV |

이미지가 없어도 단색 사각형 스프라이트로 진행할 수 있습니다.

---

## 1. 프로젝트와 씬

1. **File → New Project...** 를 선택합니다.
2. Project name에 `SkyHopper`를 입력합니다.
3. 빈 폴더를 선택하고 **Create**를 누릅니다.
4. **File → New Scene**을 선택합니다.
5. `Assets/Scenes/Game.nscene`으로 저장합니다.
6. **File → Set Current Scene as Main**을 선택합니다.

화면은 세로 구성을 권장합니다. 게임 뷰의 기준 크기는 480×720으로
생각하되, 실제 월드 단위는 카메라 Zoom으로 결정합니다.

---

## 2. 카메라와 배경

### 2.1 MainCamera

1. Entity를 만들고 이름을 `MainCamera`로 바꿉니다.
2. **Camera** 컴포넌트를 추가합니다.
3. Position을 `(5, 7.5, 0)`, Zoom을 `48`로 설정합니다.

대략 가로 10 unit, 세로 15 unit가 보이는 구성입니다.

### 2.2 배경 레이어

`Sky`, `CloudsA`, `CloudsB` 엔티티를 만들고 SpriteRenderer를
추가합니다.

| 엔티티 | Position | Order in Layer |
|---|---:|---:|
| `Sky` | `(5, 7.5)` | `-20` |
| `CloudsA` | `(5, 8)` | `-15` |
| `CloudsB` | `(15, 8)` | `-15` |

`CloudsA/B`는 같은 이미지를 나란히 두어 끊김 없이 순환시킵니다.

---

## 3. 주인공 만들기

1. Entity 이름을 `Glider`로 지정합니다.
2. Position을 `(2.5, 8, 0)`으로 설정합니다.
3. **SpriteRenderer**를 추가하고 `glider.png`를 선택합니다.
4. **Rigidbody2D**를 추가합니다.
5. Gravity는 `(0, -24)`, Drag는 `0`으로 설정합니다.
6. **BoxCollider**를 추가하고 캐릭터보다 약간 작게 맞춥니다.
7. Is Trigger는 해제합니다.

충돌 박스를 그림보다 10~20% 작게 만들면 조작감이 덜 가혹합니다.

---

## 4. 지면과 천장

`Ground`와 `Ceiling` 엔티티를 만듭니다.

* `Ground`: Position `(5, 0.4)`, BoxCollider Size `(12, 0.8)`
* `Ceiling`: Position `(5, 15.5)`, BoxCollider Size `(12, 1)`

둘 다 Rigidbody2D는 붙이지 않습니다. Ground에는 SpriteRenderer를
붙이고 Ceiling은 보이지 않게 두어도 됩니다.

---

## 5. 장애물 풀 만들기

Lua API는 실행 중 새 엔티티에 스프라이트 자산을 지정하는 용도보다
미리 만든 엔티티를 재사용하는 방식에 적합합니다. 장애물 세 쌍을
미리 준비합니다.

각 쌍은 다음 엔티티로 구성합니다.

```text
Pipe1Top
Pipe1Bottom
Pipe2Top
Pipe2Bottom
Pipe3Top
Pipe3Bottom
```

각 엔티티에 SpriteRenderer와 BoxCollider를 추가합니다.

* Top은 이미지가 아래를 향하도록 Flip Y 또는 Rotation 180° 적용
* Collider Size 예시: `(1.3, 8)`
* Rigidbody2D는 사용하지 않음
* Order in Layer: `0`

초기 위치는 화면 오른쪽 바깥으로 둡니다. 실제 위치는 스크립트가
게임 시작 때 다시 정합니다.

---

## 6. 게임 스크립트

`Assets/Scripts/SkyHopper.lua`를 만들고 다음 코드를 입력합니다.

```lua
-- Assets/Scripts/SkyHopper.lua

local FLAP_SPEED       = 9.5
local START_X          = 2.5
local START_Y          = 8.0
local PIPE_START_X     = 12.0
local PIPE_SPACING     = 4.2
local PIPE_HALF_HEIGHT = 4.0
local BASE_GAP         = 3.8
local MIN_GAP          = 2.7
local BASE_SCROLL      = 2.8
local MAX_SCROLL       = 4.8
local FLOOR_Y          = 0.8
local CEILING_Y        = 15.0

local pipeCount = 3
local run = {
  mode = "ready",       -- ready / playing / dead
  score = 0,
  best = 0,
  restartDelay = 0,
  scored = {}
}

local function difficulty()
  return math.clamp(run.score / 30, 0, 1)
end

local function currentGap()
  return BASE_GAP + (MIN_GAP - BASE_GAP) * difficulty()
end

local function currentScroll()
  return BASE_SCROLL + (MAX_SCROLL - BASE_SCROLL) * difficulty()
end

local function randomGapY()
  local gap = currentGap()
  local margin = 1.8
  return margin + gap * 0.5 +
         math.random() * (CEILING_Y - margin * 2 - gap)
end

local function placePipe(index, x)
  local gap = currentGap()
  local gapY = randomGapY()
  local topY = gapY + gap * 0.5 + PIPE_HALF_HEIGHT
  local bottomY = gapY - gap * 0.5 - PIPE_HALF_HEIGHT
  transform.set_position("Pipe" .. index .. "Top", x, topY, 0)
  transform.set_position("Pipe" .. index .. "Bottom", x, bottomY, 0)
  run.scored[index] = false
end

local function resetRound()
  run.mode = "ready"
  run.score = 0
  state.score = 0
  run.restartDelay = 0
  transform.set_position("Glider", START_X, START_Y, 0)
  transform.set_rotation("Glider", 0)
  rigidbody.set_velocity("Glider", 0, 0)
  rigidbody.set_gravity("Glider", 0, 0)
  for i = 1, pipeCount do
    placePipe(i, PIPE_START_X + (i - 1) * PIPE_SPACING)
  end
end

local function flap()
  local vx = 0
  rigidbody.set_velocity("Glider", vx, FLAP_SPEED)
  audio.play("Sounds/flap.wav", 0.75)
end

local function beginRound()
  run.mode = "playing"
  rigidbody.set_gravity("Glider", 0, -24)
  flap()
end

local function die()
  if run.mode ~= "playing" then return end
  run.mode = "dead"
  run.restartDelay = 0.8
  rigidbody.set_gravity("Glider", 0, -24)
  audio.play("Sounds/hit.wav", 0.9)
  if run.score > run.best then run.best = run.score end
  state.sky_hopper_best = math.max(state.sky_hopper_best or 0, run.best)
end

function Game_Start(self)
  math.randomseed(math.floor(time.elapsed() * 1000) + 17)
  run.best = state.sky_hopper_best or 0
  resetRound()
end

function Game_Update(self, dt)
  local pressed = input.is_key_pressed("Space") or
                  input.is_mouse_pressed(0)

  if run.mode == "ready" then
    -- 시작 전에는 천천히 떠 있는 느낌만 준다.
    local y = START_Y + math.sin(time.elapsed() * 4) * 0.15
    transform.set_position("Glider", START_X, y, 0)
    if pressed then beginRound() end
    return
  end

  if run.mode == "dead" then
    run.restartDelay = math.max(0, run.restartDelay - dt)
    if pressed and run.restartDelay == 0 then resetRound() end
    return
  end

  if pressed then flap() end

  local gx, gy = transform.get_position("Glider")
  local _, vy = rigidbody.get_velocity("Glider")
  transform.set_rotation("Glider", math.clamp(vy * 4, -35, 25))
  if gy < FLOOR_Y or gy > CEILING_Y then die() end

  local speed = currentScroll()
  for i = 1, pipeCount do
    local top = "Pipe" .. i .. "Top"
    local bottom = "Pipe" .. i .. "Bottom"
    local x, topY = transform.get_position(top)
    local _, bottomY = transform.get_position(bottom)
    x = x - speed * dt
    transform.set_position(top, x, topY, 0)
    transform.set_position(bottom, x, bottomY, 0)
    if not run.scored[i] and x < gx then
      run.scored[i] = true
      run.score = run.score + 1
      state.score = run.score
      audio.play("Sounds/score.wav", 0.8)
    end

    if x < -1.5 then
      -- 세 쌍이 같은 간격으로 순환하므로 자기 위치에 전체 풀의
      -- 폭을 더하면 항상 가장 오른쪽 슬롯으로 돌아간다.
      placePipe(i, x + PIPE_SPACING * pipeCount)
    end
  end

  -- 구름 두 장을 순환시킨다.
  for _, name in ipairs({"CloudsA", "CloudsB"}) do
    local x, y = transform.get_position(name)
    x = x - speed * 0.12 * dt
    if x < -5 then x = x + 20 end
    transform.set_position(name, x, y, 0)
  end
end

function Bird_CollisionEnter(self, other)
  if other == "Ground" or other == "Ceiling" or
     other:sub(1, 4) == "Pipe" then
    die()
  end
end
```

`GameController` 엔티티를 만들고 Script 컴포넌트를 추가합니다.

* Script: `Scripts/SkyHopper.lua`
* Entry function: `Game`

`Glider`에도 같은 Script를 추가합니다.

* Script: `Scripts/SkyHopper.lua`
* Entry function: `Bird`

---

## 7. 점수 HUD

플랫폼 튜토리얼 19장의 숫자 스프라이트 방식과 동일하게
`digit_0`~`digit_9` 서브 스프라이트를 준비합니다. `ScoreD0`,
`ScoreD1`, `ScoreD2`, `BestD0`~`BestD2` 엔티티를 카메라의 자식으로
둡니다.

`Assets/Scripts/SkyHud.lua`:

```lua
local function drawNumber(prefix, value, digits)
  value = math.max(0, math.floor(value or 0))
  for i = 0, digits - 1 do
    local n = math.floor(value / 10 ^ i) % 10
    sprite.set_sub_sprite(prefix .. i, "digit_" .. n)
  end
end

function SkyHud_Update(self, dt)
  drawNumber("ScoreD", state.score or 0, 3)
  drawNumber("BestD", state.sky_hopper_best or 0, 3)
end
```

HUD 루트 엔티티에 Script를 붙이고 Entry function을 `SkyHud`로
설정합니다.

---

## 8. 테스트 체크리스트

Play를 켜고 다음을 확인합니다.

1. 시작 전에는 새가 제자리에서 천천히 움직입니다.
2. Space 또는 왼쪽 클릭으로 게임이 시작됩니다.
3. 입력할 때마다 상승 속도가 즉시 갱신됩니다.
4. 장애물이 왼쪽으로 흐르고 화면 밖에서 재사용됩니다.
5. 한 쌍을 통과할 때 점수가 정확히 한 번 증가합니다.
6. 장애물이나 바닥에 닿으면 게임 오버가 됩니다.
7. 잠깐 뒤 입력하면 새 라운드가 시작됩니다.
8. 점수가 오를수록 속도가 증가하고 통로가 좁아집니다.

---

## 9. 확장 아이디어

* Animator로 날갯짓 2~3프레임 재생
* 충돌 시 ParticleSystem으로 깃털 방출
* `savegame.save()`로 최고 점수 영구 저장
* 장애물 스킨과 낮/밤 배경 무작위 선택
* 모바일에서는 화면 전체 터치를 날갯짓 입력으로 사용
* 점수 구간별 메달과 새 기록 연출

이제 원버튼 비행 게임의 핵심 루프가 완성되었습니다.
