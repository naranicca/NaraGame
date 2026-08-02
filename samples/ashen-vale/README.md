# Ashen Vale 만들기 — 아이소메트릭 액션 RPG

> `samples/ashen-vale/Assets`에는 월드·UI 아틀라스, 타이틀 배경과 모든
> WAV가 포함되어 있습니다. 새 프로젝트의 `Assets`로 복사하면 됩니다.

이 튜토리얼에서는 쿼터뷰 아이소메트릭 화면에서 캐릭터를 움직이고,
마을에서 퀘스트를 받은 뒤 무작위 던전을 탐험하는 액션 RPG를 만듭니다.
장르에서 널리 쓰이는 구조를 학습하되 특정 상용 게임의 명칭, 세계관,
캐릭터, UI, 맵, 대사, 수치 및 에셋은 복제하지 않습니다. 예제의
`Ashen Vale`, 인물, 몬스터와 아이템은 모두 튜토리얼용 창작물입니다.

완성되는 기능은 다음과 같습니다.

* `New Game`, `Load Game`, `Exit`가 있는 타이틀 화면
* 아이소메트릭 투영, 8방향 이동과 깊이 정렬
* 마을 NPC 대화와 수락·진행·완료되는 퀘스트
* 포탈을 이용한 마을·던전 이동
* 시드 기반 방·복도 절차 생성 던전
* 근접 공격, 마법 공격, 적 추적과 접촉 피해
* HP, MP, 물약, 사망과 마을 귀환
* 12칸 인벤토리, 소비 아이템, 무기·방어구 장착
* 세 갈래의 간단한 스킬 트리
* 저장과 불러오기
* Windows, Android, macOS/iOS 빌드 준비

처음부터 끝까지 순서대로 진행하십시오. 중간 절의 코드만 복사하면
아직 만들지 않은 엔티티를 참조할 수 있습니다.

---

## 0. 완성 구조 이해하기

프로젝트는 세 씬과 한 Lua 파일을 사용합니다.

```text
AshenVale/
├─ Assets/
│  ├─ Scenes/
│  │  ├─ Title.nscene
│  │  ├─ Town.nscene
│  │  └─ Dungeon.nscene
│  ├─ Scripts/
│  │  └─ AshenVale.lua
│  ├─ Sprites/
│  │  ├─ world_atlas.png
│  │  ├─ ui_atlas.png
│  │  └─ title_background.png
│  └─ Sounds/
│     ├─ sword.wav
│     ├─ spell.wav
│     ├─ hurt.wav
│     ├─ portal.wav
│     ├─ quest.wav
│     ├─ town.wav
│     └─ dungeon.wav
└─ ProjectSettings/
   └─ project.json
```

씬 전환 전에 `state` 테이블을 저장합니다. `savegame.save("slot1")`은
그 테이블을 `saves/slot1.json`에 기록합니다. 던전 전체 타일을 저장하지
않고 `dungeonSeed`만 저장하므로 Load Game은 같은 구조를 다시 만듭니다.
적의 정확한 위치까지 저장하는 방식보다 파일이 작고 이해하기 쉽습니다.

---

## 1. 프로젝트 만들기

1. NaraGame을 실행합니다.
2. **File → New Project**를 선택합니다.
3. 프로젝트 이름을 `AshenVale`로 입력합니다.
4. 빈 폴더를 프로젝트 위치로 선택합니다.
5. Project 패널에서 `Assets/Scenes`, `Assets/Scripts`,
   `Assets/Sprites`, `Assets/Sounds`가 있는지 확인합니다.
6. 세 빈 씬을 만들고 각각 `Title.nscene`, `Town.nscene`,
   `Dungeon.nscene`으로 저장합니다.
7. `Title.nscene`을 연 뒤 **File → Set Current Scene as Main**을
   실행합니다.

Play를 눌렀을 때 에디터가 마지막 작업 씬을 열 수 있으므로 각 씬을
개별 시험할 때는 원하는 씬을 직접 여십시오. 독립 실행 빌드는 항상
Main Scene인 `Title.nscene`에서 시작합니다.

