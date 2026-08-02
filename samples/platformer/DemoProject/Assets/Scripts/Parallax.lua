-- Assets/Scripts/Parallax.lua
-- 배경 엔티티의 X 위치를 카메라 대비 factor 만큼 이동시킨다.
-- factor = 0.0 이면 카메라와 함께 움직임 → 완전 고정된 하늘.
-- factor = 1.0 이면 지면과 동일 속도 → 전경에 가까움.
-- factor = 0.2 (하늘), 0.5 (산맥), 0.8 (나무) 정도가 자연스러움.

-- Custom LuaComponent 등록: 인스펙터에 편집 가능한 필드로 노출됨.
component.register("Parallax", {
  factor = 0.5,
  origin_x = 0.0,   -- 카메라가 (0,0)일 때 배경이 있어야 할 x
  origin_y = 3.0
})

function Parallax_Update(self, dt)
  local cam = camera.get_active()
  if cam == "" then return end
  local cx, cy, _ = transform.get_position(cam)

  local p = component.get(self, "Parallax")
  if not p then return end

  -- 카메라가 원점에서 dx 만큼 이동했으면 배경은 dx * factor 만 이동.
  local x = p.origin_x + cx * p.factor
  local y = p.origin_y + cy * p.factor
  transform.set_position(self, x, y, 0)
end