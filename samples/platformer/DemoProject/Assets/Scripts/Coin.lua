-- Assets/Scripts/Coin.lua
-- 코인은 Player가 트리거로 접촉할 때 사라진다.
-- 접촉 감지는 Player 쪽 OnTriggerEnter로 처리해도 되고,
-- 코인 쪽에 붙여도 된다. 여기서는 코인에 붙인다.

function Coin_TriggerEnter(self, other)
  if other == "Player" then
    state.coins = (state.coins or 0) + 1
    audio.play("Sounds/coin.wav", 0.8)
    print("coins =", state.coins)
    scene.destroy_entity(self)
  end
end