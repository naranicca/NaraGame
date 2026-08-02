-- Assets/Scripts/Hud.lua
-- 매 프레임 전역 state 테이블을 읽어 서브스프라이트/활성 상태로
-- 반영한다. state 는 씬이 바뀌어도 유지되므로 (11.4) 스테이지를
-- 넘어가도 점수/라이프가 이어진다.

local SCORE_DIGITS = 4   -- ScoreD0 (1의 자리) ~ ScoreD3 (1000의 자리)
local MAX_LIVES    = 3

function Hud_Update(self, dt)
  -- 점수: 자릿수별로 digit_N 서브스프라이트를 갈아 끼운다.
  local score = state.score or 0
  for i = 0, SCORE_DIGITS - 1 do
    local d = math.floor(score / 10 ^ i) % 10
    sprite.set_sub_sprite("ScoreD" .. i, "digit_" .. d)
  end

  -- 스테이지 번호
  sprite.set_sub_sprite("StageD", "digit_" .. (state.stage or 1))

  -- 라이프: 남은 개수만큼 하트를 켠다.
  local lives = state.lives or MAX_LIVES
  for i = 1, MAX_LIVES do
    scene.set_enabled("Heart" .. i, lives >= i)
  end
end