---

## 2. 에셋 준비와 저작권 안전 수칙

직접 그리거나 사용 권한이 있는 에셋만 사용합니다. 기존 상용 게임의
로고, 글꼴, 아이콘 모양, 인물 실루엣, 몬스터, 효과음 또는 대사를
그대로 추출하지 마십시오.

### 2.1 월드 아틀라스

`world_atlas.png`를 64×64 셀 기준으로 준비합니다. 실제 그림 크기는
달라도 되지만 다음 Sub Sprite 이름은 코드와 정확히 같아야 합니다.

| Sub Sprite | 용도 | 권장 그림 |
|---|---|---|
| `town_floor` | 마을 바닥 | 밝은 2:1 마름모 타일 |
| `dungeon_floor` | 던전 바닥 | 어두운 석재 마름모 |
| `dungeon_wall` | 벽 | 바닥 마름모와 세로 벽면 |
| `hero_idle` | 플레이어 | 48×64 전후 창작 캐릭터 |
| `warden` | NPC | 등불을 든 마을 수호자 |
| `portal` | 이동 포탈 | 청록색 원형 효과 |
| `crawler` | 일반 적 | 네 발 그림자 짐승 |
| `brute` | 강한 적 | 큰 돌 갑옷 짐승 |
| `loot` | 드롭 아이템 | 작은 주머니 |
| `chest` | 상자 | 독자적인 나무 상자 |

Texture Import Settings에서 다음을 설정합니다.

* Sprite Mode: `Multiple`
* Filter: 픽셀 아트는 `Point`, 고해상도 그림은 `Linear`
* Pixels Per Unit: `64`
* Pivot: 바닥 타일은 `Center`, 인물·벽은 `BottomCenter`
* 위 표의 이름으로 Sprite Sheet 셀 등록

### 2.2 UI 아틀라스

`ui_atlas.png`에는 다음 요소를 만듭니다.

* `title_new`, `title_load`, `title_exit`
* `menu_cursor`
* `hp_back`, `hp_fill`, `mp_back`, `mp_fill`
* `panel`, `slot`, `slot_selected`
* `skill_might`, `skill_focus`, `skill_vigor`
* `digit_0`~`digit_9`

버튼 문구를 그림으로 만드는 이유는 현재 런타임 UI가 SpriteRenderer와
대화 패널 중심이기 때문입니다. 이후 MSDF 텍스트 컴포넌트로 교체해도
게임 로직은 달라지지 않습니다.

### 2.3 오디오

44.1kHz 또는 48kHz의 WAV/OGG/MP3를 사용합니다. 짧은 효과음은 WAV,
긴 음악은 OGG가 편합니다. 파일명은 위 폴더 구조와 맞춥니다.

---

## 3. 아이소메트릭 좌표 이해하기

게임 로직은 타일 좌표 `(tx, ty)`로 계산하고 화면에는 다음과 같이
투영합니다.

```text
screenX = (tx - ty) × 1.0
screenY = (tx + ty) × 0.5
```

따라서 한 타일의 화면 크기는 대략 `(2, 1)` 월드 단위입니다. 더 뒤에
있는 물체가 먼저 그려지도록 `tx + ty`가 클수록 Order를 높입니다.

```lua
local function iso(tx, ty)
  return (tx - ty), (tx + ty) * 0.5
end

local function placeIso(entity, tx, ty, height)
  local x, y = iso(tx, ty)
  transform.set_position(entity, x, y + (height or 0), 0)
  sprite.set_order(entity, math.floor((tx + ty) * 10))
end
```

`Transform`의 x/y를 직접 이동시키지 않고 타일 공간 좌표를 별도로
보관하는 것이 핵심입니다. 충돌, 적 거리, 던전 생성은 모두 직교 좌표로
계산하고, 마지막에만 `iso`를 호출합니다.

---

## 4. Title 씬 구성

`Title.nscene`에 다음 계층을 만듭니다.

