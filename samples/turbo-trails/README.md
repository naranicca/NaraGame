# Turbo Trails 만들기 — 의사 3D 카트 레이싱

> `samples/turbo-trails/Assets`에는 카트, 도로, 하늘, 숫자와 그림자 PNG가
> 모두 포함되어 있습니다. 새 프로젝트의 `Assets`로 복사한 뒤 진행하십시오.

이 문서는 NaraGame을 처음 사용하는 사람도 완주 가능한 1인용 카트
레이싱 게임을 만드는 튜토리얼입니다. 실제 3D 모델 대신 여러 개의 얇은
텍스처 사다리꼴과 2D 카트 스프라이트를 사용합니다. 모든 이름과 디자인은
예제용 창작물이며, 다른 게임의 캐릭터·트랙·아이템·음원을 복제하지 않습니다.

완성 결과에는 다음 기능이 포함됩니다.

* 원근감이 있는 곡선·언덕 트랙
* 키보드와 게임패드 가속, 브레이크, 조향
* 속도에 따라 달라지는 조향과 잔디 감속
* 간단한 드리프트와 부스트
* AI 카트, 접촉 감속, 순위 계산
* 체크포인트를 이용한 3랩 경기
* 재시작과 일시 정지
* 모바일에서도 재사용되는 고정 크기 오브젝트 풀

---

## 1. 렌더링 원리

게임 규칙은 위에서 내려다본 2D 트랙 좌표로 계산합니다.

* `distance`: 트랙 시작점으로부터 전진한 거리
* `lane`: 도로 중심을 0으로 둔 좌우 위치, 보통 -1.2~1.2
* `speed`: 초당 전진 거리

화면은 수평선에서 플레이어 쪽으로 48개의 도로 조각을 그립니다. 멀리 있는
조각은 좁고 얇으며, 가까운 조각은 넓고 두껍습니다. 각 조각은
`sprite.set_quad`로 네 꼭짓점을 지정합니다. 곡선은 조각의 중심을 좌우로
누적 이동시키고, 언덕은 조각의 Y 위치를 움직여 표현합니다.

이 방식은 실제 3D 충돌이나 메시를 사용하지 않습니다. 따라서 게임 규칙은
간단하고 결과는 고전적인 16비트 의사 3D 화면처럼 보입니다.

---

## 2. 프로젝트 생성

1. NaraGame을 실행합니다.
2. **File → New Project...**를 선택합니다.
3. 프로젝트 이름을 `TurboTrails`로 지정합니다.
4. 다음 폴더를 만듭니다.

```text
Assets/
  Audio/
  Scripts/
  Sprites/
    Karts/
    Track/
    UI/
  Scenes/
```

`Assets/Scenes/Race.nscene`을 만들고 **File → Set Current Scene as Main**을
선택합니다.

---

## 3. 에셋 준비

직접 그리거나 사용 허가를 받은 다음 PNG를 준비합니다.

| 파일 | 권장 크기 | 내용 |
|---|---:|---|
| `track-strip.png` | 256×32 | 바깥 잔디, 경계선, 도로, 경계선, 잔디가 가로로 배치된 띠 |
| `sky.png` | 640×180 | 반복되지 않는 하늘과 먼 산 |
| `player-kart.png` | 96×80 | 뒤에서 본 플레이어 카트 |
| `rival-karts.png` | 128×64 | `Blue`, `Red` 두 AI 카트가 들어간 시트 |
| `digits.png` | 160×24 | 0~9가 가로로 들어간 스프라이트 시트 |
| `shadow.png` | 64×20 | 반투명 타원 그림자 |

카트에는 기존 게임 캐릭터, 로고, 고유 문양을 사용하지 않습니다. 이 예제의
테마는 황금빛 초원과 풍력 터빈이 있는 독창적인 경주장으로 정합니다.

각 텍스처를 Project 패널에서 선택하고 Pixels Per Unit을 다음처럼 맞춥니다.

* 도로와 하늘: `48`
* 카트와 UI: `64`
* 필터: 픽셀 아트면 Point, 부드러운 그림이면 Linear

`digits.png`는 Sprite Mode를 Multiple로 바꾸고 `Digit0`부터 `Digit9`까지
16×24 크기로 자릅니다.

---

## 4. 입력과 물리 업데이트

