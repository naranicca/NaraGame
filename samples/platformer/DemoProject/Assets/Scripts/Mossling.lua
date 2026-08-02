-- Assets/Scripts/Mossling.lua
-- 순찰하다가 플레이어를 발견하면 추적한다.
-- 공격은 접촉 데미지 방식 -- 몸이 닿으면 Player 쪽
-- CollisionEnter 에서 피해 처리를 한다 (18.3).

local PATROL_SPEED  = 1.5
local CHASE_SPEED   = 3.5
local PATROL_RANGE  = 3.0   -- 시작 위치에서 좌우로 왕복하는 거리
local DETECT_RANGE  = 6.0   -- 이 거리 안이면 발견 후보
local DETECT_HEIGHT = 1.5   -- 위아래 높이 차가 이 이내일 때만
local FRAME_TIME    = 0.25  -- 걷기 flipbook 프레임 간격 (초)

-- Mossling이 여러 마리여도 스크립트 파일은 하나 -- 엔티티 이름을
-- 키로 하는 테이블에 마리별 상태를 저장한다 (Player.lua와 동일한
-- 패턴).
local state = {}
local function s(name)
  local t = state[name]
  if not t then
    local x, _ = transform.get_position(name)
    t = { homeX = x, dir = -1, animT = 0, frame = 0, chasing = false }
    state[name] = t
  end
  return t
end

-- 시야 판정. 거리/높이로 먼저 거른 뒤 마지막에 시선 raycast:
-- 적 위치에서 플레이어 위치로 광선을 쏘고 (자기 자신은 exclude),
-- "가장 먼저 맞은 것"이 Player일 때만 보이는 것으로 친다.
-- 중간에 지면 타일이나 다른 콜라이더가 있으면 그쪽이 먼저 맞아
-- false가 되므로, 자연스럽게 "벽 뒤는 안 보이는" 시야가 된다.
local function canSeePlayer(self, ex, ey, px, py)
  if math.abs(px - ex) > DETECT_RANGE  then return false end
  if math.abs(py - ey) > DETECT_HEIGHT then return false end
  return scene.raycast(ex, ey, px, py, self) == "Player"
end

function Mossling_Update(self, dt)
  local st = s(self)
  local ex, ey = transform.get_position(self)
  local vx, vy = rigidbody.get_velocity(self)

  -- 플레이어 위치 (죽거나 없으면 nil)
  local px, py
  if scene.find("Player") then
    px, py = transform.get_position("Player")
  end

  st.chasing = (px ~= nil) and canSeePlayer(self, ex, ey, px, py)

  if st.chasing then
    -- 추적: 플레이어 쪽으로 빠르게 다가간다.
    st.dir = (px > ex) and 1 or -1
    vx = st.dir * CHASE_SPEED
  else
    -- 순찰: homeX +- PATROL_RANGE 사이를 왕복.
    if ex > st.homeX + PATROL_RANGE then st.dir = -1 end
    if ex < st.homeX - PATROL_RANGE then st.dir =  1 end
    vx = st.dir * PATROL_SPEED
  end

  -- 낭떠러지 검사: 진행 방향 반 유닛 앞의 발밑으로 짧은 광선을
  -- 쏘아 지면이 없으면, 순찰 중엔 돌아서고 추적 중엔 멈춘다
  -- (추적하다 절벽으로 뛰어내리지 않게).
  local aheadX = ex + st.dir * 0.5
  if scene.raycast(aheadX, ey, aheadX, ey - 1.5, self) == "" then
    if st.chasing then
      vx = 0
    else
      st.dir = -st.dir
      vx = st.dir * PATROL_SPEED
    end
  end

  rigidbody.set_velocity(self, vx, vy)
  sprite.set_flip_x(self, st.dir < 0)

  -- 2프레임 걷기 flipbook. Animator Controller 없이 스크립트로
  -- 서브스프라이트를 교대시키는 가장 가벼운 방법이다.
  st.animT = st.animT + dt
  if st.animT >= FRAME_TIME then
    st.animT = st.animT - FRAME_TIME
    st.frame = 1 - st.frame
    sprite.set_sub_sprite(self, "walk_" .. st.frame)
  end
end