```text
TitleCamera                 Camera
TitleBackground             SpriteRenderer
NewGameLabel                SpriteRenderer (title_new)
LoadGameLabel               SpriteRenderer (title_load)
ExitLabel                   SpriteRenderer (title_exit)
MenuCursor                  SpriteRenderer (menu_cursor)
LoadUnavailable             SpriteRenderer (회색 자물쇠, 선택 사항)
TitleController             Script
```

권장 Transform 값:

| 엔티티 | Position | Size/설명 |
|---|---:|---|
| `TitleCamera` | `(0,0)` | Zoom 48 |
| `TitleBackground` | `(0,0)` | 화면을 덮도록 조절 |
| `NewGameLabel` | `(0,1.0)` | 첫 메뉴 |
| `LoadGameLabel` | `(0,0)` | 둘째 메뉴 |
| `ExitLabel` | `(0,-1.0)` | 셋째 메뉴 |
| `MenuCursor` | `(-2.2,1.0)` | 선택 화살표 |

`TitleController`의 Script는 `Scripts/AshenVale.lua`, Entry Function은
`Title`로 설정합니다.

---

## 5. Town 씬 구성

### 5.1 계층

```text
TownCamera                  Camera
├─ HUD                     빈 엔티티
│  ├─ HpBack               SpriteRenderer
│  ├─ HpFill               SpriteRenderer
│  ├─ MpBack               SpriteRenderer
│  ├─ MpFill               SpriteRenderer
│  ├─ QuestMarker          SpriteRenderer
│  └─ HelpPanel            SpriteRenderer
TownMap                     Tilemap 또는 바닥 SpriteRenderer 모음
Player                      SpriteRenderer (hero_idle)
WardenNPC                   SpriteRenderer (warden)
TownPortal                  SpriteRenderer (portal)
TownController              Script
```

`HUD`는 `TownCamera`의 자식으로 만듭니다. `HpFill`, `MpFill`의 Pivot은
`BottomLeft`로 설정해야 가로 크기를 줄일 때 왼쪽 끝이 고정됩니다.

### 5.2 마을 바닥

Tilemap을 이용해 18×18 정도의 마름모 마을을 그립니다. 직교 Tilemap
그리드 자체는 사각형이지만 `town_floor` 그림에 투명 여백을 둔 2:1
마름모를 사용하면 아이소메트릭처럼 보입니다. 더 정확한 배치를 원하면
바닥 SpriteRenderer들을 만들고 위의 `iso()` 좌표에 놓습니다.

첫 프로젝트에서는 이동 가능 영역을 단순한 직사각형
`tx=1..16, ty=1..16`으로 둡니다. 건물 충돌은 뒤에서 `townBlocked()`에
사각형 영역을 추가합니다.

### 5.3 중요 위치

코드에서 다음 타일 좌표를 사용합니다.

| 대상 | 타일 좌표 |
|---|---:|
| 플레이어 시작 | `(8, 12)` |
| `WardenNPC` | `(6, 6)` |
| `TownPortal` | `(13, 5)` |

씬에서 대략 배치해도 Start 함수가 정확한 아이소메트릭 위치로 다시
옮깁니다.

`TownController`의 Entry Function은 `Town`입니다.

---

## 6. Dungeon 씬과 오브젝트 풀

절차 생성 중 새 SpriteRenderer에 텍스처를 지정하는 API는 아직 없으므로
필요한 렌더러를 씬에 미리 만들고 enabled를 켜고 끕니다. 이것이
오브젝트 풀입니다.

### 6.1 계층

```text
DungeonCamera
├─ HUD
│  ├─ HpBack / HpFill
│  ├─ MpBack / MpFill
│  └─ HelpPanel
FloorPool
├─ Floor001 ... Floor160
WallPool
├─ Wall001 ... Wall100
EnemyPool
├─ Enemy01 ... Enemy16
LootPool
├─ Loot01 ... Loot12
DungeonExit                 SpriteRenderer (portal)
Player                      SpriteRenderer (hero_idle)
DungeonController           Script
```

