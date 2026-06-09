# 타일셋 / 타일맵 에디터

NaraGame 의 인-엔진 맵 에디터입니다. `.ntmap` (자체 텍스트 포맷) 과
`.tmx` (Tiled 단일 inline 타일셋 + CSV 레이어) 를 읽고, `.ntmap` 로
저장합니다. 게임 (`brawl`, `mario`) 이 같은 파일을 `LoadNtmap` 으로
로드하며, `AssetWatcher` 가 디스크 변경을 감지해 실행 중인 게임에
즉시 반영합니다.

빌드 산출물: `build/windows-x64/bin/Release/tileset-editor.exe`.

## 화면 구성

```
┌──────────────────────────────────────────────────────────┐
│  Toolbar  (36px)  파일 경로 · 타일셋 · 맵 크기 · hover 셀  │
├────────────┬─────────────────────────────────────────────┤
│ [TILE|COL|OBJ] (탭 32px)                                  │
│  Palette   │                                             │
│  (256px)   │       Canvas (가변)                         │
│            │       LMB 페인트 · RMB 지우개                │
│            │       Middle 드래그 팬 · 휠 줌              │
├────────────┴─────────────────────────────────────────────┤
│  Status (24px)   MAP · TILE · ZOOM · MODE · SEL · UNDO   │
└──────────────────────────────────────────────────────────┘
```

좌측 패널 상단의 **TILE / COLL / OBJ** 탭을 클릭하면 모드가 즉시 전환됩니다
(`Tab` / `L` 키 단축도 그대로 사용 가능). 활성 탭은 노란색 위쪽 강조선과
함께 콘텐츠 영역 배경과 같은 색으로 변해 시각적으로 "하나의 면" 으로
연결됩니다.

해상도는 1280×800 고정입니다.

## 실행

```pwsh
# 기존 맵 열기
tileset-editor.exe path\to\level.ntmap
tileset-editor.exe path\to\map.tmx       # tmx 는 읽기만 (저장은 .ntmap)

# 새 맵 시작 (PNG 타일셋 지정)
tileset-editor.exe --tileset path\to\tiles.png --tilesize 32
tileset-editor.exe --tileset tiles.png --tilesize 32 --mapsize 40 24 `
                   --out level1.ntmap
