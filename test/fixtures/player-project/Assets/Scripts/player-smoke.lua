function Test_Enable(entity)
  transform.set_position(entity, 1, 2, 0)
end

function Test_Start(entity)
  input.bind("smoke", {"Space", "A"})
end

function Test_Update(entity, dt)
  local x, y, z = transform.get_position(entity)
  transform.set_position(entity, x + dt, y, z)
  local _ = time.elapsed() + time.delta() + time.fps()
end

function Test_Disable(entity) end
function Test_Destroy(entity) end