### 6.2 풀 만드는 방법

1. `Floor001`을 만들고 `world_atlas.png / dungeon_floor`를 지정합니다.
2. Size를 `(2,1)`, Pivot을 Center, enabled를 끕니다.
3. Duplicate로 `Floor160`까지 만듭니다.
4. `Wall001`은 `dungeon_wall`, Pivot `BottomCenter`, Size `(2,2)`로
   만들고 `Wall100`까지 복제합니다.
5. `Enemy01`은 `crawler`, Size `(0.8,1.2)`, Pivot `BottomCenter`로
   만들고 `Enemy16`까지 복제합니다.
6. `Loot01`은 `loot`, Size `(0.4,0.4)`로 만들고 `Loot12`까지
   복제합니다.

이름의 0 개수를 정확히 맞추십시오. 코드가 `Floor%03d`, `Enemy%02d`로
찾습니다. 풀을 늘리려면 씬 엔티티 수와 코드 상수를 함께 늘립니다.

`DungeonController`의 Entry Function은 `Dungeon`입니다.

---

## 7. 공통 스크립트 작성

`Assets/Scripts/AshenVale.lua`를 만들고 아래 코드를 순서대로 붙입니다.
한 파일에 모든 씬 함수를 두므로 Lua 모듈 경로를 설정할 필요가 없습니다.

### 7.1 상수, 유틸리티와 새 게임 상태

```lua
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
```

`schema`는 나중에 저장 형식이 달라졌을 때 이전 세이브를 변환하기 위한
번호입니다. 테이블에는 함수, userdata 또는 순환 참조를 넣지 마십시오.
숫자·문자열·불리언·일반 테이블만 저장 대상으로 사용합니다.

### 7.2 입력 설정

```lua
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
```

대각선 벡터를 정규화하지 않으면 대각선 이동이 약 1.414배 빨라집니다.

---

## 8. 타이틀 메뉴 구현

다음 코드를 같은 파일에 이어 붙입니다.

```lua
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
```

확인 사항:

1. 위·아래 키가 세 항목을 순환하는지 확인합니다.
2. 저장 파일이 없으면 Load가 회색인지 확인합니다.
3. New Game은 기존 진행을 초기화합니다. 제품 버전에서는
   “기존 저장을 덮어쓸까요?” 확인 대화를 추가하는 것이 좋습니다.
4. Exit는 에디터 자체를 닫을 수도 있으므로 에디터 테스트 중에는
   저장 후 실행하십시오. 독립 실행 빌드에서는 게임 창이 종료됩니다.

---

## 9. HUD, 인벤토리와 스킬 트리

### 9.1 HUD 갱신

```lua
local function updateHud()
  local hpRatio = clamp(state.hp / state.maxHp, 0, 1)
  local mpRatio = clamp(state.mp / state.maxMp, 0, 1)
  sprite.set_size("HpFill", 3.6 * hpRatio, 0.28)
  sprite.set_size("MpFill", 3.6 * mpRatio, 0.28)
end
```

`HpFill`과 `MpFill`의 Pivot이 왼쪽이 아니라면 게이지가 양쪽으로
줄어듭니다. Texture Import Settings에서 Pivot을 다시 확인하십시오.

### 9.2 인벤토리

```lua
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
```

인벤토리는 12칸으로 제한합니다. 드롭을 줍기 전에 `#state.inventory < 12`를
검사합니다. 장비 교체 시 기존 장비를 가방으로 되돌리는 순서가 중요합니다.

### 9.3 스킬 트리

세 갈래 스킬은 각각 최대 3랭크입니다.

| 스킬 | 랭크당 효과 |
|---|---|
| Might | 기본 공격력 +3 |
| Focus | 최대 MP +10, 주문 피해 +4 |
| Vigor | 최대 HP +15, 받는 피해 감소 +1 |

```lua
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
```

처음에는 모든 선행 조건을 생략했습니다. 확장할 때는 예를 들어
`Might 2`가 있어야 강공격을 해금하도록 조건을 추가할 수 있습니다.