```

인수 없이 실행하면 빈 상태로 떠서, `Ctrl+N` 으로 PNG 를 고르거나
`Ctrl+O` 로 기존 맵을 열 수 있습니다.

## 레이어 (편집 모드)

`Tab` 또는 `L` 키로 3 모드를 순환합니다. 각 모드는 좌측 팔레트의
내용과 페인트 대상이 달라집니다.

| 모드 | 데이터 | LMB | RMB |
|------|--------|-----|-----|
| **Tile** | 타일셋 인덱스 (정수) | 선택 타일로 칠하기 | -1 (빈 칸) |
| **Collision** | `Floor=0` / `Wall=1` / `Water=2` | 선택 코드로 칠하기 | 0 (Floor) |
| **Object** | 단일 문자 마커 | 선택 심볼 배치 | 셀의 마커 제거 |

세 레이어는 같은 셀에 공존할 수 있습니다. 예를 들어 마리오의
mushroom block (`!`) 은 **Tile=QUESTION + Object=!** 두 레이어가
동시에 차 있는 셀입니다.

### 객체 심볼 팔레트

팔레트의 심볼 세트는 `.ntmap` 의 옵션 `symbols` 섹션이 정의합니다 — 맵마다
자기 심볼을 들고 다닙니다. 섹션이 없으면 에디터의 기본 fallback (아래
9개) 이 적용되고, 새로 저장하면 그 fallback 이 그대로 파일에 기록됩니다.

기본 fallback:

| 심볼 | 의미 | 색 |
|------|------|-----|
| `P` | 플레이어 스폰 (마리오·브롤 공용) | 푸른 |
| `E` | 적 스폰      | 빨강 |
| `I` | 아이템       | 노랑 |
| `C` | 코인         | 노랑 |
| `X` | 일반 마커    | 회색 |
| `g` | 굼바 스폰    | 갈색 |
| `!` | 버섯 블록    | 살구 |
| `*` | 파이어 플라워 블록 | 핑크 |

게임은 자기가 모르는 심볼은 그냥 무시합니다. 새 게임이 자기 심볼 세트를
쓰려면 `.ntmap` 에 `symbols` 섹션을 손으로 한 번 채우거나, 에디터의
기본 fallback (`tools/tileset-editor/src/EditorApp.cpp`
의 `DefaultObjectSymbols()`) 에 추가하면 됩니다.

### 에디터에서 심볼 추가 / 삭제

Object 모드 팔레트 상단의 **`+ Add symbol`** 버튼을 누르면 새 심볼
모달이 뜹니다:
- 키보드로 단일 문자 → 심볼 char 지정
- 8개 컬러 스와치 중 클릭 → 색 지정
- 계속 타이핑 → 라벨 (최대 24자, Backspace 로 수정)
- `Enter` 확정, `Esc` 취소

각 심볼 셀의 우상단 **`x`** 버튼을 누르면 삭제 확인 모달이 뜹니다 —
그 심볼을 사용하는 맵 위의 모든 마커 개수까지 함께 표시. `Y` / Enter
확정, `N` / Esc 취소. 확정하면 심볼 목록에서 제거되고 맵 위 마커도 모두
사라지며 다음 저장 시 `.ntmap` 에 반영됩니다 (`m_dirty` 토글됨).

## 키바인드

### 파일 / 편집

| 키 | 동작 |
|----|------|
| `F1` | 키바인드 치트시트 토글 (Esc 로도 닫힘) |
| `Ctrl+O` | 열기 (`.ntmap` / `.tmx` 네이티브 다이얼로그) |
| `Ctrl+N` | 새 맵 (PNG 타일셋 선택 → 기본 32×24) |
| `Ctrl+S` | 저장. 경로가 없으면 Save As |
| `Ctrl+Shift+S` | 다른 이름으로 저장 (항상 다이얼로그) |
| `Ctrl+Z` | 실행 취소 |
| `Ctrl+Y` 또는 `Ctrl+Shift+Z` | 다시 실행 |
| `Esc` | 종료 — 저장 안 된 변경이 있으면 확인 모달 (`S` 저장+종료 / `D` 버리고 종료 / `C`·`Esc` 취소) |

`F1` 헬프 오버레이가 떠 있는 동안에는 다른 입력 (페인트·팔레트·스크롤바)
이 모두 차단됩니다 — "잠깐 멈추고 읽기" 용도로 안전하게 쓸 수 있습니다.

### 페인트

| 키 / 마우스 | 동작 |
|-------------|------|
| `LMB` | 선택 값(또는 스탬프)으로 페인트 |
| `RMB` | 빈 칸 / Floor / 마커 제거 |
| `Shift + LMB` 드래그 | 사각형 페인트 (release 에 커밋, 스탬프 타일링) |
| `F` (커서가 캔버스 안) | 같은 값 영역 채우기 (Flood fill) |
| `Alt + LMB` | 스포이드 (셀의 값을 선택값으로, 스탬프는 1x1 리셋) |
| `[` / `]` | 브러시 크기 줄이기/키우기 (1·3·5·7·9, 홀수) |

브러시 크기 > 1 이면 hover 셀을 중심으로 NxN 영역에 선택 값이 한 번에 칠해집니다.
Tile 모드에서 스탬프(다중 셀)와는 배타적으로 동작 — brush > 1 인 동안에는 스탬프
대신 `m_selectedTile` 단일 값으로 페인트. Object 모드는 항상 1로 강제 (마커는
셀당 하나).

#### 스탬프 브러시 (Tile 모드 전용)

팔레트에서 한 칸을 그냥 클릭하면 그 타일이 1x1 스탬프가 됩니다. **드래그**
하면 드래그 사각형이 그대로 스탬프가 되어, 캔버스에 한 번 클릭할 때마다
같은 패턴이 한꺼번에 박힙니다. 예: 풀+이끼 2x2 패치를 만들고 캔버스
드래그로 큰 영역을 채우면, 스탬프가 타일링되어 자연스러운 텍스처가 깔립니다.

- 팔레트의 사각형 영역 끝이 타일셋 끝을 넘으면 그 셀은 `skip` 으로 표시
  되어 캔버스에는 빈 칸 그대로 남습니다 (덮어쓰지 않음).
- `Shift+드래그` 로 사각형 페인트하면 스탬프가 `WxH` 단위로 타일링됩니다.
- 1-9 단축키, `Alt+클릭` (스포이드), 파일 로드는 모두 1x1 스탬프로 리셋.
- Collision / Object 모드는 스탬프가 적용되지 않고 단일 값으로 동작.

상태바의 `SEL` 필드는 스탬프가 다중 셀일 때 `SEL: 5 (3x2)` 처럼 크기를
같이 보여줍니다.

다중 셀 스탬프는 캔버스에 hover 했을 때 반투명 미리보기 + WxH 사각형
outline 으로 어디에 박힐지 보여줍니다. 맵 밖으로 빠지는 부분과 `-1`
(overhang) 셀은 미리보기에서 자동으로 빠집니다.

### 선택 (Marquee + 클립보드)

| 키 / 마우스 | 동작 |
|-------------|------|
| `Ctrl + LMB` 드래그 | 사각 영역 선택 |
| `Ctrl + C` | 선택 복사 |
| `Ctrl + X` | 선택 잘라내기 (복사 + 비움) |
| `Ctrl + V` | 호버 셀을 좌상단으로 붙여넣기 |

붙여넣기는 클립보드가 만들어진 레이어(Tile/Collision/Object) 에
페인트됩니다.

### 카메라

| 키 / 마우스 | 동작 |
|-------------|------|
| 방향키 | 팬 |
| `Middle` 드래그 | 팬 |
| 휠 위/아래 또는 `+` / `-` | 줌 인/아웃 (커서 기준 anchor) |
| `Home` | 카메라 (0, 0), 줌 1:1 |
| 미니맵 `LMB` 클릭/드래그 | 카메라를 그 지점으로 점프 |
| `G` | 그리드 표시 토글 |
| `H` | 비-액티브 레이어 오버레이 표시 토글 |

캔버스가 맵 전체를 한 번에 못 담을 때 (지금 zoom·맵 크기 기준) 우상단
코너에 미니맵이 자동으로 표시됩니다. 타일 텍스처가 그대로 mini 형태로
그려지고, 노란색 사각형이 현재 뷰포트 위치. 클릭/드래그로 카메라가 그
지점을 중심으로 이동합니다.

`H` 는 액티브 모드가 아닌 레이어의 오버레이를 끄고 켭니다. 예를 들어
Tile 모드에서 H 를 누르면 collision 음영 + 객체 마커가 사라지고 순수한
타일만 보입니다. Collision / Object 모드에서는 그 모드의 데이터가
주로 보이고 나머지 오버레이는 H 로 토글 가능. Tile 레이어는 다른 모드의
시각적 기준이라 항상 그려집니다.

### 팔레트 빠른 선택

| 키 | 동작 |
|----|------|
| `1` … `9` | 활성 모드의 팔레트 슬롯 1-9 번 선택 |
| `Tab` 또는 `L` | 모드 순환 (Tile → Collision → Object → …) |

### 맵 리사이즈

`Ctrl + 방향키` 가 해당 가장자리를 한 행/열 늘립니다. `Shift` 를
같이 누르면 줄입니다. 단일 Undo 스텝으로 묶이며, 객체 마커는
가장자리 이동에 맞춰 자동으로 좌표가 재계산됩니다 (잘려나가는
영역의 마커는 제거).

```
Ctrl+Right          → 오른쪽으로 1열 확장
Ctrl+Shift+Down     → 아래쪽 1행 축소
```

**마우스 드래그 리사이즈**: 맵 가장자리 근처에 커서를 올리면 그 변이
노란색으로 강조됩니다. `LMB` 로 잡고 안쪽/바깥쪽으로 드래그하면 셀
단위로 스냅되고, 드래그 중에는 새 맵 경계의 ghost 사각형이 보이고
커서 옆에 `WxH` 가 떠 있습니다. 손을 떼면 한 번에 커밋 (마찬가지로
단일 Undo 스텝). 변 4 곳 모두 동일하게 동작.

## `.ntmap` 파일 포맷

라인 기반 텍스트. UTF-8 BOM 금지 (파서가 BOM 을 `ntmap` 토큰의
일부로 읽음).

```
ntmap 1
tileset <relative-png-path> <cellW> <cellH> <margin> <spacing>
size <mapW> <mapH>
layer
<int> <int> ... <int>      # mapW 개의 정수, mapH 줄 반복 (-1 = 빈 칸)
collision                  # 선택 — 없어도 됨
<uint8> ... <uint8>        # mapW × mapH 개. 0=Floor 1=Wall 2=Water
objects                    # 선택
<symbol> <x> <y>           # 한 줄에 하나, x/y 는 셀 좌표
...
symbols                    # 선택 — 객체 팔레트 정의
<symbol> <rrggbb> <label>  # 한 줄에 하나, label 은 줄 끝까지 (공백 OK)
...
```

규칙:
- `#` 으로 시작하는 줄은 주석.
- `tileset` 경로는 `.ntmap` 파일이 있는 디렉토리 기준 상대 경로
  (절대 경로도 허용).
