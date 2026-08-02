local function drawNumber(prefix, value, digits)
  value = math.max(0, math.floor(value or 0))
  for i = 0, digits - 1 do
    local n = math.floor(value / 10 ^ i) % 10
    sprite.set_sub_sprite(prefix .. i, "digit_" .. n)
  end
end

function SkyHud_Update(self, dt)
  drawNumber("ScoreD", state.score or 0, 3)
  drawNumber("BestD", state.sky_hopper_best or 0, 3)
end