레이싱 물리는 렌더 프레임 속도와 분리해야 합니다. NaraGame의
`Race_FixedUpdate(entity, dt)`는 60Hz로 호출되므로 속도, 조향, AI를 여기에
둡니다. 화면 투영은 매 프레임 `Race_Update`에서 처리합니다.

조작은 다음과 같습니다.

| 기능 | 키보드 | 게임패드 |
|---|---|---|
| 가속 | W 또는 Up | A 또는 오른쪽 트리거 |
| 브레이크 | S 또는 Down | B 또는 왼쪽 트리거 |
| 조향 | A/D 또는 Left/Right | 왼쪽 스틱 X |
| 드리프트 | Space | 오른쪽 숄더 |
| 일시 정지 | Escape | Start |
| 재시작 | R | Y |

---

## 5. Race 씬 만들기

Hierarchy를 다음과 같이 구성합니다. 이름은 스크립트가 그대로 찾으므로 철자를
정확히 입력합니다.

```text
RaceController       Script (Entry Function: Race)
MainCamera            Camera
Sky                   SpriteRenderer
Road01                SpriteRenderer
...
Road48                SpriteRenderer
Rival01               SpriteRenderer
...
Rival08               SpriteRenderer
PlayerShadow          SpriteRenderer
PlayerKart            SpriteRenderer
LapDigit              SpriteRenderer
PositionDigit         SpriteRenderer
SpeedDigit1           SpriteRenderer
SpeedDigit2           SpriteRenderer
SpeedDigit3           SpriteRenderer
```

설정 요약:

* `MainCamera`: Position `(0,0,0)`, Zoom `48`, Priority `10`
* `Sky`: 화면 위쪽을 덮도록 크기와 위치 조정, Order `-100`
* `Road01~48`: `track-strip.png`, Draw Mode `Simple`, Order `-48~-1`
* `Rival01~08`: 카트 텍스처, 처음에는 비활성화
* `PlayerShadow`: Order `90`
* `PlayerKart`: Order `100`, 화면 아래 중앙
* UI 숫자: Order `200` 이상

도로 오브젝트의 Inspector에서 **Custom Quad**를 켤 필요는 없습니다. 스크립트의
첫 프레임에 `sprite.set_quad`가 자동으로 켭니다.

---

## 6. 전체 스크립트

`Assets/Scripts/TurboTrails.lua`를 만들고 아래 코드를 그대로 입력합니다.