- `collision` 의 크기가 `mapW × mapH` 와 다르면 로더가 무시하고
  경고합니다.
- `objects` 의 좌표가 맵 범위 밖이면 로드 시 자동 클리핑.
- `symbols` 는 에디터 전용 메타데이터. 게임 코드는 `ObjectMarker.symbol`
  (단일 char) 만 소비하므로 색·라벨을 무시합니다. 섹션이 없으면 에디터가
  내장 fallback (P/E/I/C/X/M/g/!/`*`) 을 보여줍니다.

## 게임 통합

게임 코드에서 `LoadNtmap` 으로 직접 로드합니다:

```cpp
#include <NaraGame/assets/TilemapLoader.h>

TilemapAsset asset = LoadNtmap(device, "assets/level1.ntmap",
                               TextureFilter::Point);
if (!asset.valid) { /* 에러 처리 */ }

asset.map.Draw(renderer, camera.VisibleBounds());

for (const ObjectMarker& m : asset.objects) {
    if (m.symbol == 'P') player.spawn = {m.x, m.y};
    ...
}
```

`asset` 이 텍스처를 소유합니다. `asset.map` 의 `SpriteSheet` 가
`asset.texture` 를 raw pointer 로 참조하므로, **`asset` 을 멤버로
유지**해야 합니다. `std::move(asset.map)` 으로 맵만 빼내도 텍스처는
원래 `asset` 에 남아 살아있어야 합니다.