---

## 10. 마을 이동과 NPC 퀘스트

### 10.1 마을 런타임 상태

```lua
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
```

축을 나누어 충돌을 검사하면 벽 모서리에 닿았을 때 완전히 멈추지 않고
한 축 방향으로 미끄러집니다.

### 10.2 NPC 대화와 퀘스트 단계

```lua
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
```

퀘스트 상태를 문자열로 두면 디버깅이 쉽습니다.

```text
none → active → ready → done
```

### 10.3 마을 Start/Update

```lua
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
```

포탈에 닿기만 해도 이동하게 만들 수 있지만, 실수로 이동하지 않도록
이 예제는 가까이에서 E를 누르는 방식을 사용합니다.

---

## 11. 절차 생성 던전

### 11.1 데이터 구조

24×24 격자를 사용합니다. `0`은 빈 공간, `1`은 바닥, `2`는 벽입니다.

```lua
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
```

### 11.2 방과 복도 만들기

```lua
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
```

같은 시드를 주면 같은 던전이 만들어집니다. Load Game에서 구조가
바뀐다면 `math.randomseed(state.dungeonSeed)`가 다른 난수 호출보다 먼저
실행되는지 확인하십시오.

### 11.3 풀에 타일 배치

```lua
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
```

풀이 부족하면 일부 타일이 보이지 않지만 게임이 중단되지는 않습니다.
Console의 경고를 보고 풀 크기를 조정합니다.

---

## 12. 적 생성과 전투

### 12.1 적 생성

```lua
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
```

### 12.2 피해 계산

```lua
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
```

무적 시간 없이 매 프레임 접촉 피해를 주면 1초에 수십 번 피해가
들어갑니다. `hurtTimer`는 반드시 dt로 감소시킵니다.

### 12.3 적 사망, 경험치와 퀘스트

```lua
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
```

### 12.4 플레이어 공격

```lua
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
```

근접 공격은 가장 가까운 한 적, 주문은 반경 안의 모든 적을 공격합니다.
공격 애니메이션을 추가할 때는 타격 판정 시점을 Animation Event에 맞추면
손과 피해가 더 자연스럽게 일치합니다.

### 12.5 적 AI

```lua
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
```

이 AI는 가까운 적이 직선 추적합니다. 벽이 많은 맵에서 적이 걸리면
`scene.find_path`를 이용한 A* 경로 추적으로 확장하십시오. 첫 완성본은
두 칸 폭 복도를 사용하므로 직선 추적도 충분히 시험할 수 있습니다.

---

## 13. 드롭 획득과 사망

```lua
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
```

사망 시 저장을 삭제하지 않습니다. 골드 10%와 시간을 잃고 마을로
돌아오는 가벼운 페널티를 사용합니다. 더 강한 규칙은 프로젝트 성격에
맞게 결정하십시오.

---

## 14. 던전 전체 실행 루프

```lua
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
```

스크립트를 저장하면 ScriptHost가 다시 로드합니다. Console에 Lua 오류가
나오면 표시된 줄 번호를 먼저 고치십시오.

---

## 15. Script 컴포넌트 연결

각 씬에서 다음을 확인합니다.

| 씬 | 엔티티 | Script | Entry Function |
|---|---|---|---|
| Title | `TitleController` | `AshenVale.lua` | `Title` |
| Town | `TownController` | `AshenVale.lua` | `Town` |
| Dungeon | `DungeonController` | `AshenVale.lua` | `Dungeon` |

Entry Function이 `Town`이면 NaraGame은 `Town_Start`, `Town_Update`를
호출합니다. 엔티티 이름과 Entry Function을 혼동하지 마십시오.

---

## 16. 단계별 테스트

한꺼번에 시험하지 말고 다음 순서로 진행합니다.

### 16.1 타이틀

1. Title 씬을 열고 Play.
2. 메뉴 순환 확인.
3. 저장이 없을 때 Load 거부 확인.
4. New Game으로 Town 전환 확인.
5. 독립 실행 빌드에서 Exit 확인.