```lua
local SEGMENT_LENGTH = 8.0
local ROAD_COUNT = 48
local TRACK_SEGMENTS = 240
local TRACK_LENGTH = SEGMENT_LENGTH * TRACK_SEGMENTS
local MAX_SPEED = 92.0
local ROAD_LIMIT = 1.0

local game = {
  distance = 0.0,
  lane = 0.0,
  speed = 0.0,
  lap = 1,
  lapsToWin = 3,
  paused = false,
  finished = false,
  boost = 0.0,
  driftCharge = 0.0,
  lastSteer = 0.0,
  rivals = {},
  visibleRivals = {},
}

local track = {}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function wrapDistance(v)
  while v >= TRACK_LENGTH do v = v - TRACK_LENGTH end
  while v < 0 do v = v + TRACK_LENGTH end
  return v
end

local function signedTrackDelta(a, b)
  local d = wrapDistance(a) - wrapDistance(b)
  if d > TRACK_LENGTH * 0.5 then d = d - TRACK_LENGTH end
  if d < -TRACK_LENGTH * 0.5 then d = d + TRACK_LENGTH end
  return d
end

local function segmentAt(distance)
  return math.floor(wrapDistance(distance) / SEGMENT_LENGTH) + 1
end

local function buildTrack()
  track = {}
  for i = 1, TRACK_SEGMENTS do
    local curve = 0.0
    local hill = 0.0
    if i >= 25 and i < 55 then curve = 0.018 end
    if i >= 70 and i < 105 then curve = -0.024 end
    if i >= 125 and i < 155 then curve = 0.032 end
    if i >= 175 and i < 205 then curve = -0.016 end
    if i >= 45 and i < 75 then hill = 0.025 end
    if i >= 75 and i < 105 then hill = -0.025 end
    if i >= 145 and i < 170 then hill = -0.020 end
    if i >= 170 and i < 195 then hill = 0.020 end
    track[i] = { curve = curve, hill = hill }
  end
end

local function resetRace()
  game.distance = 0.0
  game.lane = 0.0
  game.speed = 0.0
  game.lap = 1
  game.paused = false
  game.finished = false
  game.boost = 0.0
  game.driftCharge = 0.0
  game.rivals = {}
  for i = 1, 8 do
    game.rivals[i] = {
      distance = (i * 31.0) % TRACK_LENGTH,
      lane = ((i % 3) - 1) * 0.62,
      speed = 55.0 + i * 2.8,
      phase = i * 0.83,
    }
  end
end

local function keyboardAxis()
  local x = 0.0
  if input.is_key_down("A") or input.is_key_down("Left") then x = x - 1.0 end
  if input.is_key_down("D") or input.is_key_down("Right") then x = x + 1.0 end
  return x
end

local function controls()
  local steer = keyboardAxis()
  local throttle = input.is_key_down("W") or input.is_key_down("Up")
  local brake = input.is_key_down("S") or input.is_key_down("Down")
  local drift = input.is_key_down("Space")

  if input.is_gamepad_connected(1) then
    local padX = input.gamepad_axis(1, "left_x")
    if math.abs(padX) > math.abs(steer) then steer = padX end
    throttle = throttle or input.is_gamepad_down(1, "a")
      or input.gamepad_axis(1, "right_trigger") > 0.1
    brake = brake or input.is_gamepad_down(1, "b")
      or input.gamepad_axis(1, "left_trigger") > 0.1
    drift = drift or input.is_gamepad_down(1, "right_shoulder")
  end
  return steer, throttle, brake, drift
end

local function updateRivals(dt)
  for i, rival in ipairs(game.rivals) do
    local seg = track[segmentAt(rival.distance)]
    local target = math.sin(rival.phase + rival.distance * 0.008) * 0.72
    rival.lane = rival.lane + (target - rival.lane) * dt * 0.8
    local cornerPenalty = math.abs(seg.curve) * 240.0
    local targetSpeed = 68.0 + i * 1.6 - cornerPenalty
    rival.speed = rival.speed + (targetSpeed - rival.speed) * dt * 0.7
    rival.distance = wrapDistance(rival.distance + rival.speed * dt)
  end
end

local function resolveKartContacts()
  for _, rival in ipairs(game.rivals) do
    local dz = signedTrackDelta(rival.distance, game.distance)
    if dz > -1.0 and dz < 3.5 and math.abs(rival.lane - game.lane) < 0.32 then
      game.speed = game.speed * 0.72
      if game.lane < rival.lane then game.lane = game.lane - 0.10
      else game.lane = game.lane + 0.10 end
    end
  end
end

function Race_FixedUpdate(entity, dt)
  if game.paused or game.finished then return end
  local steer, throttle, brake, drift = controls()
  game.lastSteer = steer
  local onGrass = math.abs(game.lane) > ROAD_LIMIT

  local acceleration = throttle and 42.0 or -12.0
  if brake then acceleration = acceleration - 58.0 end
  if onGrass then acceleration = acceleration - 34.0 end
  if game.boost > 0.0 then
    acceleration = acceleration + 52.0
    game.boost = math.max(0.0, game.boost - dt)
  end
  game.speed = clamp(game.speed + acceleration * dt, 0.0,
    MAX_SPEED + (game.boost > 0.0 and 20.0 or 0.0))

  local speedRatio = game.speed / MAX_SPEED
  local steerRate = (1.8 - speedRatio * 0.65) * speedRatio
  if drift and math.abs(steer) > 0.25 and speedRatio > 0.45 then
    steerRate = steerRate * 1.45
    game.driftCharge = clamp(game.driftCharge + dt, 0.0, 1.2)
  elseif game.driftCharge > 0.35 then
    game.boost = clamp(game.driftCharge * 0.65, 0.2, 0.8)
    game.driftCharge = 0.0
  else
    game.driftCharge = 0.0
  end

  game.lane = game.lane + steer * steerRate * dt
  local current = track[segmentAt(game.distance)]
  game.lane = game.lane - current.curve * game.speed * dt * 0.45
  game.lane = clamp(game.lane, -1.55, 1.55)

  local previous = game.distance
  game.distance = wrapDistance(game.distance + game.speed * dt)
  if game.distance < previous and previous > TRACK_LENGTH * 0.8 then
    game.lap = game.lap + 1
    if game.lap > game.lapsToWin then
      game.lap = game.lapsToWin
      game.finished = true
      game.speed = 0.0
      dialog.show("Finish! Press R or gamepad Y to race again.")
    end
  end
  updateRivals(dt)
  resolveKartContacts()
end

local function roadName(i) return string.format("Road%02d", i) end
local function rivalName(i) return string.format("Rival%02d", i) end

local function renderRoad()
  local baseSegment = segmentAt(game.distance)
  local fractional = (game.distance % SEGMENT_LENGTH) / SEGMENT_LENGTH
  local curveCenter = 0.0
  local curveVelocity = 0.0
  local elevation = 0.0
  local previousCenter = -game.lane * 4.2
  local previousY = -7.0
  local previousW = 12.5

  -- Project from the player toward the horizon. Curve displacement must
  -- accumulate in this direction; doing it far-to-near makes upcoming bends
  -- incorrectly move the road directly under the kart.
  for i = 1, ROAD_COUNT do
    local nearDepth = math.max(0.0, i - 1.0 - fractional)
    local farDepth = i - fractional
    local nearT = 1.0 - clamp(nearDepth / ROAD_COUNT, 0.0, 1.0)
    local farT = 1.0 - clamp(farDepth / ROAD_COUNT, 0.0, 1.0)
    local index = ((baseSegment + i - 2) % TRACK_SEGMENTS) + 1
    local seg = track[index]

    curveVelocity = curveVelocity + seg.curve
    curveCenter = curveCenter + curveVelocity * (0.4 + farT * 1.8)
    elevation = elevation + seg.hill

    local nearY = i == 1 and previousY
      or 2.3 - nearT * nearT * 9.3 + elevation * nearT * 2.0
    local farY = 2.3 - farT * farT * 9.3 + elevation * farT * 2.0
    local nearW = i == 1 and previousW or 0.18 + nearT * nearT * 12.5
    local farW = 0.18 + farT * farT * 12.5
    local nearCenter = previousCenter
    local farCenter = curveCenter - game.lane * 4.2 * farT
    local name = roadName(i)
    sprite.set_quad(name,
      farCenter - farW, farY,
      farCenter + farW, farY,
      nearCenter + nearW, nearY,
      nearCenter - nearW, nearY)
    local stripe = index % 2
    sprite.set_uv_rect(name, 0.0, stripe * 0.5, 1.0, stripe * 0.5 + 0.5)
    sprite.set_order(name, -i)
    previousCenter = farCenter
    previousY = farY
    previousW = farW
  end
end

local function projectRival(rival)
  local dz = signedTrackDelta(rival.distance, game.distance)
  if dz <= 1.0 or dz > ROAD_COUNT * SEGMENT_LENGTH then return nil end
  local depth = dz / SEGMENT_LENGTH
  local t = 1.0 - clamp(depth / ROAD_COUNT, 0.0, 1.0)
  local y = 2.3 - t * t * 9.3
  local roadW = 0.18 + t * t * 12.5
  local x = (rival.lane - game.lane) * roadW * 0.72
  local scale = 0.08 + t * t * 0.92
  return x, y, scale, depth
end

local function renderRivals()
  local visible = {}
  for i, rival in ipairs(game.rivals) do
    local x, y, scale, depth = projectRival(rival)
    if x then table.insert(visible, {i=i, x=x, y=y, scale=scale, depth=depth}) end
  end
  table.sort(visible, function(a, b) return a.depth > b.depth end)
  for slot = 1, 8 do
    local name = rivalName(slot)
    local item = visible[slot]
    scene.set_enabled(name, item ~= nil)
    if item then
      transform.set_position(name, item.x, item.y, 0)
      transform.set_scale(name, item.scale, item.scale, 1)
      sprite.set_order(name, 20 + slot)
      sprite.set_sub_sprite(name, item.i % 2 == 0 and "Blue" or "Red")
    end
  end
end

local function setDigit(entity, number)
  number = math.floor(clamp(number, 0, 9))
  sprite.set_sub_sprite(entity, "Digit" .. tostring(number))
end

local function updateHud()
  setDigit("LapDigit", game.lap)
  local position = 1
  for _, rival in ipairs(game.rivals) do
    local ahead = signedTrackDelta(rival.distance, game.distance)
    if ahead > 0 then position = position + 1 end
  end
  setDigit("PositionDigit", position)
  local kmh = math.floor(game.speed * 3.0)
  setDigit("SpeedDigit1", math.floor(kmh / 100) % 10)
  setDigit("SpeedDigit2", math.floor(kmh / 10) % 10)
  setDigit("SpeedDigit3", kmh % 10)
end

function Race_Start(entity)
  buildTrack()
  resetRace()
  for i = 1, ROAD_COUNT do
    sprite.set_quad(roadName(i), -1, 0.1, 1, 0.1, 1, -0.1, -1, -0.1)
  end
end

function Race_Update(entity, dt)
  local pausePressed = input.is_key_pressed("Escape")
    or input.is_gamepad_pressed(1, "start")
  if pausePressed then game.paused = not game.paused end
  local restart = input.is_key_pressed("R")
    or input.is_gamepad_pressed(1, "y")
  if restart then resetRace() end

  renderRoad()
  renderRivals()
  updateHud()

  local tilt = clamp(-game.lastSteer * 8.0, -8.0, 8.0)
  transform.set_rotation("PlayerKart", tilt)
end
```