### 라이브 리로드

`AssetWatcher` 가 디렉토리 변경을 감지해 콜백을 호출합니다.
에디터에서 `Ctrl+S` 하면 게임이 즉시 새 맵을 반영합니다.

```cpp
m_watcher.Watch(assetsDir, [this](const std::string& rel) {
    if (rel == "level1.ntmap" || rel == "tiles.png") {
        // 다음 OnUpdate 에서 다시 LoadNtmap
        m_reloadPending = true;
    }
});
```

게임 예시:
- `test/brawl/src/BrawlApp.cpp` — `EnsureBackgroundMap` /
  `ReloadBackgroundMap`. 콜리전 grid 까지 사용.
- `test/mario/src/MarioApp.cpp` — `GameScene::OnEnter` 에서
  `LoadNtmap`, `levelReloadCounter` 로 씬 재진입 신호.

## 새 맵을 만드는 표준 흐름

1. `tileset-editor.exe --tileset assets/tiles.png --tilesize 32`
2. 좌측 팔레트에서 타일 클릭 → 캔버스에 `LMB` 로 페인트
3. `Tab` 으로 Object 레이어 진입 → `P` (플레이어 스폰) 등 배치
4. `Ctrl+S` → 처음에는 Save As 다이얼로그가 뜸. `.ntmap` 확장자로 저장
5. 게임 코드에서 같은 경로를 `LoadNtmap` 으로 읽음
6. 게임 실행 중이면 `Ctrl+S` 만으로 자동 리로드

## 알려진 제한 / 후속 작업

- 객체 마커는 셀당 하나. 한 셀에 두 종류의 마커는 불가.
- `.tmx` 는 읽기 전용 + 단일 inline 타일셋 + CSV 레이어만 지원.
  flip/rotate 비트, 외부 TSX, base64/zlib 는 거부.
