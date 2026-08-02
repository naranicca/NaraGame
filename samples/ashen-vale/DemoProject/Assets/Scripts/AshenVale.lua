-- Assets/Scripts/AshenVale.lua

local SAVE_SLOT = "slot1"
local FLOOR_MAX, WALL_MAX = 160, 100
local ENEMY_MAX, LOOT_MAX = 16, 12
local MOVE_SPEED = 4.2

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function distance(ax, ay, bx, by)
  local dx, dy = bx - ax, by - ay
  return math.sqrt(dx * dx + dy * dy)
end

local function iso(tx, ty)
  return (tx - ty), (tx + ty) * 0.5
end

local function placeIso(entity, tx, ty, height)
  local x, y = iso(tx, ty)
  transform.set_position(entity, x, y + (height or 0), 0)
  sprite.set_order(entity, math.floor((tx + ty) * 10))
end

local function setPool(prefix, count, enabled)
  for i = 1, count do
    local digits = (prefix == "Floor" or prefix == "Wall") and "%03d" or "%02d"
    scene.set_enabled(prefix .. string.format(digits, i), enabled)
  end
end

local function newGameState()
  state = {
    schema = 1,
    location = "town",
    hp = 100, maxHp = 100,
    mp = 50, maxMp = 50,
    gold = 0,
    level = 1,
    xp = 0,
    xpNext = 25,
    skillPoints = 1,
    skills = { might = 0, focus = 0, vigor = 0 },
    inventory = {
      { id = "small_potion", name = "Small Potion", kind = "potion", power = 35 }
    },
    equipped = { weapon = nil, armor = nil },
    quest = { id = "shadows_at_gate", stage = "none", kills = 0, target = 5 },
    dungeonSeed = 1001,
    dungeonVisits = 0
  }
end

local function ensureState()
  if state.schema == nil then newGameState() end
  state.skills = state.skills or { might = 0, focus = 0, vigor = 0 }
  state.inventory = state.inventory or {}
  state.equipped = state.equipped or { weapon = nil, armor = nil }
  state.quest = state.quest or
    { id = "shadows_at_gate", stage = "none", kills = 0, target = 5 }
end

local function saveNow()
  return savegame.save(SAVE_SLOT)
end

local function bindControls()
  input.bind("move_up", "W")
  input.bind("move_down", "S")
  input.bind("move_left", "A")
  input.bind("move_right", "D")
  input.bind("attack", "Space")
  input.bind("spell", "Q")
  input.bind("interact", "E")
  input.bind("inventory", "I")
  input.bind("skills", "K")
end

local function movement(dt)
  local dx, dy = 0, 0
  if input.is_action_down("move_left") then dx = dx - 1 end
  if input.is_action_down("move_right") then dx = dx + 1 end
  if input.is_action_down("move_up") then dy = dy - 1 end
  if input.is_action_down("move_down") then dy = dy + 1 end
  local len = math.sqrt(dx * dx + dy * dy)
  if len > 0 then
    dx, dy = dx / len, dy / len
  end
  return dx * MOVE_SPEED * dt, dy * MOVE_SPEED * dt
end

local titleMenu = { index = 1, ys = { 1.0, 0.0, -1.0 } }

local function updateTitleCursor()
  transform.set_position("MenuCursor", -2.2, titleMenu.ys[titleMenu.index], 0)
  local canLoad = savegame.exists(SAVE_SLOT)
  sprite.set_color("LoadGameLabel",
    canLoad and 1.0 or 0.35,
    canLoad and 1.0 or 0.35,
    canLoad and 1.0 or 0.35, 1.0)
  if scene.find("LoadUnavailable") then
    scene.set_enabled("LoadUnavailable", not canLoad)
  end
end

function Title_Start(entity)
  bindControls()
  titleMenu.index = 1
  updateTitleCursor()
end