---

## 7. 중요한 코드 설명

### 7.1 트랙 데이터

트랙은 240개의 세그먼트로 구성됩니다. 각 세그먼트는 `curve`와 `hill`만
저장합니다. 값이 0이면 직선·평지이고, 양수와 음수는 서로 반대 방향입니다.
처음에는 절댓값을 0.01~0.03 사이로 유지하십시오. 너무 큰 값을 넣으면 가까운
도로 조각이 화면 밖으로 급격히 이동합니다.

### 7.2 사다리꼴 순서

`sprite.set_quad`의 인수 순서는 반드시 TL, TR, BR, BL입니다. Y축은 월드에서
위쪽이 양수이므로 먼 변의 Y가 가까운 변보다 큽니다. 가까운 변을 더 넓게 하면
원근감이 생깁니다.

### 7.3 멀리서 가까운 순서로 그리기

먼 도로는 낮은 Order, 가까운 도로는 높은 Order를 사용합니다. AI 카트도
화면상 깊이로 정렬한 다음 서로 다른 Order를 부여합니다. 이 순서가 반대면
멀리 있는 카트가 가까운 카트 위에 그려집니다.

### 7.4 고정 업데이트

가속과 AI는 `FixedUpdate`에 있으므로 60Hz, 120Hz 모니터에서도 거의 같은
결과를 냅니다. 입력의 `Pressed` 계열은 한 프레임 이벤트이므로 일시 정지와
재시작처럼 한 번만 실행할 기능은 `Update`에서 읽습니다.

