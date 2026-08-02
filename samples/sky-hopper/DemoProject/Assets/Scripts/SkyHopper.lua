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