function Title_Update(entity, dt)
  if input.is_key_pressed("Up") or input.is_key_pressed("W") then
    titleMenu.index = titleMenu.index - 1
    if titleMenu.index < 1 then titleMenu.index = 3 end
    updateTitleCursor()
  elseif input.is_key_pressed("Down") or input.is_key_pressed("S") then
    titleMenu.index = titleMenu.index + 1
    if titleMenu.index > 3 then titleMenu.index = 1 end
    updateTitleCursor()
  end

  if not input.is_key_pressed("Enter") then return end

  if titleMenu.index == 1 then
    newGameState()
    saveNow()
    scene.load("Scenes/Town.nscene")
  elseif titleMenu.index == 2 then
    if savegame.load(SAVE_SLOT) then
      ensureState()
      if state.location == "dungeon" then
        scene.load("Scenes/Dungeon.nscene")
      else
        scene.load("Scenes/Town.nscene")
      end
    else
      dialog.show("No save file was found. Choose New Game first.")
    end
  else
    app.quit()
  end
end

local function updateHud()
  local hpRatio = clamp(state.hp / state.maxHp, 0, 1)
  local mpRatio = clamp(state.mp / state.maxMp, 0, 1)
  sprite.set_size("HpFill", 3.6 * hpRatio, 0.28)
  sprite.set_size("MpFill", 3.6 * mpRatio, 0.28)
end

local uiMode = "none"

local function itemLabel(item)
  if item.kind == "weapon" then
    return item.name .. "  [Weapon +" .. item.power .. "]"
  elseif item.kind == "armor" then
    return item.name .. "  [Armor +" .. item.power .. "]"
  end
  return item.name .. "  [Heal " .. item.power .. "]"
end