---

## 8. 트랙 텍스처 만들기

`track-strip.png`의 위쪽 절반과 아래쪽 절반에 색이 약간 다른 두 띠를
배치하십시오. 스크립트는 세그먼트 번호의 홀짝에 따라 UV를 바꿉니다. 경계선이
번갈아 깜박이는 효과 덕분에 속도를 쉽게 느낄 수 있습니다.

텍스처의 중앙 50%를 도로로 만들고 양쪽을 잔디로 두면 Custom Quad 하나만으로
도로와 주변 지형을 동시에 그릴 수 있습니다. 도로 폭 판정의 `ROAD_LIMIT`과
그림의 경계 비율이 맞지 않으면 둘 중 하나를 조정합니다.

---

## 9. 카트 애니메이션 개선

기본 예제는 플레이어 카트를 회전시킵니다. 더 좋은 결과를 위해
`player-kart.png`를 스프라이트 시트로 만들고 다음 이름을 추가할 수 있습니다.

* `KartHardLeft`
* `KartLeft`
* `KartStraight`
* `KartRight`
* `KartHardRight`

조향값 구간에 따라 `sprite.set_sub_sprite("PlayerKart", name)`을 호출합니다.
바퀴 먼지용 ParticleSystem 두 개를 카트 아래에 배치하고 잔디 또는 드리프트
중에만 활성화하면 속도감이 좋아집니다.

---

## 10. 테스트 순서

