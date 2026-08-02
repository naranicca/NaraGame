-- Assets/Scripts/Camera.lua
-- MainCamera가 Player를 부드럽게 따라가게 한다.
-- LERP factor로 지연 팔로우, Y 축은 지면 근처에서 살짝 위에 유지.
-- STAGE_* 로 카메라가 스테이지 밖 (검은 배경 / 낙사 영역) 을 보여
-- 주지 못하도록 목표 위치를 클램프한다.

local TARGET      = "Player"
local FOLLOW_LERP = 8.0    -- 초당 접근 비율 (크면 즉시, 작으면 지연)
local Y_OFFSET    = 1.5

-- 스테이지의 world-unit 경계. 자기 레벨 크기에 맞춰 조정.
local STAGE_MIN_X = -18.0
local STAGE_MAX_X =  40.0
local STAGE_MIN_Y = -6.0
local STAGE_MAX_Y =  20.0
-- 카메라가 보여 주는 반너비 / 반높이 (world unit).
-- 계산: (viewport pixel size / 2) / Camera.zoom (px per unit).
-- 예) 960×540 창 + zoom 48 → half_w = 480/48 = 10, half_h ≈ 5.6.
-- 창 크기나 카메라 zoom 을 바꾸면 여기도 같이 조정.
local VIEW_HALF_W = 10.0
local VIEW_HALF_H = 5.6

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

function Camera_Update(self, dt)
  if not scene.find(TARGET) then return end
  local tx, ty, _ = transform.get_position(TARGET)
  local cx, cy, _ = transform.get_position(self)

  -- 이번 프레임의 원하는 카메라 위치 (Player 기준).
  local targetX = tx
  local targetY = ty + Y_OFFSET

  -- 스테이지 안쪽 (뷰포트 반너비만큼 안으로 밀어 준 유효 영역) 에
  -- 목표를 클램프. 스테이지가 뷰보다 좁으면 lo > hi 가 되어 clamp
  -- 가 항상 lo 를 반환 → 카메라가 스테이지 왼쪽 / 아래쪽 안쪽
  -- 경계에 붙어 고정된다.
  targetX = clamp(targetX, STAGE_MIN_X + VIEW_HALF_W,
                            STAGE_MAX_X - VIEW_HALF_W)
  targetY = clamp(targetY, STAGE_MIN_Y + VIEW_HALF_H,
                            STAGE_MAX_Y - VIEW_HALF_H)

  local t = math.clamp(FOLLOW_LERP * dt, 0, 1)
  local nx = math.lerp(cx, targetX, t)
  local ny = math.lerp(cy, targetY, t)
  transform.set_position(self, nx, ny, 0)
end