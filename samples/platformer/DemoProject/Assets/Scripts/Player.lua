-- Assets/Scripts/Player.lua
-- 클래식 플랫포머 스타일 좌우 이동 + 점프.

local MOVE_SPEED     = 6.0   -- world units / sec
local JUMP_SPEED     = 12.0  -- world units / sec (점프 초기 속도)
-- 가변 점프 높이: 상승 도중에 점프 버튼을 떼면 위쪽
-- 속도를 이 값으로 잘라 낸다. 짧게 누르면 낮게 뛰고, 계속 누르고
-- 있으면 JUMP_SPEED / 중력이 정한 최대 높이까지 뛴다.
local JUMP_CUT_SPEED = 4.0
local COYOTE_TIME    = 0.08  -- 지면 이탈 직후 얼마 동안 점프 허용
local JUMP_BUFFER    = 0.08  -- 착지 직전 눌린 점프 입력 저장 시간

-- entity 이름 → { coyote=<sec>, buffer=<sec>, wasGrounded=<bool> }
-- 여러 Player 인스턴스에 대응하기 위한 per-entity 상태 저장소.
local state = {}

local function s(name)
  local t = state[name]
  if not t then
    t = { coyote = 0, buffer = 0, wasGrounded = false }
    state[name] = t
  end
  return t
end

-- Ground 감지: Player 원점에서 아래로 2 world unit 쏘고, 자기 자신
-- 콜라이더는 exclude 로 걸러낸다. 2 unit 이면 대부분의 캐릭터
-- 콜라이더 (원점 아래 최대 1.5 unit 정도) 를 통과하고도 지면
-- tile 까지 닿을 여유. Ray 길이가 콜라이더 반높이보다 짧으면
-- 지면에 못 닿아 항상 not grounded 로 나오므로 주의.
local function isGrounded(self, py)
  local px, _, _ = transform.get_position(self)
  local hit = scene.raycast(px, py, px, py - 2.0, self)
  return hit ~= ""
end

function Player_Start(self)
  -- 액션 하나에 여러 키를 걸고 싶으면 두 번째 인자를 문자열 대신
  -- 배열 (테이블) 로 넘기면 됩니다. 아래는 A/D + 방향키를 모두
  -- 받도록 한 예시.
  input.bind("left",  {"a", "Left"})
  input.bind("right", {"d", "Right"})
  input.bind("jump",  {"Space", "w", "Up"})
  print("Player_Start: ", self)
end

function Player_Update(self, dt)
  local st = s(self)
  local vx, vy = rigidbody.get_velocity(self)
  local px, py, _ = transform.get_position(self)

  -- 좌우 입력
  local move = 0
  if input.is_action_down("left")  then move = move - 1 end
  if input.is_action_down("right") then move = move + 1 end
  vx = move * MOVE_SPEED

  -- 점프 버퍼: 공중이어도 눌러 두면 착지 시 자동 점프
  if input.is_action_pressed("jump") then
    st.buffer = JUMP_BUFFER
  else
    st.buffer = math.max(0, st.buffer - dt)
  end

  -- Coyote time: 지면 이탈 직후 잠깐 점프 가능
  local grounded = isGrounded(self, py)
  if grounded then
    st.coyote = COYOTE_TIME
  else
    st.coyote = math.max(0, st.coyote - dt)
  end

  if st.buffer > 0 and st.coyote > 0 then
    vy = JUMP_SPEED
    st.buffer = 0
    st.coyote = 0
    audio.play("Sounds/jump.wav", 0.8)
    animator.set_trigger(self, "Jump")
    -- 점프 먼지 (섹션 10에서 JumpDust 엔티티 준비 후 활성화)
    if scene.find("JumpDust") then
      transform.set_position("JumpDust", px, py - 0.25, 0)
      particle.emit("JumpDust", 10)
    end
  end

  -- 가변 점프 높이: 아직 상승 중일 때 점프 버튼을 떼면 위쪽 속도를
  -- JUMP_CUT_SPEED 이하로 잘라 낸다. is_action_released 는 버튼이
  -- 눌렸다가 떨어진 프레임에 딱 한 번 true 를 반환하므로 상승 도중
  -- 여러 번 발동하지 않는다.
  if input.is_action_released("jump") and vy > JUMP_CUT_SPEED then
    vy = JUMP_CUT_SPEED
  end

  rigidbody.set_velocity(self, vx, vy)

  -- 좌우 반전
  if move ~= 0 then
    sprite.set_flip_x(self, move < 0)
  end

  -- Animator 파라미터 업데이트
  animator.set_float(self, "speed", math.abs(vx))
  animator.set_bool(self, "grounded", grounded)

  -- 낙사 판정 (섹션 14): y가 너무 낮으면 씬 재시작
  if py < -10 then
    scene.load("Assets/Scenes/Level1.nscene",
      { fade_out = 0.4, fade_in = 0.4, color = { r=0, g=0, b=0 } })
  end

  st.wasGrounded = grounded
end