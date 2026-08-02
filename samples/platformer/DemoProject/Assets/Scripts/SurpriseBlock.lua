-- Assets/Scripts/SurpriseBlock.lua
-- 선물 블록: 아래에서 치면 아이템이 나오고 빈 블록이 된다.

-- 이미 사용된 블록 기록. Lua VM은 씬을 다시 로드해도 유지되므로
-- (11.4) 씬 재시작 때 블록이 "계속 빈 블록"으로 남지 않도록
-- Start 에서 자기 항목을 지워 초기화한다.
local used = {}

function SurpriseBlock_Start(self)
  used[self] = nil
end

function SurpriseBlock_CollisionEnter(self, other)
  if other ~= "Player" then return end
  if used[self] then return end
  local bx, by = transform.get_position(self)
  local px, py = transform.get_position(other)
  if not (py < by - 0.4 and math.abs(px - bx) < 0.6) then return end

  used[self] = true
  sprite.set_sub_sprite(self, "surprise_used")
  audio.play("Sounds/coin.wav", 0.9)

  -- 숨겨 둔 아이템을 블록 바로 위에서 활성화. 아이템이 코인이면
  -- Coin.lua(11.2)가 붙어 있으므로 그대로 주울 수 있다.
  local item = self .. "_Item"
  if scene.find(item) then
    transform.set_position(item, bx, by + 1.0, 0)
    scene.set_enabled(item, true)
  end
end