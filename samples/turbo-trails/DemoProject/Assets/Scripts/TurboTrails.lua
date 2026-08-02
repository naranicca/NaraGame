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