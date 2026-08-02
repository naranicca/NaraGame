-- Assets/Scripts/Platform.lua
-- 시작 지점에서 (TRAVEL_X, TRAVEL_Y)만큼 떨어진 지점까지
-- 부드럽게 왕복하는 발판. 올라탄 플레이어는 이동량만큼
-- 같이 움직인다 (캐리).

local TRAVEL_X = 4.0   -- 왕복 이동량 (world unit). 수직 발판이면
local TRAVEL_Y = 0.0   -- X를 0으로, Y에 거리를 넣는다.
local PERIOD   = 4.0   -- 한 번 갔다 돌아오는 데 걸리는 시간 (초)

local state = {}
local function s(name)
  local t = state[name]
  if not t then
    local x, y = transform.get_position(name)
    t = { homeX = x, homeY = y, phase = 0 }
    state[name] = t
  end
  return t
end

function Platform_Update(self, dt)
  local st = s(self)
  local x, y = transform.get_position(self)

  -- 사인 왕복: phase 0 -> 1 이 한 사이클. cos 곡선을 쓰면 양 끝에서
  -- 속도가 0으로 줄어 부드럽게 방향을 바꾼다 (등속 왕복보다 자연스럽다).
  st.phase = (st.phase + dt / PERIOD) % 1.0
  local k = 0.5 - 0.5 * math.cos(st.phase * 2.0 * math.pi)
  local nx = st.homeX + TRAVEL_X * k
  local ny = st.homeY + TRAVEL_Y * k
  local dx = nx - x
  local dy = ny - y
  transform.set_position(self, nx, ny, 0)

  -- 캐리: 플레이어의 발밑 raycast가 "이 플랫폼"을 맞추고 있고
  -- 상승 중이 아니면 (점프 순간 제외) 올라탄 것으로 보고
  -- 이동량만큼 같이 옮긴다. raycast를 쓰면 콜라이더 크기를
  -- 상수로 다시 적을 필요가 없다 (8장의 isGrounded와 같은 원리).
  if scene.find("Player") then
    local px, py = transform.get_position("Player")
    local _, pvy = rigidbody.get_velocity("Player")
    if pvy <= 0.1 and
       scene.raycast(px, py, px, py - 2.0, "Player") == self then
      transform.translate("Player", dx, dy, 0)
    end
  end
end