### 16.2 마을

1. WASD 직선·대각선 속도 확인.
2. 카메라와 HUD 추적 확인.
3. 건물 영역 통과 불가 확인.
4. NPC 근처 E, 수락/거절 확인.
5. 포탈 근처 E로 Dungeon 전환 확인.

### 16.3 던전

1. 바닥과 벽이 서로 다른 시드로 바뀌는지 확인.
2. 플레이어와 적이 벽을 통과하지 않는지 확인.
3. Space 공격 범위와 재사용 시간 확인.
4. Q 사용 시 MP 15 감소와 범위 피해 확인.
5. 적 사망 시 경험치·퀘스트 카운트·드롭 확인.
6. 아이템 12개일 때 추가 획득이 멈추는지 확인.
7. 출구 E로 마을 귀환 확인.

### 16.4 저장

1. 퀘스트 수락 후 게임 종료.
2. Load Game 선택.
3. 퀘스트 진행, 장비, 레벨, HP/MP 확인.
4. 던전에서 종료 후 Load하여 같은 시드의 던전 확인.
5. `saves/slot1.json`을 일부러 손상한 경우 Load가 실패하고 New Game이
   가능한지 확인.

---

## 17. 자주 생기는 문제

### 바닥이나 벽 일부가 안 보인다

Console에 `pool exhausted`가 출력되는지 확인합니다. `Floor161` 이후를
추가하고 `FLOOR_MAX`를 같은 값으로 늘립니다.

### 인물이 타일 뒤로 들어간다

인물과 벽의 Pivot을 BottomCenter로 맞추고 모든 이동 후 `placeIso`를
호출하십시오. 수동 Order 값이 남아 있어도 런타임에 덮어씁니다.

### 대화 창을 닫은 뒤 움직이지 않는다

`uiMode`를 `"none"`으로 되돌렸는지, `dialog.hide()`를 호출했는지
확인합니다.

### Load Game 후 상태가 nil이다

`savegame.load` 성공 직후 `ensureState()`를 호출하십시오. 새 필드를
추가한 뒤 오래된 저장 파일을 읽을 때 기본값을 채우는 역할도 합니다.

### 음악이나 효과음이 안 난다

파일 경로의 대소문자를 확인합니다. Windows는 대소문자를 관대하게
처리하지만 Android와 Apple 번들은 구분할 수 있습니다. Build 결과의
`assets.pak`에 `Sounds/` 파일이 포함됐는지도 확인합니다.

### 적이 벽 모서리에 걸린다

복도를 두 칸 폭으로 유지하고 적 크기를 타일보다 작게 그립니다.
완성도를 높일 때 `scene.find_path` 기반 경로를 0.2초 간격으로 계산하고
웨이포인트를 따라가게 만드십시오. 매 프레임 모든 적의 A*를 계산하면
불필요하게 비쌉니다.

---

## 18. 게임 감각 개선

핵심 기능이 모두 동작한 뒤 다음 순서로 다듬습니다.

1. 이동·대기·공격·피격·사망 애니메이션을 Animator Controller로 구성.
2. 공격 Animation Event에서 실제 피해를 적용.
3. 공격 직후 0.05초 정지하는 짧은 히트 스톱.
4. 적 피격 시 Sprite 색을 잠시 흰색 또는 붉은색으로 변경.
5. ParticleSystem으로 주문, 포탈, 드롭 광택 추가.
6. 강한 적의 색·크기·공격 예고를 일반 적과 명확히 구분.
7. 드롭 전에 작은 빛기둥을 표시하되 아이템 등급 색을 과도하게
   상용 게임과 동일하게 복제하지 않기.
8. NPC 머리 위 QuestMarker를 `none/active/ready/done`에 따라 표시.

전투 수치는 한 번에 크게 바꾸지 말고 플레이 테스트 기록을 남겨
조정합니다. 예를 들어 기본 공격 10, 일반 적 HP 35라면 대략 네 번
공격해야 쓰러집니다. 초반이 너무 느리면 적 HP를 먼저 낮추고, 후반이
쉬우면 방문 횟수에 따라 HP를 늘립니다.

