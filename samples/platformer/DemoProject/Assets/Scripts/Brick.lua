-- Assets/Scripts/Brick.lua
-- 아래에서 머리로 치면 깨지는 벽돌.

-- "아래에서 쳤는가": 플레이어 중심이 블록 중심보다 충분히 아래
-- + 수평으로 겹쳐 있을 때만 인정. 옆에서 걸어와 부딪히거나 위에
-- 올라섰을 때는 거짓이 된다. 0.4 / 0.6 은 블록 1x1, 플레이어
-- 콜라이더 폭 ~1 기준 -- 크기를 바꿨다면 함께 조정할 것.
local function hitFromBelow(self, other)
  local bx, by = transform.get_position(self)
  local px, py = transform.get_position(other)
  return py < by - 0.4 and math.abs(px - bx) < 0.6
end

function Brick_CollisionEnter(self, other)
  if other ~= "Player" then return end
  if not hitFromBelow(self, other) then return end

  state.score = (state.score or 0) + 50
  audio.play("Sounds/break.wav", 0.9)
  -- 파편 연출 (선택): 파티클 엔티티를 하나 만들어 두면 깨질 때
  -- 그 자리에서 터뜨릴 수 있다 (10장의 JumpDust 패턴).
  if scene.find("BrickDust") then
    local bx, by = transform.get_position(self)
    transform.set_position("BrickDust", bx, by, 0)
    particle.emit("BrickDust", 12)
  end
  scene.destroy_entity(self)
end