local function openInventory()
  if #state.inventory == 0 then
    dialog.show("Inventory is empty.")
    uiMode = "message"
    return
  end
  local choices = {}
  for i, item in ipairs(state.inventory) do
    choices[#choices + 1] = itemLabel(item)
  end
  choices[#choices + 1] = "Close"
  dialog.choices("Inventory  (" .. #state.inventory .. "/12)", choices)
  uiMode = "inventory"
end

local function resolveInventoryChoice()
  if uiMode ~= "inventory" then return false end
  local pick = dialog.last_choice()
  if pick <= 0 then return true end
  if pick > #state.inventory then
    dialog.hide()
    uiMode = "none"
    return true
  end

  local item = state.inventory[pick]
  if item.kind == "potion" then
    state.hp = math.min(state.maxHp, state.hp + item.power)
    table.remove(state.inventory, pick)
  elseif item.kind == "weapon" then
    local old = state.equipped.weapon
    state.equipped.weapon = item
    table.remove(state.inventory, pick)
    if old then state.inventory[#state.inventory + 1] = old end
  elseif item.kind == "armor" then
    local old = state.equipped.armor
    state.equipped.armor = item
    table.remove(state.inventory, pick)
    if old then state.inventory[#state.inventory + 1] = old end
  end
  dialog.hide()
  uiMode = "none"
  updateHud()
  saveNow()
  return true
end

local function openSkillTree()
  local s = state.skills
  dialog.choices("Skill Tree  |  Points: " .. state.skillPoints, {
    "Might  " .. s.might .. "/3  (+attack)",
    "Focus  " .. s.focus .. "/3  (+mana, spell)",
    "Vigor  " .. s.vigor .. "/3  (+health, defense)",
    "Close"
  })
  uiMode = "skills"
end

local function resolveSkillChoice()
  if uiMode ~= "skills" then return false end
  local pick = dialog.last_choice()
  if pick <= 0 then return true end
  if pick == 4 or state.skillPoints <= 0 then
    dialog.hide(); uiMode = "none"; return true
  end
  local keys = { "might", "focus", "vigor" }
  local key = keys[pick]
  if state.skills[key] < 3 then
    state.skills[key] = state.skills[key] + 1
    state.skillPoints = state.skillPoints - 1
    if key == "focus" then
      state.maxMp = state.maxMp + 10
      state.mp = state.maxMp
    elseif key == "vigor" then
      state.maxHp = state.maxHp + 15
      state.hp = state.maxHp
    end
    saveNow()
  end
  dialog.hide(); uiMode = "none"
  updateHud()
  return true
end

local function updateCommonUi()
  if resolveInventoryChoice() or resolveSkillChoice() then return true end
  if dialog.is_visible() then return true end
  if input.is_action_pressed("inventory") then openInventory(); return true end
  if input.is_action_pressed("skills") then openSkillTree(); return true end
  return false
end

local town = { px = 8, py = 12, dialog = "none" }

local function townBlocked(x, y)
  if x < 1 or y < 1 or x > 16 or y > 16 then return true end
  -- 대장간 건물 영역 예시
  if x >= 9 and x <= 12 and y >= 9 and y <= 11 then return true end
  return false
end

local function moveTownPlayer(dt)
  local dx, dy = movement(dt)
  local nx = town.px + dx
  local ny = town.py + dy
  if not townBlocked(nx, town.py) then town.px = nx end
  if not townBlocked(town.px, ny) then town.py = ny end
  placeIso("Player", town.px, town.py, 0.35)
  local x, y = iso(town.px, town.py)
  transform.set_position("TownCamera", x, y, 0)
end

local function talkToWarden()
  local q = state.quest
  if q.stage == "none" then
    dialog.choices("Warden: Creatures gather beyond the gate. Will you help?", {
      "Accept: defeat 5 creatures", "Not yet"
    })
    town.dialog = "offer"
  elseif q.stage == "active" then
    dialog.show("Warden: You have defeated " .. q.kills .. "/" .. q.target .. ".")
    town.dialog = "message"
  elseif q.stage == "ready" then
    dialog.choices("Warden: The road is safe again. Take this training seal.", {
      "Complete quest"
    })
    town.dialog = "complete"
  else
    dialog.show("Warden: The valley remembers your courage.")
    town.dialog = "message"
  end
end

local function updateTownDialog()
  if town.dialog == "none" then return false end
  local pick = dialog.last_choice()
  if pick <= 0 then return true end
  if town.dialog == "offer" and pick == 1 then
    state.quest.stage = "active"
    state.quest.kills = 0
    audio.play("Sounds/quest.wav", 0.8)
    saveNow()
  elseif town.dialog == "complete" then
    state.quest.stage = "done"
    state.gold = state.gold + 100
    state.skillPoints = state.skillPoints + 1
    audio.play("Sounds/quest.wav", 0.8)
    saveNow()
  end
  dialog.hide()
  town.dialog = "none"
  return true
end

function Town_Start(entity)
  ensureState()
  bindControls()
  state.location = "town"
  town.px, town.py = 8, 12
  town.dialog = "none"
  placeIso("WardenNPC", 6, 6, 0.35)
  placeIso("TownPortal", 13, 5, 0.1)
  placeIso("Player", town.px, town.py, 0.35)
  updateHud()
  audio.stop_music()
  audio.play_music("Sounds/town.wav", 0.45, true)
  saveNow()
end

function Town_Update(entity, dt)
  if updateTownDialog() then return end
  if updateCommonUi() then return end
  moveTownPlayer(dt)

  if input.is_action_pressed("interact") then
    if distance(town.px, town.py, 6, 6) < 1.5 then
      talkToWarden()
      return
    end
    if distance(town.px, town.py, 13, 5) < 1.4 then
      state.location = "dungeon"
      state.dungeonVisits = state.dungeonVisits + 1
      state.dungeonSeed = state.dungeonSeed + 7919
      saveNow()
      audio.play("Sounds/portal.wav", 0.8)
      scene.load("Scenes/Dungeon.nscene")
    end
  end
end

local dungeon = {
  w = 24, h = 24, grid = {},
  px = 2, py = 2,
  exitX = 2, exitY = 2,
  enemies = {}, loot = {},
  attackTimer = 0, spellTimer = 0,
  hurtTimer = 0, regenTimer = 0
}

local function cellIndex(x, y)
  return y * dungeon.w + x + 1
end

local function getCell(x, y)
  if x < 0 or y < 0 or x >= dungeon.w or y >= dungeon.h then return 0 end
  return dungeon.grid[cellIndex(x, y)] or 0
end

local function setCell(x, y, value)
  if x >= 0 and y >= 0 and x < dungeon.w and y < dungeon.h then
    dungeon.grid[cellIndex(x, y)] = value
  end
end

local function roomsOverlap(a, b)
  return a.x - 1 <= b.x + b.w and b.x - 1 <= a.x + a.w and
         a.y - 1 <= b.y + b.h and b.y - 1 <= a.y + a.h
end

local function carveRoom(room)
  for y = room.y, room.y + room.h - 1 do
    for x = room.x, room.x + room.w - 1 do setCell(x, y, 1) end
  end
end

local function carveCorridor(ax, ay, bx, by)
  local x, y = ax, ay
  while x ~= bx do
    setCell(x, y, 1); setCell(x, y + 1, 1)
    x = x + ((bx > x) and 1 or -1)
  end
  while y ~= by do
    setCell(x, y, 1); setCell(x + 1, y, 1)
    y = y + ((by > y) and 1 or -1)
  end
  setCell(bx, by, 1)
end

local function generateDungeon(seed)
  math.randomseed(seed)
  dungeon.grid = {}
  for i = 1, dungeon.w * dungeon.h do dungeon.grid[i] = 0 end

  local rooms = {}
  for attempt = 1, 70 do
    local room = {
      w = math.random(4, 7), h = math.random(4, 7),
      x = math.random(2, dungeon.w - 9),
      y = math.random(2, dungeon.h - 9)
    }
    local blocked = false
    for _, old in ipairs(rooms) do
      if roomsOverlap(room, old) then blocked = true; break end
    end
    if not blocked then
      rooms[#rooms + 1] = room
      carveRoom(room)
      if #rooms > 1 then
        local a, b = rooms[#rooms - 1], room
        carveCorridor(a.x + math.floor(a.w / 2),
                      a.y + math.floor(a.h / 2),
                      b.x + math.floor(b.w / 2),
                      b.y + math.floor(b.h / 2))
      end
      if #rooms >= 6 then break end
    end
  end

  if #rooms == 0 then
    rooms[1] = { x = 8, y = 8, w = 6, h = 6 }
    carveRoom(rooms[1])
  end

  -- 바닥에 인접한 빈 칸을 벽으로 바꾼다.
  local walls = {}
  for y = 1, dungeon.h - 2 do
    for x = 1, dungeon.w - 2 do
      if getCell(x, y) == 0 then
        local near = false
        for oy = -1, 1 do
          for ox = -1, 1 do
            if getCell(x + ox, y + oy) == 1 then near = true end
          end
        end
        if near then walls[#walls + 1] = { x = x, y = y } end
      end
    end
  end
  for _, p in ipairs(walls) do setCell(p.x, p.y, 2) end

  local first, last = rooms[1], rooms[#rooms]
  dungeon.px = first.x + math.floor(first.w / 2)
  dungeon.py = first.y + math.floor(first.h / 2)
  dungeon.exitX = last.x + math.floor(last.w / 2)
  dungeon.exitY = last.y + math.floor(last.h / 2)
  return rooms
end

local function renderDungeonGrid()
  setPool("Floor", FLOOR_MAX, false)
  setPool("Wall", WALL_MAX, false)
  local floorUsed, wallUsed = 0, 0

  for y = 0, dungeon.h - 1 do
    for x = 0, dungeon.w - 1 do
      local cell = getCell(x, y)
      if cell == 1 and floorUsed < FLOOR_MAX then
        floorUsed = floorUsed + 1
        local name = "Floor" .. string.format("%03d", floorUsed)
        scene.set_enabled(name, true)
        sprite.set_sub_sprite(name, "dungeon_floor")
        placeIso(name, x, y, 0)
        sprite.set_order(name, math.floor((x + y) * 10) - 5)
      elseif cell == 2 and wallUsed < WALL_MAX then
        wallUsed = wallUsed + 1
        local name = "Wall" .. string.format("%03d", wallUsed)
        scene.set_enabled(name, true)
        sprite.set_sub_sprite(name, "dungeon_wall")
        placeIso(name, x, y, 0.45)
      end
    end
  end

  if floorUsed == FLOOR_MAX then
    print("Floor pool exhausted: add more Floor entities")
  end
  if wallUsed == WALL_MAX then
    print("Wall pool exhausted: add more Wall entities")
  end
  placeIso("DungeonExit", dungeon.exitX, dungeon.exitY, 0.1)
end

local function floorCellsFarFromPlayer()
  local cells = {}
  for y = 1, dungeon.h - 2 do
    for x = 1, dungeon.w - 2 do
      if getCell(x, y) == 1 and
         distance(x, y, dungeon.px, dungeon.py) > 5 then
        cells[#cells + 1] = { x = x + 0.5, y = y + 0.5 }
      end
    end
  end
  return cells
end

local function spawnEnemies()
  setPool("Enemy", ENEMY_MAX, false)
  dungeon.enemies = {}
  local candidates = floorCellsFarFromPlayer()
  for i = 1, math.min(ENEMY_MAX, 8 + state.dungeonVisits) do
    if #candidates == 0 then break end
    local index = math.random(1, #candidates)
    local p = table.remove(candidates, index)
    local strong = (i % 5 == 0)
    local e = {
      name = "Enemy" .. string.format("%02d", i),
      x = p.x, y = p.y,
      hp = strong and 70 or 35,
      maxHp = strong and 70 or 35,
      damage = strong and 12 or 7,
      speed = strong and 1.3 or 1.8,
      active = true
    }
    dungeon.enemies[#dungeon.enemies + 1] = e
    scene.set_enabled(e.name, true)
    sprite.set_sub_sprite(e.name, strong and "brute" or "crawler")
    sprite.set_size(e.name, strong and 1.05 or 0.8,
                             strong and 1.5 or 1.2)
    placeIso(e.name, e.x, e.y, 0.35)
  end
end

local function weaponPower()
  return state.equipped.weapon and state.equipped.weapon.power or 0
end

local function armorPower()
  return state.equipped.armor and state.equipped.armor.power or 0
end

local function attackDamage()
  return 10 + state.level * 2 + state.skills.might * 3 + weaponPower()
end

local function spellDamage()
  return 18 + state.level * 3 + state.skills.focus * 4
end

local function hurtPlayer(rawDamage)
  if dungeon.hurtTimer > 0 then return end
  local reduction = armorPower() + state.skills.vigor
  local damage = math.max(1, rawDamage - reduction)
  state.hp = math.max(0, state.hp - damage)
  dungeon.hurtTimer = 0.65
  audio.play("Sounds/hurt.wav", 0.8)
  updateHud()
end

local function addXp(amount)
  state.xp = state.xp + amount
  while state.xp >= state.xpNext do
    state.xp = state.xp - state.xpNext
    state.level = state.level + 1
    state.xpNext = math.floor(state.xpNext * 1.35)
    state.skillPoints = state.skillPoints + 1
    state.hp, state.mp = state.maxHp, state.maxMp
  end
end

local function makeDrop(enemy)
  if #dungeon.loot >= LOOT_MAX then return end
  local roll = math.random(1, 100)
  if roll > 65 then return end
  local item
  if roll <= 25 then
    item = { id = "small_potion", name = "Small Potion",
             kind = "potion", power = 35 }
  elseif roll <= 48 then
    item = { id = "ash_blade", name = "Ash Blade",
             kind = "weapon", power = math.random(2, 6) }
  else
    item = { id = "hide_coat", name = "Hide Coat",
             kind = "armor", power = math.random(1, 4) }
  end
  local index = #dungeon.loot + 1
  local drop = { name = "Loot" .. string.format("%02d", index),
                 x = enemy.x, y = enemy.y, item = item, active = true }
  dungeon.loot[index] = drop
  scene.set_enabled(drop.name, true)
  placeIso(drop.name, drop.x, drop.y, 0.18)
end

local function killEnemy(enemy)
  enemy.active = false
  scene.set_enabled(enemy.name, false)
  makeDrop(enemy)
  addXp(10)
  if state.quest.stage == "active" then
    state.quest.kills = state.quest.kills + 1
    if state.quest.kills >= state.quest.target then
      state.quest.stage = "ready"
    end
  end
end

local function nearestEnemy(maxRange)
  local best, bestDistance = nil, maxRange
  for _, e in ipairs(dungeon.enemies) do
    if e.active then
      local d = distance(dungeon.px, dungeon.py, e.x, e.y)
      if d < bestDistance then best, bestDistance = e, d end
    end
  end
  return best
end

local function playerAttack()
  if dungeon.attackTimer > 0 then return end
  local target = nearestEnemy(1.65)
  if not target then return end
  target.hp = target.hp - attackDamage()
  dungeon.attackTimer = math.max(0.25, 0.55 - state.skills.might * 0.05)
  audio.play("Sounds/sword.wav", 0.7)
  if target.hp <= 0 then killEnemy(target) end
end

local function playerSpell()
  local manaCost = 15
  if dungeon.spellTimer > 0 or state.mp < manaCost then return end
  state.mp = state.mp - manaCost
  dungeon.spellTimer = math.max(0.8, 1.8 - state.skills.focus * 0.2)
  audio.play("Sounds/spell.wav", 0.8)
  for _, e in ipairs(dungeon.enemies) do
    if e.active and distance(dungeon.px, dungeon.py, e.x, e.y) < 2.7 then
      e.hp = e.hp - spellDamage()
      if e.hp <= 0 then killEnemy(e) end
    end
  end
  updateHud()
end

local function dungeonBlocked(x, y)
  return getCell(math.floor(x), math.floor(y)) ~= 1
end

local function updateEnemies(dt)
  for _, e in ipairs(dungeon.enemies) do
    if e.active then
      local dx, dy = dungeon.px - e.x, dungeon.py - e.y
      local len = math.sqrt(dx * dx + dy * dy)
      if len < 7 and len > 0.05 then
        dx, dy = dx / len, dy / len
        local nx, ny = e.x + dx * e.speed * dt, e.y + dy * e.speed * dt
        if not dungeonBlocked(nx, e.y) then e.x = nx end
        if not dungeonBlocked(e.x, ny) then e.y = ny end
      end
      if len < 0.75 then hurtPlayer(e.damage) end
      placeIso(e.name, e.x, e.y, 0.35)
    end
  end
end

local function updateLoot()
  for _, drop in ipairs(dungeon.loot) do
    if drop.active and distance(dungeon.px, dungeon.py, drop.x, drop.y) < 0.8 then
      if #state.inventory < 12 then
        state.inventory[#state.inventory + 1] = drop.item
        drop.active = false
        scene.set_enabled(drop.name, false)
        saveNow()
      end
    end
  end
end

local function returnToTownAfterDeath()
  state.hp = math.max(1, math.floor(state.maxHp * 0.5))
  state.mp = math.floor(state.maxMp * 0.5)
  state.gold = math.floor(state.gold * 0.9)
  state.location = "town"
  saveNow()
  scene.load("Scenes/Town.nscene")
end

local function moveDungeonPlayer(dt)
  local dx, dy = movement(dt)
  local nx, ny = dungeon.px + dx, dungeon.py + dy
  if not dungeonBlocked(nx, dungeon.py) then dungeon.px = nx end
  if not dungeonBlocked(dungeon.px, ny) then dungeon.py = ny end
  placeIso("Player", dungeon.px, dungeon.py, 0.35)
  local x, y = iso(dungeon.px, dungeon.py)
  transform.set_position("DungeonCamera", x, y, 0)
end

function Dungeon_Start(entity)
  ensureState()
  bindControls()
  state.location = "dungeon"
  uiMode = "none"
  dungeon.attackTimer, dungeon.spellTimer = 0, 0
  dungeon.hurtTimer, dungeon.regenTimer = 0, 0
  dungeon.loot = {}
  setPool("Loot", LOOT_MAX, false)

  generateDungeon(state.dungeonSeed)
  renderDungeonGrid()
  spawnEnemies()
  placeIso("Player", dungeon.px, dungeon.py, 0.35)
  updateHud()
  audio.stop_music()
  audio.play_music("Sounds/dungeon.wav", 0.4, true)
  saveNow()
end

function Dungeon_Update(entity, dt)
  if updateCommonUi() then return end

  dungeon.attackTimer = math.max(0, dungeon.attackTimer - dt)
  dungeon.spellTimer = math.max(0, dungeon.spellTimer - dt)
  dungeon.hurtTimer = math.max(0, dungeon.hurtTimer - dt)
  dungeon.regenTimer = dungeon.regenTimer + dt
  if dungeon.regenTimer >= 1.0 then
    dungeon.regenTimer = dungeon.regenTimer - 1.0
    state.mp = math.min(state.maxMp, state.mp + 2 + state.skills.focus)
    updateHud()
  end

  moveDungeonPlayer(dt)
  if input.is_action_pressed("attack") then playerAttack() end
  if input.is_action_pressed("spell") then playerSpell() end
  updateEnemies(dt)
  updateLoot()

  if state.hp <= 0 then
    returnToTownAfterDeath()
    return
  end

  if input.is_action_pressed("interact") and
     distance(dungeon.px, dungeon.py, dungeon.exitX, dungeon.exitY) < 1.4 then
    state.location = "town"
    saveNow()
    audio.play("Sounds/portal.wav", 0.8)
    scene.load("Scenes/Town.nscene")
  end
end