```lua
local visitScale = 1 + state.dungeonVisits * 0.12
e.hp = math.floor(e.hp * visitScale)
e.maxHp = e.hp
```

---

## 19. 확장 과제

### 장비 속성

`power` 하나 대신 접두·접미 옵션을 둡니다.

```lua
{
  id = "ash_blade_rare",
  name = "Tempered Ash Blade",
  kind = "weapon",
  power = 7,
  crit = 0.05,
  manaOnHit = 1
}
```

### 스킬 트리 선행 조건

노드를 `{id, rank, maxRank, requires}` 테이블로 바꾸고, 선행 노드 랭크가
부족하면 선택지를 회색 표시하거나 선택 후 설명 대화를 띄웁니다.

### 여러 던전 층

`state.dungeonFloor`를 추가하고 출구에서 마지막 층이 아니면 seed를
변경하여 Dungeon 씬을 다시 로드합니다. 3층마다 강한 적을 배치합니다.

### 상인과 골드

NPC 대화의 `dialog.choices`에 구매 목록을 만들고 골드와 인벤토리
공간을 검사합니다. 판매 가격은 구매 가격보다 낮게 두어 무한 이익을
막습니다.

### 저장 슬롯

`slot1`, `slot2`, `slot3` 버튼을 만들고 선택한 슬롯명을 별도 설정
저장에 기록합니다. 화면에 레벨과 마지막 위치를 표시하려면 슬롯별
요약 테이블을 읽는 로직을 추가합니다.

---

## 20. 빌드

1. Title 씬에서 **Set Current Scene as Main**을 다시 확인합니다.
2. **File → Build...**을 선택합니다.
3. 타겟 플랫폼과 빈 출력 폴더를 선택합니다.
4. Application ID를 `com.example.ashenvale`처럼 자신이 소유한 역도메인
   형식으로 바꿉니다.
5. Version을 `0.1.0`으로 설정합니다.

Windows 결과는 `game.exe`, `assets.pak`, `ProjectSettings/`입니다.
Android는 내보낸 폴더의 `platform/android`에서 다음을 실행합니다.

```powershell
.\gradlew.bat :app:assembleRelease
```

macOS/iOS는 Xcode가 설치된 macOS에서 내보낸 Apple 프로젝트를
구성하고 서명합니다. 모바일에서는 터치 이동 UI가 필요하므로 출시
전 가상 스틱과 스킬 버튼을 추가하십시오. 이 튜토리얼의 키보드 입력은
데스크톱 완성본과 모바일 개발 중 테스트를 위한 기본 입력입니다.

---

## 21. 최종 체크리스트

* [ ] 타이틀의 New Game, Load Game, Exit가 모두 동작한다.
* [ ] 새 게임은 기존 상태를 초기화한다.
* [ ] 마을에서 NPC 대화로 퀘스트를 수락하고 완료한다.
* [ ] 포탈로 무작위 던전에 들어가고 출구로 돌아온다.
* [ ] 같은 seed는 같은 던전을 만든다.
* [ ] 근접 공격과 MP를 소비하는 주문이 동작한다.
* [ ] HP 0에서 마을로 돌아오며 사망 페널티가 적용된다.
* [ ] 인벤토리 제한, 물약, 무기와 방어구 교체가 동작한다.
* [ ] 스킬 포인트와 세 스킬의 효과가 저장된다.
* [ ] 퀘스트 진행, 레벨, 장비와 위치가 Load Game 후 유지된다.
* [ ] 효과음과 마을·던전 음악이 빌드에서도 재생된다.
* [ ] 사용한 모든 이미지, 글꼴과 사운드의 라이선스를 기록했다.

여기까지 완료하면 타이틀, 안전한 거점, 퀘스트, 절차 생성 탐험,
전투, 성장, 전리품과 저장이 연결된 작은 아이소메트릭 액션 RPG의
전체 플레이 루프가 완성됩니다.