1. Play를 누르고 도로가 수평선부터 화면 아래까지 끊김 없이 이어지는지 봅니다.
2. W를 누르면 띠가 가까워지는 것처럼 움직이는지 확인합니다.
3. A/D로 도로 중심이 이동하고 카트가 기울어지는지 확인합니다.
4. 잔디로 나가 속도가 빠르게 줄어드는지 확인합니다.
5. Space를 누른 채 코너를 돌고 놓았을 때 잠깐 가속되는지 확인합니다.
6. AI 카트가 멀리서는 작고 가까이서는 커지는지 확인합니다.
7. AI와 겹치면 속도가 줄어드는지 확인합니다.
8. 게임패드를 연결하고 왼쪽 스틱과 트리거를 시험합니다.
9. 세 바퀴 뒤 Finish 대화가 나타나는지 확인합니다.
10. R 또는 게임패드 Y로 초기화되는지 확인합니다.

---

## 11. 문제 해결

### 도로가 직사각형으로만 보인다

스크립트 컴포넌트의 Entry Function이 `Race`인지, Road 이름이 두 자리 숫자를
포함한 `Road01` 형식인지 확인합니다.

### 도로 가운데 대각선이 보인다

임의 사각형은 두 삼각형으로 나뉩니다. 한 조각의 높이가 너무 크면 두 삼각형의
보간 차이가 눈에 띕니다. `ROAD_COUNT`를 늘리고 각 띠를 더 얇게 만드십시오.

### 코너에서 도로가 화면 밖으로 사라진다

`buildTrack`의 curve 절댓값을 낮추거나 `renderRoad`의 curve 배율을 줄입니다.

### 게임패드가 인식되지 않는다

Windows에서는 XInput 호환 모드를 사용합니다. Android에서는 시스템이 패드를
게임 컨트롤러로 인식하는지 확인합니다. Lua에서
`print(input.is_gamepad_connected(1))`로 연결 상태를 먼저 검사하십시오.

### 속도가 모니터 주사율에 따라 달라진다

속도 변경 코드가 `Race_Update`가 아니라 `Race_FixedUpdate` 안에 있는지
확인합니다. FixedUpdate의 `dt`를 다시 임의의 프레임 dt로 바꾸지 마십시오.

---

## 12. 다음 확장 과제

* 경사에 따라 카트 Y 위치와 그림자 간격 변경
* 트랙 옆 나무·표지판용 32개 오브젝트 풀
* 아이템 상자와 투사체
* 타임 트라이얼 최고 기록 저장
* 2인 분할 화면
* 여러 트랙과 차량 선택 화면
* 터치 조향 버튼과 가속 페달
* 엔진음의 피치를 속도에 따라 변경

분할 화면을 구현할 때는 같은 트랙 상태를 두 카메라로 투영해야 합니다. 먼저
한 화면 경기의 규칙과 랩 판정을 완성한 뒤 확장하는 편이 안전합니다.

---

## 13. 빌드

**File → Build...**에서 플랫폼을 선택합니다. 키보드만으로도 항상 플레이할 수
있도록 입력 fallback을 유지하십시오.

* Windows: XInput 패드와 키보드
* Android: 터치 UI를 추가하기 전에는 연결된 패드 필요
* macOS/iPhone: GameController 호환 패드 또는 별도 터치 UI

Android에서는 화면 회전을 Landscape로 고정하고, 모바일 HUD와 터치 버튼은
화면 가장자리의 safe area 안쪽에 배치합니다.

---

## 14. 최종 체크리스트

* [ ] 도로 스트립 48개가 끊김 없이 원근 투영된다.
* [ ] 직선, 좌·우 코너와 언덕이 모두 보인다.
* [ ] 키보드와 게임패드로 가속·브레이크·조향할 수 있다.
* [ ] 물리 로직이 `FixedUpdate`에서 실행된다.
* [ ] 잔디 감속, 드리프트와 부스트가 동작한다.
* [ ] AI 카트가 주행하고 접촉 시 속도가 줄어든다.
* [ ] 순위, 속도와 랩 숫자가 갱신된다.
* [ ] 3랩 완주와 재시작이 동작한다.
* [ ] 모든 그래픽과 사운드의 사용 권한을 기록했다.

이제 NaraGame의 2D 렌더러만으로 곡선 트랙, 경쟁 차량, 랩과 순위를 갖춘
완전한 의사 3D 카트 레이싱 프로토타입을 만들 수 있습니다.
