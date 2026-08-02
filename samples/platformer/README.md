# 클래식 플랫포머 만들기 — NaraGame 에디터 튜토리얼

> `samples/platformer/Assets`에는 이 문서에서 사용하는 모든 PNG와 WAV가
> 포함되어 있습니다. `MyGame`을 만든 직후 그 내용을 프로젝트 `Assets`로
> 복사하면 별도의 에셋 준비 없이 진행할 수 있습니다.

이 튜토리얼은 NaraGame 에디터만으로 클래식 스타일의 2D
플랫포머의 뼈대를 완성하는 과정을 처음부터 끝까지 설명합니다.
프로젝트 생성 → 캐릭터 → 스프라이트 편집 → 애니메이션 → 입력 →
물리 → 파티클 → 배경/음악 → 패럴랙스 → 게임 오버 → 스플래시
→ 빌드 순서로 진행합니다. 각 단계마다 어느 창의 어느 버튼을
누르는지, 어떤 필드에 어떤 값을 넣는지, 어떤 Lua 코드를 저장하는지
모두 명시했으니 문서만 보고 따라 하시면 됩니다.

---

## 목차

0. 시작 전 준비물
1. 프로젝트 만들기 & 첫 씬 저장
2. 배경(단색) + 메인 카메라 배치
3. 지면 만들기 — Tileset 편집기 + Tilemap 페인트
4. 플레이어 캐릭터 만들기 — 스프라이트 임포트 & 슬라이싱
5. 플레이어 물리 컴포넌트 붙이기 (Rigidbody2D + BoxCollider)
6. 애니메이션 만들기 (Idle / Run / Jump)
7. Animator Controller 만들기 (상태 그래프 + 파라미터)
8. 키 입력 & 이동 스크립트 (Player.lua)
9. 카메라가 플레이어를 따라가게 하기
10. 파티클 이펙트 (점프 먼지 + 코인 반짝임)
11. 코인 수집 + 트리거 이벤트
12. 배경 음악 + 효과음
13. 배경 패럴랙스 (여러 레이어)
14. 게임 오버 & 씬 재시작
15. 스플래시 화면
16. 게임 빌드 & 실행
17. 자주 하는 실수 요약
18. 적 만들기 — 순찰 · 발견 · 추적, 그리고 밟기
19. HUD 만들기 — 점수 · 스테이지 · 라이프 표시
20. 움직이는 플랫폼
21. 깨지는 블록과 ? 블록 (아이템 블록)

부록 A. Lua API 요약표
부록 B. Lua 라이프사이클 훅 목록

---

## 0. 시작 전 준비물

* Windows + 이미 빌드된 `NaraGame.exe` 실행 파일
  (`build/windows-x64/bin/Release/NaraGame.exe`).
* 아래 이미지/사운드 파일을 미리 준비합니다. 파일 확장자와 대략적인
  픽셀 크기만 지키면 됩니다. 예시 이름은 아래 튜토리얼과 그대로
  맞춰 두면 편합니다.

| 종류 | 예시 파일명 | 권장 사양 |
|---|---|---|
| 플레이어 스프라이트 시트 | `player.png` | 가로로 여러 프레임이 나열된 시트, 프레임당 예: 16x16 또는 32x32 |
| 타일셋 | `tiles.png` | 격자 정렬, 셀 크기 16x16 또는 32x32 |
| 코인 스프라이트 | `coin.png` | 16x16 이상 |
| 배경 레이어 3장 | `sky.png`, `mountain.png`, `trees.png` | 가로로 긴 이미지 |
| 점프 SFX | `jump.wav` | 짧은 WAV |
| 코인 SFX | `coin.wav` | 짧은 WAV |
| 배경 음악 | `bgm.wav` | 루프 가능한 WAV |

파일이 없어도 튜토리얼의 배치·설정을 따라 해 볼 수 있습니다.
스프라이트 없이 진행하면 스프라이트 슬롯을 비워 두거나 컬러
사각형만 보이게 됩니다.

---

## 1. 프로젝트 만들기 & 첫 씬 저장

### 1.1 새 프로젝트 만들기

1. `NaraGame.exe`를 실행합니다.
2. 상단 메뉴에서 **File → New Project...** 를 클릭합니다.
3. `New Project` 창이 뜹니다.
   * **Project name**: `MyGame`
   * **Location (parent folder)** 옆의 **Browse...** 버튼을 눌러
     새 게임을 담을 부모 폴더를 선택합니다.
     선택된 폴더는 반드시 **비어 있어야** 합니다. (기존 폴더에
     내용이 있으면 `"Folder must be empty."` 오류가 뜹니다.)
4. **Create** 버튼을 누릅니다.

프로젝트가 만들어지면 아래 폴더 구조가 자동으로 생성됩니다.

```
<선택한 부모>/MyGame/
  ProjectSettings/
    project.json               -- 프로젝트 설정 파일
  Assets/
    Sprites/    Tilesets/    Scripts/    Scenes/
    Prefabs/    Materials/   Animations/
  Library/
    metadata/   cache/       thumbnails/
```

> `MyGame.nproj` 같은 확장자 파일은 없습니다. 프로젝트를 다시 열
> 때는 **File → Open Project...** 로 `MyGame/` 폴더 자체를
> 선택하면 됩니다 (내부의 `ProjectSettings/project.json`이 있으면
> 프로젝트로 인식합니다).

### 1.2 첫 씬 저장

1. **File → New Scene** — 빈 씬이 새로 열립니다.
2. **File → Save Scene** — 파일 대화상자가 뜹니다. 기본 폴더가
   `Assets/Scenes`로 열립니다.
3. 파일 이름을 `Level1.nscene` 으로 저장합니다.

창 제목이 `NaraGame - MyGame : Level1` 로 바뀝니다.
씬을 수정하면 `Level1*` 처럼 별표가 붙어 저장이 안 된 상태임을
알려 줍니다.

### 1.3 창 레이아웃 이해

기본 레이아웃은 다음과 같습니다.

| 창 | 역할 |
|---|---|
| **Viewport** (중앙) | 씬을 눈으로 보고, 엔티티를 배치·이동 |
| **Hierarchy** (좌) | 씬에 존재하는 엔티티 트리 |
| **Inspector** (우) | 선택된 엔티티/자산의 속성 편집 |
| **Project** (하) | Assets 폴더 브라우저 |
| **Console** (하) | 스크립트 `print()`, 애니메이션 이벤트 로그, REPL |

배치가 흐트러졌으면 **Window → Reset Layout** 으로 초기화합니다.

### 1.4 이 튜토리얼에서 쓸 정렬 레이어(Sorting Layers) 만들기

1. **Window → Sorting Layers...** — 모달 창 `Sorting Layers`가
   뜹니다. 기본값으로 `Background`, `Default`, `Foreground`, `UI`
   4개가 있습니다.
2. 그대로 두고 창을 닫아도 되고, 필요하면 `+` 로 레이어를 추가해
   위/아래 순서를 조정할 수 있습니다. **위쪽 레이어일수록 뒤쪽에
   렌더링됩니다** (배경 → 지면 → 전경 순서를 원한다면
   `Background → Default → Foreground` 그대로 사용).

---

## 2. 배경(단색) + 메인 카메라 배치

### 2.1 카메라 엔티티 만들기

1. **Hierarchy** 창 상단의 **+ Entity** 버튼을 누릅니다. 트리에
   `Entity`가 하나 추가됩니다.
2. Hierarchy에서 방금 추가한 `Entity`를 클릭해 선택하면
   **Inspector**에 편집 UI가 뜹니다.
3. Inspector 최상단의 **Name** 입력란에 `MainCamera` 를 입력합니다.
4. Inspector 하단의 **Add Component...** 버튼을 누릅니다. 뜨는
   팝업에서 **Camera** 를 선택합니다.
5. Camera 컴포넌트 필드:
   * **Zoom (px/unit)**: `48` (기본값, 1 world unit = 48 픽셀).
   * **Priority**: `0` (이 값이 큰 카메라가 활성 카메라가 됩니다).

### 2.2 배경(단색) 설정 방식

이 엔진의 `Camera` 컴포넌트에는 `clearColor` 같은 명시적 필드가
없습니다. 뷰포트 배경색은 에디터가 자동으로 처리하므로, 게임 내
"배경"은 뒤에서 스프라이트로 깔아 줄 것입니다 (섹션 13에서 패럴랙스
배경으로 대체).

지금은 카메라만 배치해 두고 다음 단계로 갑니다.

### 2.3 좌표계 이해

* +X = 오른쪽, **+Y = 위쪽** (이 플랫포머에서 중력은 -Y).
* 카메라 엔티티의 **Transform.Position** 이 곧 화면 중심입니다.
* 픽셀당 월드 단위 환산은 카메라 `Zoom` (기본 48 px/unit)에 의해
  결정됩니다. 32x32 픽셀 타일이라면 대략 `32 / 48 ≈ 0.67` 월드
  유닛 크기가 됩니다.

---

## 3. 지면 만들기 — Tileset 편집기 + Tilemap 페인트

플랫포머의 지면은 Tilemap 컴포넌트로 그립니다. 순서는
**(a) 타일 이미지를 임포트 → (b) Tileset 자산 만들고 각 셀의
콜라이더 형태 지정 → (c) 씬에 Tilemap 엔티티 배치 → (d) Tile Palette
로 그리기** 입니다.

### 3.1 타일 이미지를 Assets에 넣기

Windows 탐색기로 `MyGame/Assets/Tilesets/` 폴더에 `tiles.png`
파일을 복사해 넣습니다. 에디터의 파일 워처가 약 1초 안에
자동으로 감지해 **Project** 창 트리에 새 항목이 나타납니다.

### 3.2 텍스처 임포트 설정

1. **Project** 창에서 `Tilesets/tiles.png` 를 한 번 클릭해 선택.
2. **Inspector**에 텍스처 임포트 설정이 뜹니다.
   * **Pixels Per Unit**: `48` (카메라 Zoom과 맞춰 두면 1 unit =
     한 타일이 편합니다. 32x32 타일이라면 `32`로 지정해도 됩니다)
   * **Filter**: `Point` (픽셀 아트가 흐릿해지지 않게)
   * **Wrap**: `Clamp`
   * **Alpha is Transparency**: 체크
   * **Border (L,R,T,B)**: 0, 0, 0, 0 (Sliced/Tiled 렌더링에 쓰이는
     9-슬라이스 보더인데 타일셋은 필요 없음)

### 3.3 Tileset 자산 만들기

1. **File → New Tileset...** 을 클릭합니다.
2. 파일 대화상자가 `Assets/Tilesets`로 열립니다. 이름을
   `GroundTiles.ntileset` 으로 저장합니다.
3. 자동으로 **Tileset Editor** 창이 열립니다.
4. Tileset Editor의 필드:
   * **Texture**: 콤보에서 `tiles.png` 선택.
   * **Cell Pixels (W,H)**: 원본 타일 셀 크기를 입력 (예: `16, 16`
     또는 `32, 32`).
5. 프리뷰에 격자가 뜹니다. 셀을 클릭할 때마다 콜라이더 상태가
   다음 순서로 순환합니다.

   `빈 셀 → Solid Box → OneWay → Slope R 45° → Slope L 45° →
   Slope R 22.5° → Slope L 22.5° → Trigger → 빈 셀`

   * **Solid Box** (빨강): 사방이 막힌 지면 블록. 플레이어가 위/옆에
     설 수 있는 벽/땅.
   * **OneWay** (주황): "점프해서 위로 통과 가능한 발판". 아래에서
     위로 뚫고 올라올 수 있고, 위에서는 착지합니다. 구름 발판.
   * **Slope**: 경사 지면. 완만한 언덕 만들 때 사용.
   * **Trigger** (파랑): 통과 가능하지만 접촉 이벤트 발생 (예: 함정
     영역).

6. 지면·벽으로 쓸 셀들은 **Solid Box** 로 지정합니다. 구름 발판이
   있다면 **OneWay** 로 지정. 이 설정은 자동으로 `.ntileset`에
   즉시 저장됩니다.

### 3.4 씬에 Tilemap 엔티티 배치

1. Hierarchy에서 **+ Entity** → 새 엔티티 생성 → Inspector Name을
   `Ground` 로 변경.
2. Inspector → **Add Component...** → **Tilemap** 선택.
3. Tilemap 컴포넌트 필드:
   * **Tileset**: 콤보에서 `GroundTiles.ntileset` 선택.
   * **Cell Pixels (W,H)**: 텍스처와 같은 값 (예: 16, 16).
   * **Sorting Layer**: `Default`
   * **Order in Layer**: `0`
   * **Collision Layer**: `Default`

### 3.5 Tile Palette 로 지면 그리기

`Ground` 엔티티가 선택된 상태에서:

1. 별도로 **Tile Palette** 창이 자동으로 뜹니다 (안 뜨면 Tilemap이
   활성화된 상태인지 확인).
2. 툴바에서 **Brush** 버튼 선택.
3. 하단 텍스처 프리뷰에서 그릴 타일 셀을 클릭. 여러 셀을 드래그로
   선택하면 스탬프 크기가 커집니다.
4. 뷰포트로 마우스를 옮기면 격자에 스냅된 커서가 나옵니다.
   왼쪽 클릭·드래그로 타일을 페인트합니다.
5. **Eraser** 로 지우기, **Rect** 로 사각형 채우기, **Bucket** 으로
   같은 영역 채우기가 됩니다.
6. **Show collision** 을 체크하면 어떤 셀이 Solid/OneWay/Slope 인지
   색으로 확인할 수 있습니다.

지면·플랫폼 몇 줄과 벽 몇 개를 그려 두세요. 20~30 셀 길이의 지면과
공중 발판 두어 개면 충분합니다.

`Ctrl+S` 대신 **File → Save Scene** 으로 저장합니다.

---

## 4. 플레이어 캐릭터 만들기 — 스프라이트 임포트 & 슬라이싱

### 4.1 스프라이트 시트 임포트

Windows 탐색기로 `Assets/Sprites/` 에 `player.png` 를 복사합니다.

**Project → Sprites/player.png** 를 클릭 → Inspector에서:

* **Pixels Per Unit**: `48` (카메라 Zoom과 동일)
* **Pivot**: `BottomLeft` (플랫포머는 발밑 기준이 편함).
  * **Custom** 을 골라 `Custom Pivot (0.5, 0.0)` 처럼 지정하면
    "가로 중심 + 발밑"이 됩니다.
* **Filter**: `Point`
* **Wrap**: `Clamp`
* **Alpha is Transparency**: 체크

### 4.2 Sprite Editor 로 스프라이트 시트 자르기

Project 창에서 `player.png` 를 **더블 클릭** — `Sprite Editor` 창이
열립니다.

1. 상단 콤보 `##slicemode` 에서 슬라이싱 방식을 선택.
   * **By Cell Size**: `Cell Width` / `Cell Height` 를 픽셀로 지정.
     예: 16x16 스프라이트라면 `16`, `16`. 그리고 **Slice** 클릭.
   * **By Cell Count**: 가로 프레임 수 `Columns`, 세로 `Rows` 를
     지정. 예: 가로 8프레임 시트면 `8`, `1` → **Slice**.
   * **Manual**: 프리뷰에서 드래그해 사각형을 직접 만듭니다.
2. Slice 후 오른쪽 리스트 `##sublist` 에 서브스프라이트가 채워집니다.
   기본 이름은 `player_0`, `player_1`, ... 입니다.
3. 각 서브스프라이트를 클릭해 이름을 의미 있게 바꾸는 것을 권장합니다.
   예: `idle_0`, `idle_1`, `run_0`, `run_1`, `run_2`, `run_3`,
   `jump_0`. 서브스프라이트 이름 InputText 필드로 편집합니다.
4. 필요하면 **pivot** 을 서브스프라이트별로 미세 조정합니다.
5. 창을 닫습니다 (자동 저장).

### 4.3 플레이어 엔티티 만들기

1. Hierarchy에서 **+ Entity** → 이름을 `Player` 로 변경.
2. Inspector → **Add Component...** → **SpriteRenderer** 선택.
3. SpriteRenderer 필드:
   * **Sprite**: 콤보에서 `player.png` 선택.
   * **Sub-sprite**: 콤보에서 `idle_0` 선택 (기본 idle 프레임).
   * **Tint**: 흰색 (RGBA 1,1,1,1)
   * **Draw Mode**: `Simple`
   * **Sorting Layer**: `Default`
   * **Order in Layer**: `10` (지면 tilemap보다 위로)
4. Transform 컴포넌트에서:
   * **Position**: 지면 위쪽으로 (예: 5, 3, 0)
   * **Scale**: 1, 1, 1

**File → Save Scene** 으로 저장하고 뷰포트에서 플레이어가 지면 위에
보이는지 확인합니다.

---

## 5. 플레이어 물리 컴포넌트 붙이기

플랫포머에서 반드시 필요한 두 컴포넌트는 **Rigidbody2D** (중력, 속도)
와 **BoxCollider** (충돌 모양) 입니다.

### 5.1 Rigidbody2D 추가

Player 엔티티 선택 → Inspector → **Add Component...** →
**Rigidbody2D** 선택.

* **Velocity**: 0, 0 (매 프레임 스크립트가 덮어씀)
* **Gravity**: `0, -30` — Y가 음수여야 아래로 떨어집니다. 값이
  클수록 무거운 느낌.
* **Drag**: `0` (공기 저항. 0으로 두면 관성 유지)
* **Mass**: `1`
* **Restitution**: `0` (튀기지 않음)
* **Friction**: `0.2` (지면 마찰. 너무 크면 미끄러지지 않고 갑자기
  멈춤)

### 5.2 BoxCollider 추가

Inspector → **Add Component...** → **BoxCollider**.

* **Size**: 스프라이트 크기에 맞춥니다. 16x16 스프라이트, Pixels Per
  Unit=48 이면 대략 `0.33, 0.33`. 32x32 스프라이트에 Pixels Per Unit
  =48 이면 약 `0.67, 0.67`. 필요에 따라 조금 줄여서 판정 여유를
  줍니다 (예: 캐릭터 시각 크기의 80%).
* **Offset**: 스프라이트 pivot에 따라 조정. BottomLeft pivot에 세로가
  0.5 유닛인 경우 `(0.25, 0.25)` 처럼 중심을 몸통으로 옮깁니다.
* **Is Trigger**: **체크 해제 상태 유지 (false)**. 이게 기본값이라
  새로 추가한 BoxCollider 는 곧바로 실체 콜라이더로 작동합니다.
  이 값이 true 인 경우 발판이 "통과"돼 캐릭터가 지면 아래로
  떨어지므로, 지면·플랫폼·플레이어 몸통의 BoxCollider 는 반드시
  false 여야 합니다. 코인·데미지 존 같이 통과하되 이벤트만
  받고 싶은 경우에만 true 로 켭니다 (11 장 참조).

* **Layer**: `Default` (또는 `Player` 로 별도 관리)

### 5.3 Tilemap 콜라이더 확인

큰 단일 지면을 BoxCollider로 대신 시험할 때는 콜라이더 엔티티의
Transform Scale을 `(1,1)`로 유지하고 Collider Size에 실제 월드 크기를
입력하십시오. 장식 스프라이트를 크게 늘려야 한다면 `GroundVisual`과
물리용 `Ground`를 분리하면 충돌 응답을 안정적으로 확인할 수 있습니다.

3.5절에서 그린 지면 셀들은 Tileset 편집기에서 **Solid Box** 로
지정된 경우 자동으로 콜라이더가 됩니다. 이는 스크립트 없이도 물리
엔진이 해당 셀 위에 다른 collider(플레이어)의 착지를 처리해 줍니다.
(내부적으로 물리 서브스텝 4단계로 tunneling이 방지됩니다.)

이 상태에서 뷰포트 툴바의 **Play** 체크박스를 한 번 켜 봅니다 (섹션
16 참조). 플레이어가 지면 위로 떨어져 착지하면 물리 세팅이 정상입니다.
아래로 뚫고 지나가면 3.3 을 다시 확인하고, 5.2 의 BoxCollider 에서
Is Trigger 가 실수로 켜져 있지 않은지 확인하세요.
**Play** 를 다시 끄면 원래 위치로 돌아옵니다.

---

## 6. 애니메이션 만들기 (Idle / Run / Jump)

### 6.1 Idle 애니메이션

1. **File → New Animation...** → 파일 대화상자에서
   `Assets/Animations/Idle.nanim` 로 저장.
2. 자동으로 **Animation Editor** 창이 열립니다.
3. 상단:
   * **Texture**: `player.png` 선택.
   * **Loop**: 체크.
4. **Frames** 섹션:
   * **+ Frame** 버튼을 눌러 프레임 행을 추가합니다.
   * 각 행의 `##sprite` 콤보에서 `idle_0` 선택, `##dur` 에 지속시간
     (예: `0.25` 초).
   * idle이 2프레임이면 프레임 2개 추가 (`idle_0` 0.25s, `idle_1`
     0.25s).
5. 창을 닫습니다.

### 6.2 Run 애니메이션

같은 방식으로 `Assets/Animations/Run.nanim` 을 만들고:

* Texture: `player.png`
* Loop: 체크
* 프레임: `run_0` ~ `run_3` 각 0.08~0.12초.

### 6.3 Jump 애니메이션

`Assets/Animations/Jump.nanim`:

* Texture: `player.png`
* Loop: **해제** (점프 프레임은 한 번만 재생).
* 프레임: `jump_0` (0.5s) 하나만.

---

## 7. Animator Controller 만들기 (상태 그래프)

Animator Controller는 조건부로 애니메이션 상태를 전이시키는
상태 머신입니다.

### 7.1 컨트롤러 자산 만들기

1. **File → New Animator Controller...** →
   `Assets/Animations/PlayerAC.ncontroller` 로 저장.
2. **Animator Controller** 창이 열립니다.

### 7.2 상태 3개 추가

**States** 섹션에서 **+ State** 를 3번 눌러 행 3개 생성.

| # | ##name | ##anim | ##speed |
|---|---|---|---|
| 1 | `Idle` | `Idle.nanim` | 1.0x |
| 2 | `Run` | `Run.nanim` | 1.0x |
| 3 | `Jump` | `Jump.nanim` | 1.0x |

### 7.3 기본 상태 지정

상단의 **Default State** 콤보에서 `Idle` 선택.

### 7.4 파라미터 3개 추가

**Parameters** 섹션에서 **+ Param** 을 3번 눌러 행 3개를 만들고,
왼쪽 `##pname` 필드에 이름을, 옆의 `##ptype` 드롭다운에서 타입을
지정합니다.

| ##pname | ##ptype | 기본값 |
|---|---|---|
| `speed` | `Float` | `0.00` |
| `grounded` | `Bool` | `false` (체크 해제) |
| `Jump` | `Trigger` | (auto-reset -- 별도 값 없음) |

⚠️ 파라미터 이름은 **대소문자를 구분**합니다. `Jump` 와 `jump` 는
서로 다른 파라미터로 취급되고, 뒤에서 만들 Lua 스크립트가
`animator.set_trigger(self, "Jump")` 로 발동하므로 반드시 대문자로
시작하는 `Jump` 여야 합니다.

### 7.5 전이(Transition) 만들기

두 가지 방법으로 전이를 추가할 수 있습니다:

* **그래프에서**: 상태 그래프의 노드를 우클릭 →
  **Add Transition From Here** → 마우스로 도착 노드를 클릭. 화살표가
  생성됩니다. 시작이 특정 상태인 경우에 편리합니다.
* **Transitions 섹션에서 `+ Transition` 버튼**: `From = Any` 로 새 행이
  생깁니다. `Any → X` 처럼 그래프에 시작 노드가 없는 전이는 이 쪽이
  편합니다.

행이 만들어지면 그 행의 콤보들을 채워 넣습니다. 설정할 4개 전이:

1. **Idle → Run**: `speed` `>` `0.1`
2. **Run → Idle**: `speed` `<` `0.1`
3. **Any → Jump**: 파라미터 `Jump` (**From** 콤보 = `Any`,
   **To** = `Jump`)
4. **Jump → Idle**: `grounded` `==` `true`

각 전이 행의 위젯 구성:

* **From** 콤보 → 시작 상태 (또는 `Any` = 어느 상태에서든).
* **To** 콤보 → 도착 상태.
* **when** 옆의 **파라미터** 콤보 → 7.4에서 만든 파라미터 이름 선택.
  콤보는 파라미터 **타입** 이 아니라 **이름** 을 나열합니다.
  `Jump` (Trigger 타입) 도 이 목록에서 그냥 이름 `Jump` 로
  보입니다.
* 연산자 + 임계값 → 파라미터 타입에 따라 다른 위젯이 나옵니다.
  `Float / Int` 는 `< <= > >= == !=` 6단 콤보 + 숫자,
  `Bool` 은 `== !=` + `True`/`False` 드롭다운,
  `Trigger` 는 값 칸이 `True` 로 잠긴 disabled 드롭다운으로
  표시됩니다 (트리거는 발동 자체가 조건이라 값을 지정할 필요가
  없고, 발동 즉시 auto-reset 됩니다).

⚠️ 한 전이당 조건 파라미터는 **하나** 만 지정할 수 있습니다.
"grounded && speed > 0.1" 같은 AND 조합은 UI에 저장 자리가 없으므로,
필요하다면 Lua 쪽에서 두 조건을 합친 파생 파라미터
(예: `landingWithSpeed`) 를 하나 더 만들어 매 프레임 세팅하는 것이
가장 간단합니다.

창을 닫으면 자동 저장됩니다.

### 7.6 Player 엔티티에 Animator 붙이기

Hierarchy에서 `Player` 선택 → Inspector → **Add Component...** →
**Animator**.

Animator 컴포넌트:
* **Animation**: 비워 두거나 `Idle.nanim` (컨트롤러가 지정되면
  컨트롤러가 관리)
* **Controller**: `PlayerAC.ncontroller`
* **Playing**: 체크

이제 파라미터 값에 따라 자동으로 상태가 바뀝니다. 값을 매 프레임
바꿀 주체는 다음 섹션의 Lua 스크립트입니다.

---

## 8. 키 입력 & 이동 스크립트 (Player.lua)

### 8.1 스크립트 파일 만들기

에디터 안에서 신규 `.lua` 를 만드는 메뉴는 아직 없습니다.
Windows 탐색기로 `Assets/Scripts/` 폴더에 빈 `Player.lua` 파일을
만듭니다 (`New → 텍스트 문서 → 이름을 Player.lua로`).

파일 워처가 감지해 Project 트리에 나타나면 **더블 클릭** —
`Script: Scripts/Player.lua` 창이 뜹니다.

또는 다른 편집기(VS Code 등)로 열어 편집해도 되고, 파일이 바뀌면
편집기에서 **Edit → Reload Scripts** 로 다시 로드할 수 있습니다.

### 8.2 Player.lua 전체 코드

에디터의 스크립트 편집기에 아래 내용을 붙여 넣고 **Save** 를 누릅니다.

```lua
-- Assets/Scripts/Player.lua
-- 클래식 플랫포머 스타일 좌우 이동 + 점프.

local MOVE_SPEED     = 6.0   -- world units / sec
local JUMP_SPEED     = 12.0  -- world units / sec (점프 초기 속도)
-- 가변 점프 높이: 상승 도중에 점프 버튼을 떼면 위쪽
-- 속도를 이 값으로 잘라 낸다. 짧게 누르면 낮게 뛰고, 계속 누르고
-- 있으면 JUMP_SPEED / 중력이 정한 최대 높이까지 뛴다.
local JUMP_CUT_SPEED = 4.0
local COYOTE_TIME    = 0.08  -- 지면 이탈 직후 얼마 동안 점프 허용
local JUMP_BUFFER    = 0.08  -- 착지 직전 눌린 점프 입력 저장 시간

-- entity 이름 → { coyote=<sec>, buffer=<sec>, wasGrounded=<bool> }
-- 여러 Player 인스턴스에 대응하기 위한 per-entity 상태 저장소.
local state = {}

local function s(name)
  local t = state[name]
  if not t then
    t = { coyote = 0, buffer = 0, wasGrounded = false }
    state[name] = t
  end
  return t
end

-- Ground 감지: Player 원점에서 아래로 2 world unit 쏘고, 자기 자신
-- 콜라이더는 exclude 로 걸러낸다. 2 unit 이면 대부분의 캐릭터
-- 콜라이더 (원점 아래 최대 1.5 unit 정도) 를 통과하고도 지면
-- tile 까지 닿을 여유. Ray 길이가 콜라이더 반높이보다 짧으면
-- 지면에 못 닿아 항상 not grounded 로 나오므로 주의.
local function isGrounded(self, py)
  local px, _, _ = transform.get_position(self)
  local hit = scene.raycast(px, py, px, py - 2.0, self)
  return hit ~= ""
end

function Player_Start(self)
  -- 액션 하나에 여러 키를 걸고 싶으면 두 번째 인자를 문자열 대신
  -- 배열 (테이블) 로 넘기면 됩니다. 아래는 A/D + 방향키를 모두
  -- 받도록 한 예시.
  input.bind("left",  {"a", "Left"})
  input.bind("right", {"d", "Right"})
  input.bind("jump",  {"Space", "w", "Up"})
  print("Player_Start: ", self)
end

function Player_Update(self, dt)
  local st = s(self)
  local vx, vy = rigidbody.get_velocity(self)
  local px, py, _ = transform.get_position(self)

  -- 좌우 입력
  local move = 0
  if input.is_action_down("left")  then move = move - 1 end
  if input.is_action_down("right") then move = move + 1 end
  vx = move * MOVE_SPEED

  -- 점프 버퍼: 공중이어도 눌러 두면 착지 시 자동 점프
  if input.is_action_pressed("jump") then
    st.buffer = JUMP_BUFFER
  else
    st.buffer = math.max(0, st.buffer - dt)
  end

  -- Coyote time: 지면 이탈 직후 잠깐 점프 가능
  local grounded = isGrounded(self, py)
  if grounded then
    st.coyote = COYOTE_TIME
  else
    st.coyote = math.max(0, st.coyote - dt)
  end

  if st.buffer > 0 and st.coyote > 0 then
    vy = JUMP_SPEED
    st.buffer = 0
    st.coyote = 0
    audio.play("Sounds/jump.wav", 0.8)
    animator.set_trigger(self, "Jump")
    -- 점프 먼지 (섹션 10에서 JumpDust 엔티티 준비 후 활성화)
    if scene.find("JumpDust") then
      transform.set_position("JumpDust", px, py - 0.25, 0)
      particle.emit("JumpDust", 10)
    end
  end

  -- 가변 점프 높이: 아직 상승 중일 때 점프 버튼을 떼면 위쪽 속도를
  -- JUMP_CUT_SPEED 이하로 잘라 낸다. is_action_released 는 버튼이
  -- 눌렸다가 떨어진 프레임에 딱 한 번 true 를 반환하므로 상승 도중
  -- 여러 번 발동하지 않는다.
  if input.is_action_released("jump") and vy > JUMP_CUT_SPEED then
    vy = JUMP_CUT_SPEED
  end

  rigidbody.set_velocity(self, vx, vy)

  -- 좌우 반전
  if move ~= 0 then
    sprite.set_flip_x(self, move < 0)
  end

  -- Animator 파라미터 업데이트
  animator.set_float(self, "speed", math.abs(vx))
  animator.set_bool(self, "grounded", grounded)

  -- 낙사 판정 (섹션 14): y가 너무 낮으면 씬 재시작
  if py < -10 then
    scene.load("Assets/Scenes/Level1.nscene",
      { fade_out = 0.4, fade_in = 0.4, color = { r=0, g=0, b=0 } })
  end

  st.wasGrounded = grounded
end
```

### 8.3 Player 엔티티에 Script 컴포넌트 붙이기

1. Hierarchy → `Player` 선택 → Inspector → **Add Component...** →
   **Script**.
2. Script 컴포넌트 필드:
   * **Script**: 콤보에서 `Scripts/Player.lua` 선택.
   * **Entry function**: `Player` 입력.
     (비워 두면 `OnStart`/`OnUpdate` 이름을 찾음. `Player`라고
     넣으면 `Player_Start`/`Player_Update` 를 우선 호출합니다.)

### 8.4 키 이름의 함정 (반드시 읽기)

* 알파벳 한 글자와 숫자는 소문자/대문자 상관없이 인식됩니다 (`"a"`,
  `"W"`, `"5"`).
* **여러 글자 키는 PascalCase로만 인식됩니다.** 아래 목록의 정확한
  철자를 사용하세요.

| 정확한 문자열 |
|---|
| `"Space"`, `"Enter"`, `"Return"`, `"Escape"`, `"Esc"`, `"Tab"`, `"Backspace"`, `"Delete"` |
| `"Home"`, `"End"`, `"PageUp"`, `"PageDown"` |
| `"Up"` / `"UpArrow"`, `"Down"` / `"DownArrow"`, `"Left"` / `"LeftArrow"`, `"Right"` / `"RightArrow"` |
| `"LeftShift"`, `"RightShift"`, `"LeftCtrl"`, `"RightCtrl"`, `"LeftAlt"`, `"RightAlt"` |

`"space"` (소문자) 나 `"SPACE"` (전부 대문자) 는 인식되지 않아
`is_key_pressed` 가 항상 `false` 를 반환합니다. F-키(F1~F12), 물결(`~`),
기호 키는 지원되지 않습니다.

### 8.5 실행해 보기

뷰포트 툴바의 **Play** 체크박스를 켭니다. `a`/`d`로 좌우 이동, `Space`로
점프가 됩니다. Animator가 상태 그래프에 따라 Idle ↔ Run ↔ Jump 를
자동으로 전환합니다.

**Play** 를 끄면 씬은 저장된 초기 상태로 되돌아갑니다.

---

## 9. 카메라가 플레이어를 따라가게 하기

Camera 컴포넌트에는 built-in follow target 필드가 없으므로 스크립트로
카메라 엔티티의 Transform을 매 프레임 옮깁니다.

### 9.1 Camera.lua 만들기

`Assets/Scripts/Camera.lua`:

```lua
-- Assets/Scripts/Camera.lua
-- MainCamera가 Player를 부드럽게 따라가게 한다.
-- LERP factor로 지연 팔로우, Y 축은 지면 근처에서 살짝 위에 유지.
-- STAGE_* 로 카메라가 스테이지 밖 (검은 배경 / 낙사 영역) 을 보여
-- 주지 못하도록 목표 위치를 클램프한다.

local TARGET      = "Player"
local FOLLOW_LERP = 8.0    -- 초당 접근 비율 (크면 즉시, 작으면 지연)
local Y_OFFSET    = 1.5

-- 스테이지의 world-unit 경계. 자기 레벨 크기에 맞춰 조정.
local STAGE_MIN_X = -18.0
local STAGE_MAX_X =  40.0
local STAGE_MIN_Y = -6.0
local STAGE_MAX_Y =  20.0
-- 카메라가 보여 주는 반너비 / 반높이 (world unit).
-- 계산: (viewport pixel size / 2) / Camera.zoom (px per unit).
-- 예) 960×540 창 + zoom 48 → half_w = 480/48 = 10, half_h ≈ 5.6.
-- 창 크기나 카메라 zoom 을 바꾸면 여기도 같이 조정.
local VIEW_HALF_W = 10.0
local VIEW_HALF_H = 5.6

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

function Camera_Update(self, dt)
  if not scene.find(TARGET) then return end
  local tx, ty, _ = transform.get_position(TARGET)
  local cx, cy, _ = transform.get_position(self)

  -- 이번 프레임의 원하는 카메라 위치 (Player 기준).
  local targetX = tx
  local targetY = ty + Y_OFFSET

  -- 스테이지 안쪽 (뷰포트 반너비만큼 안으로 밀어 준 유효 영역) 에
  -- 목표를 클램프. 스테이지가 뷰보다 좁으면 lo > hi 가 되어 clamp
  -- 가 항상 lo 를 반환 → 카메라가 스테이지 왼쪽 / 아래쪽 안쪽
  -- 경계에 붙어 고정된다.
  targetX = clamp(targetX, STAGE_MIN_X + VIEW_HALF_W,
                            STAGE_MAX_X - VIEW_HALF_W)
  targetY = clamp(targetY, STAGE_MIN_Y + VIEW_HALF_H,
                            STAGE_MAX_Y - VIEW_HALF_H)

  local t = math.clamp(FOLLOW_LERP * dt, 0, 1)
  local nx = math.lerp(cx, targetX, t)
  local ny = math.lerp(cy, targetY, t)
  transform.set_position(self, nx, ny, 0)
end
```

### 9.2 MainCamera 엔티티에 붙이기

`MainCamera` 엔티티 → **Add Component...** → **Script**.

* **Script**: `Scripts/Camera.lua`
* **Entry function**: `Camera`

### 9.3 재실행

**Play** 를 다시 켭니다. 플레이어를 좌우로 움직이면 카메라가 부드럽게
따라옵니다.

---

## 10. 파티클 이펙트 (점프 먼지 + 코인 반짝임)

### 10.1 JumpDust 엔티티

1. Hierarchy → **+ Entity** → 이름 `JumpDust`.
2. Transform.Position 은 화면 밖 (예: -100, -100, 0). 스크립트가
   점프할 때마다 플레이어 발밑으로 옮깁니다.
3. **Add Component...** → **ParticleSystem**.
4. ParticleSystem 필드:
   * **Max Particles**: `64`
   * **Emission Rate**: `0` (자동 방출 안 함. 스크립트에서 burst)
   * **Lifetime (min/max)**: `0.3 / 0.5`
   * **Start Speed (min/max)**: `1.5 / 3.0`
   * **Start Size (min/max)**: `0.06 / 0.10`
   * **Size over Lifetime**: 체크 → **End Size (min/max)**: `0.0 / 0.02`
   * **Direction (deg, min/max)**: `60 / 120` (위쪽 부채꼴)
   * **Start Color**: 밝은 갈색 RGBA `(0.8, 0.7, 0.5, 1.0)`
   * **End Color**: `(0.8, 0.7, 0.5, 0.0)` (알파 페이드아웃)
   * **Gravity**: `0, -4` (자체 중력)
   * **Emission Shape**: `Point`
   * **Play On Start**: 해제
   * **Loop**: 해제
   * **Duration (s)**: `1.0`

### 10.2 트리거

`Player.lua` 의 점프 블록에 이미 아래 코드가 들어 있습니다.

```lua
if scene.find("JumpDust") then
  transform.set_position("JumpDust", px, py - 0.25, 0)
  particle.emit("JumpDust", 10)
end
```

`particle.emit(entity, count)` 은 자동 emission rate와 무관하게 즉시
`count` 개의 파티클을 burst 방출합니다.

### 10.3 코인 반짝임 (선택)

코인이 회전하는 느낌을 파티클로 낼 수도 있지만, 이 튜토리얼에서는
코인은 스프라이트로만 두고 다음 섹션의 수집 이벤트 때 SFX 를 재생합니다.

---

## 11. 코인 수집 + 트리거 이벤트

### 11.1 Coin 엔티티

1. `Assets/Sprites/coin.png` 를 임포트 (Pivot: Center, Filter: Point).
2. Hierarchy → **+ Entity** → 이름 `Coin1` → 배치 (예: 지면 위 좀
   떨어진 자리).
3. **Add Component...** → **SpriteRenderer**:
   * Sprite: `coin.png`, Order in Layer: `5`
4. **Add Component...** → **BoxCollider**:
   * Size: `0.3, 0.3`
   * **Is Trigger**: **체크** (수집용은 통과 가능한 트리거)
   * Layer: `Default`
5. Coin이 여러 개 필요하면 Hierarchy에서 우클릭 → **Duplicate** →
   Inspector에서 Position만 바꿉니다.

### 11.2 Coin.lua

`Assets/Scripts/Coin.lua`:

```lua
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
```

각 코인 엔티티(`Coin1`, `Coin2`, ...)에:

* **Add Component...** → **Script**
* Script: `Scripts/Coin.lua`
* Entry function: `Coin`

### 11.3 트리거 vs 충돌 규칙

* 양쪽 BoxCollider의 `Is Trigger` 가 **모두 false** 인 경우 →
  물리적 충돌 + `OnCollisionEnter/Stay/Exit`.
* 한쪽이라도 `Is Trigger = true` → 통과 + `OnTriggerEnter/Stay/Exit`
  이벤트만.

따라서 지면·플랫폼·플레이어 몸통은 `Is Trigger = false`, 코인·수집
아이템·데미지 존은 `Is Trigger = true` 로 세팅합니다.

### 11.4 전역 상태와 저장

`state` 는 전역 Lua 테이블입니다. `savegame.save("slot1")` 로 JSON 파일에
저장, `savegame.load("slot1")` 로 복원할 수 있습니다. 슬롯 파일은
`saves/slot1.json` 에 저장됩니다.

---

## 12. 배경 음악 + 효과음

### 12.1 배경 음악 엔티티

1. `Assets/` 아래 `Audio/` 폴더를 하나 만들고 (또는 아무 곳이나)
   `bgm.wav`, `jump.wav`, `coin.wav` 를 넣습니다.
   (튜토리얼 코드는 `Sounds/jump.wav` 로 참조했으므로, `Sounds`
   폴더를 만들어 넣거나 코드의 경로를 실제 경로로 맞춰 주세요.)
2. Hierarchy → **+ Entity** → 이름 `BGM`.
3. **Add Component...** → **AudioSource**.
4. AudioSource 필드:
   * **Clip**: `bgm.wav`
   * **Volume**: `0.5`
   * **Play On Start**: 체크
   * **Loop (streams via PlayMusic)**: **체크** (이 체크가 `PlayMusic`
     경로 = 스트리밍 재생을 활성화합니다. 짧은 SFX는 체크 해제)
   * **3D Positional**: 해제

### 12.2 SFX 는 스크립트에서

* `audio.play("Sounds/jump.wav", 0.8)` — 단발성 SFX
* `audio.play("Sounds/coin.wav", 0.8)` — 위 Coin.lua 참조.
* 볼륨 조절: `audio.set_bus_volume("sfx", 0.6)`,
  `audio.set_bus_volume("music", 0.4)`.

경로는 절대 경로 / `Assets/` 상대 경로 / 36자 GUID 셋 다 받습니다.

---

## 13. 배경 패럴랙스 (여러 레이어)

패럴랙스는 built-in 컴포넌트가 없으므로 커스텀 Lua 컴포넌트로
만듭니다.

### 13.1 배경 스프라이트 세 장 배치

1. `sky.png`, `mountain.png`, `trees.png` 를 `Assets/Sprites/` 에
   넣습니다.
2. 각각 Project 창에서 선택 → Inspector에서
   `Pixels Per Unit=48`, `Wrap = Repeat`, `Filter = Point` 로 설정.
3. Hierarchy에서 **+ Entity** 로 세 개 만들기:
   * `BG_Sky` — SpriteRenderer → Sprite: sky.png,
     Sorting Layer: `Background`, Order in Layer: 0
   * `BG_Mountains` — SpriteRenderer → Sprite: mountain.png,
     Sorting Layer: `Background`, Order in Layer: 1
   * `BG_Trees` — SpriteRenderer → Sprite: trees.png,
     Sorting Layer: `Background`, Order in Layer: 2
4. 각각의 Transform.Position 을 카메라 근처(예: 0, 3, 0)에 배치.

`Background` 레이어를 만들지 않았으면 **Window → Sorting Layers...**
에서 추가하고 Default 위에 배치합니다.

### 13.2 Parallax LuaComponent 등록 + 스크립트

`Assets/Scripts/Parallax.lua`:

```lua
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
```

### 13.3 각 배경에 Parallax 붙이기

세 배경 엔티티(`BG_Sky`, `BG_Mountains`, `BG_Trees`)에:

* **Add Component...** → **Script**
  * Script: `Scripts/Parallax.lua`
  * Entry function: `Parallax`
* **Add Component...** → **Parallax** (팝업 검색에서 `Parallax`
  — 등록된 커스텀 컴포넌트가 나타남).
  * 인스펙터에 `factor`, `origin_x`, `origin_y` 세 필드가 노출됩니다.

각 배경에 factor 를 다르게:

| 엔티티 | factor | 효과 |
|---|---|---|
| `BG_Sky` | `0.1` | 거의 정지, 아주 멀리 있는 하늘 |
| `BG_Mountains` | `0.4` | 중간 거리 |
| `BG_Trees` | `0.8` | 앞쪽 나무, 지면과 비슷하게 움직임 |

### 13.4 커스텀 컴포넌트 편집 팁

`component.get(entity, type)` 은 **테이블 복사본**을 반환합니다.
값을 바꾼 뒤에는 반드시 `component.set(entity, type, tbl)` 로 다시
저장해야 인스턴스에 반영됩니다.

Inspector에서 편집한 값은 씬 저장 시 유지됩니다.

---

## 14. 게임 오버 & 씬 재시작

`Player.lua` 안에 이미 낙사 판정이 들어 있습니다.

```lua
if py < -10 then
  scene.load("Assets/Scenes/Level1.nscene",
    { fade_out = 0.4, fade_in = 0.4, color = { r=0, g=0, b=0 } })
end
```

* `scene.load(path, opts)` 로 씬 전환. `opts` 의 `fade_out`/`fade_in`
  으로 검은색 페이드를 자동으로 그립니다.
* 같은 씬을 다시 로드하면 초기 상태로 리셋됩니다.
* 여러 스테이지가 있으면 골인 지점에 `Goal` 이름의 트리거 엔티티를
  만들고 `Player_TriggerEnter` 에서:

  ```lua
  if other == "Goal" then
    scene.load("Assets/Scenes/Level2.nscene",
      { fade_out = 0.5, fade_in = 0.5, color = { r=0, g=0, b=0 } })
  end
  ```

---

## 15. 스플래시 화면

게임 시작 시 로고를 잠깐 보여 주려면 별도의 `Splash.nscene` 씬을
만들고 `SplashScreenComponent` 를 사용합니다.

### 15.1 Splash 씬 만들기

1. **File → New Scene** → 저장:
   `Assets/Scenes/Splash.nscene`.
2. `Assets/Sprites/logo.png` 를 임포트 (없으면 아무 텍스처).
3. Hierarchy → **+ Entity** → 이름 `Splash`.
4. **Add Component...** → **SplashScreen** — Inspector:
   * **Texture**: `logo.png` 를 Project 창에서 드래그해
     `Drop texture here` 슬롯 위에 떨어뜨립니다 (또는 우측 위 콤보로
     선택).
   * **Next scene**: `Assets/Scenes/Level1.nscene`
   * **Fade in**: `0.5`
   * **Hold**: `1.5`
   * **Fade out**: `0.5`
   * **Priority**: `0`
   * **Background**: 검정 (0,0,0,1)
   * **Tint**: 흰색 (1,1,1,1)
5. 이 씬에도 **Camera** 를 하나 만들어 두는 편이 안전합니다.
6. **File → Save Scene**.

### 15.2 메인 씬 지정

`Splash.nscene` 이 열려 있는 상태에서
**File → Set Current Scene as Main** 을 클릭.
`ProjectSettings/project.json` 의 `mainScene` 이 이 씬으로 지정됩니다.

빌드된 게임이 부팅되면 Splash → (2.5초 후 자동) → Level1 순서로
전환됩니다.

---

## 16. 게임 빌드 & 실행

### 16.1 에디터 내 미리보기 (Play)

**Viewport** 창 상단 툴바의 **Play** 체크박스를 켭니다.

* 체크: 스크립트/물리/애니메이션/파티클/스플래시가 모두 tick
  됩니다. 마우스 클릭·이동으로 엔티티 편집은 여전히 가능.
* 해제: 씬이 초기 상태로 되돌아갑니다.

### 16.2 스탠드얼론 실행 (에디터 없이)

**File → Build...** 을 클릭하고 타겟 플랫폼을 선택한 다음 → 폴더 피커에서 **빈 폴더** 를
선택합니다. 빌드가 완료되면 그 폴더에:

```
<빌드폴더>/
  game.exe               -- 실행 파일 (NaraGame.exe 복제)
  assets.pak             -- Assets/ 를 묶은 팩 파일
  ProjectSettings/       -- 프로젝트 설정 사본
```

`game.exe` 를 더블 클릭하면 스탠드얼론 모드로 실행되어 mainScene 이
바로 뜹니다. UI 도킹/메뉴/그리드 없이 뷰포트만 borderless로 렌더
됩니다.

### 16.3 명령줄 실행

에디터를 특정 씬으로 스탠드얼론 시작:

```
NaraGame.exe --project "C:\path\to\MyGame" --scene "Assets/Scenes/Level1.nscene"
```

---

## 17. 자주 하는 실수 요약

이 튜토리얼을 따라 하면서 흔히 마주치는 실수와 원인:

| 증상 | 원인 | 해결 |
|---|---|---|
| 플레이어가 지면 아래로 계속 떨어짐 | 지면 또는 플레이어 `BoxCollider.Is Trigger` 가 실수로 켜져 있음 | 두 콜라이더 모두 `Is Trigger` 체크 해제 (기본값 false) |
| `Space` 키가 안 먹힘 | 키 이름 `"space"` (소문자) 사용 | `"Space"` (PascalCase) 로 변경 |
| 점프가 즉시 두 번씩 | `is_action_down` 을 점프에 사용 | `is_action_pressed` (한 프레임만 true) 사용 |
| Player가 벽을 뚫고 지나감 | 매 프레임 `transform.set_position` 으로 직접 이동 | `rigidbody.set_velocity` 를 쓰고 물리에 맡김 |
| 스프라이트가 안 보임 | `Sorting Layer` 가 카메라 앞 레이어가 아님 / 스프라이트 슬롯 비어 있음 | Sorting Layer 순서 확인, Sprite 콤보 확인 |
| 애니메이션이 재생 안 됨 | Animator `Playing` 체크 해제 / Controller 미지정 | 두 항목 확인 |
| Animator가 상태 전이를 안 함 | Parameters 값을 스크립트에서 안 씀 | `animator.set_float("Player", "speed", ...)` 확인 |
| 카메라가 안 움직임 | 활성 카메라가 다른 엔티티 | `camera.get_active()` 로 이름 확인, Priority 조정 |
| `component.get` 값을 바꿔도 반영 안 됨 | 반환값은 복사본 | 바꾼 뒤 `component.set` 로 다시 저장 |
| `rigidbody.add_force` 를 매 프레임 불렀는데 캐릭터가 광속 | `add_force` 는 사실 impulse (즉시 속도에 더함) | 지속 힘은 `set_gravity` 나 매 프레임 `set_velocity` 조정 |
| BGM 이 재생 시마다 새로 로드돼 렉 | AudioSource `Loop` 체크 안 함 | `Loop (streams via PlayMusic)` 체크 |
| 스크립트 바꿨는데 반영 안 됨 | 파일만 저장하고 리로드 안 함 | 스크립트 편집기의 **Save** 또는 **Edit → Reload Scripts** |
| Tilemap 셀에 콜라이더 없음 | Tileset 편집기에서 셀 클릭 안 함 (기본은 빈 셀) | Tileset Editor 에서 각 셀 클릭하여 Solid Box 로 지정 |
| 배경이 카메라와 함께 안 움직임 | Parallax factor = 0 | 0.1~0.9 로 조정, `origin_x/y` 도 확인 |

---

## 18. 적 만들기 — 순찰 · 발견 · 추적, 그리고 밟기

버섯 요정 모양의 적 `Mossling`을 만듭니다. 동작은 세 단계입니다:

1. **순찰**: 시작 위치를 중심으로 좌우를 일정 거리 왕복합니다.
2. **발견**: 플레이어가 탐지 범위 안에 들어오고 **시선이 닿으면**
   (중간에 벽·지면이 없으면) 추적 모드로 바뀝니다.
3. **추적·공격**: 플레이어 쪽으로 빠르게 다가가고, 몸통이 닿으면
   플레이어가 피해를 입습니다(접촉 공격). 단, 플레이어가 **위에서
   밟으면** 반대로 적이 사라지면서 점수가 오르고 코인이 나옵니다.

### 18.1 Mossling 엔티티 만들기

1. `Assets/Sprites/mossling.png` 를 임포트합니다 (Pivot: Center,
   Filter: Point). 걷기 2프레임짜리 가로 시트를 권장합니다.
2. Project 창에서 더블 클릭 → Sprite Editor 에서 **By Cell Count**
   `2 x 1` 로 슬라이스하고 이름을 `walk_0`, `walk_1` 로 바꿉니다.
3. Hierarchy → **+ Entity** → 이름 `Mossling1` → 지면 위에 배치.
4. **Add Component...** → **SpriteRenderer**:
   * Sprite: `mossling.png`, Sub-sprite: `walk_0`
   * Sorting Layer: `Default`, Order in Layer: `9` (플레이어보다 아래)
5. **Add Component...** → **Rigidbody2D**: 플레이어와 같은 동적
   바디 설정 (중력 적용).
6. **Add Component...** → **BoxCollider**:
   * Size: 몸통에 맞게 (예: `0.7, 0.6`), **Is Trigger: 해제**
   * Layer: `Default`
   * 트리거가 아니어야 플레이어와 `CollisionEnter` 이벤트가
     발생합니다 (11.3 참고).

적을 여러 마리 두려면 `Mossling1` 을 Duplicate 해서 `Mossling2`,
`Mossling3`, ... 로 이름을 바꾸고 위치만 옮기면 됩니다. 아래
스크립트는 이름이 `Mossling` 으로 시작하기만 하면 동작합니다.

### 18.2 Mossling.lua — 순찰 · 발견 · 추적

`Assets/Scripts/Mossling.lua`:

```lua
-- Assets/Scripts/Mossling.lua
-- 순찰하다가 플레이어를 발견하면 추적한다.
-- 공격은 접촉 데미지 방식 -- 몸이 닿으면 Player 쪽
-- CollisionEnter 에서 피해 처리를 한다 (18.3).

local PATROL_SPEED  = 1.5
local CHASE_SPEED   = 3.5
local PATROL_RANGE  = 3.0   -- 시작 위치에서 좌우로 왕복하는 거리
local DETECT_RANGE  = 6.0   -- 이 거리 안이면 발견 후보
local DETECT_HEIGHT = 1.5   -- 위아래 높이 차가 이 이내일 때만
local FRAME_TIME    = 0.25  -- 걷기 flipbook 프레임 간격 (초)

-- Mossling이 여러 마리여도 스크립트 파일은 하나 -- 엔티티 이름을
-- 키로 하는 테이블에 마리별 상태를 저장한다 (Player.lua와 동일한
-- 패턴).
local state = {}
local function s(name)
  local t = state[name]
  if not t then
    local x, _ = transform.get_position(name)
    t = { homeX = x, dir = -1, animT = 0, frame = 0, chasing = false }
    state[name] = t
  end
  return t
end

-- 시야 판정. 거리/높이로 먼저 거른 뒤 마지막에 시선 raycast:
-- 적 위치에서 플레이어 위치로 광선을 쏘고 (자기 자신은 exclude),
-- "가장 먼저 맞은 것"이 Player일 때만 보이는 것으로 친다.
-- 중간에 지면 타일이나 다른 콜라이더가 있으면 그쪽이 먼저 맞아
-- false가 되므로, 자연스럽게 "벽 뒤는 안 보이는" 시야가 된다.
local function canSeePlayer(self, ex, ey, px, py)
  if math.abs(px - ex) > DETECT_RANGE  then return false end
  if math.abs(py - ey) > DETECT_HEIGHT then return false end
  return scene.raycast(ex, ey, px, py, self) == "Player"
end

function Mossling_Update(self, dt)
  local st = s(self)
  local ex, ey = transform.get_position(self)
  local vx, vy = rigidbody.get_velocity(self)

  -- 플레이어 위치 (죽거나 없으면 nil)
  local px, py
  if scene.find("Player") then
    px, py = transform.get_position("Player")
  end

  st.chasing = (px ~= nil) and canSeePlayer(self, ex, ey, px, py)

  if st.chasing then
    -- 추적: 플레이어 쪽으로 빠르게 다가간다.
    st.dir = (px > ex) and 1 or -1
    vx = st.dir * CHASE_SPEED
  else
    -- 순찰: homeX +- PATROL_RANGE 사이를 왕복.
    if ex > st.homeX + PATROL_RANGE then st.dir = -1 end
    if ex < st.homeX - PATROL_RANGE then st.dir =  1 end
    vx = st.dir * PATROL_SPEED
  end

  -- 낭떠러지 검사: 진행 방향 반 유닛 앞의 발밑으로 짧은 광선을
  -- 쏘아 지면이 없으면, 순찰 중엔 돌아서고 추적 중엔 멈춘다
  -- (추적하다 절벽으로 뛰어내리지 않게).
  local aheadX = ex + st.dir * 0.5
  if scene.raycast(aheadX, ey, aheadX, ey - 1.5, self) == "" then
    if st.chasing then
      vx = 0
    else
      st.dir = -st.dir
      vx = st.dir * PATROL_SPEED
    end
  end

  rigidbody.set_velocity(self, vx, vy)
  sprite.set_flip_x(self, st.dir < 0)

  -- 2프레임 걷기 flipbook. Animator Controller 없이 스크립트로
  -- 서브스프라이트를 교대시키는 가장 가벼운 방법이다.
  st.animT = st.animT + dt
  if st.animT >= FRAME_TIME then
    st.animT = st.animT - FRAME_TIME
    st.frame = 1 - st.frame
    sprite.set_sub_sprite(self, "walk_" .. st.frame)
  end
end
```

각 Mossling 엔티티에 **Add Component... → Script**:

* Script: `Scripts/Mossling.lua`
* Entry function: `Mossling`

### 18.3 Player.lua — 밟기와 피격 처리

밟기/피격 판정은 한곳에서 처리하는 게 깔끔하므로 **Player 쪽**
`CollisionEnter` 에 둡니다. `Player.lua` 에 아래를 추가합니다.

상수와 상태 (파일 상단):

```lua
local STOMP_BOUNCE    = 9.0   -- 밟은 뒤 튀어오르는 속도
local HURT_KNOCKBACK  = 8.0   -- 피격 시 밀려나는 속도
local HURT_INVINCIBLE = 1.5   -- 피격 후 무적 시간 (초)
```

`s(name)` 헬퍼가 만드는 상태 테이블에 `invincible = 0` 필드를
추가합니다:

```lua
t = { coyote = 0, buffer = 0, wasGrounded = false, invincible = 0 }
```

`Player_Start` 에 전역 게임 상태 초기화를 추가합니다. `state` 는
씬을 다시 로드해도 유지되는 전역 테이블이므로 (11.4), `or` 로
"이미 있으면 그대로" 초기화합니다:

```lua
state.score = state.score or 0
state.lives = state.lives or 3
state.stage = state.stage or 1
```

충돌 핸들러 (새 함수):

`CollisionEnter` 는 물리 엔진이 충돌을 해소한 뒤 호출됩니다. 따라서
플레이어가 적의 윗면에 착지한 프레임에는 아래쪽 속도가 이미 `0`으로
보정되어 있을 수 있습니다. 하강 여부는 `vy < 0`이 아니라 작은 오차를
허용한 `vy <= 0.1`로 검사합니다. 플레이어가 적보다 충분히 위에 있다는
위치 조건을 함께 사용하므로 옆면 충돌은 밟기로 처리되지 않습니다.

```lua
-- 적과 충돌. 위에서 밟았으면 적 처치 + 점수 + 코인 드롭,
-- 옆/아래에서 닿았으면 피격.
function Player_CollisionEnter(self, other)
  if other:sub(1, 8) ~= "Mossling" then return end
  local st = s(self)
  local px, py = transform.get_position(self)
  local ex, ey = transform.get_position(other)
  local vx, vy = rigidbody.get_velocity(self)

  -- 밟기 판정: 플레이어가 적 중심보다 충분히 위에 있고
  -- 하강 중이거나 착지 충돌로 Y 속도가 0에 가깝게 보정된 경우에만
  -- 밟은 것으로 인정한다. CollisionEnter는 충돌 해소 뒤 호출된다.
  if py > ey + 0.3 and vy <= 0.1 then
    state.score = (state.score or 0) + 100
    audio.play("Sounds/stomp.wav", 0.9)
    -- 미리 숨겨 둔 드롭 코인을 적이 있던 자리에서 활성화 (18.4)
    local coin = other .. "_Coin"
    if scene.find(coin) then
      transform.set_position(coin, ex, ey + 0.6, 0)
      scene.set_enabled(coin, true)
    end
    scene.destroy_entity(other)
    rigidbody.set_velocity(self, vx, STOMP_BOUNCE)
    return
  end

  -- 피격. 무적 시간 동안은 무시.
  if st.invincible > 0 then return end
  st.invincible = HURT_INVINCIBLE
  state.lives = state.lives - 1
  audio.play("Sounds/hurt.wav", 0.9)
  -- 적 반대쪽으로 튕겨 나가는 넉백
  local knockDir = (px < ex) and -1 or 1
  rigidbody.set_velocity(self, knockDir * HURT_KNOCKBACK, 6.0)

  if state.lives <= 0 then
    -- 게임 오버: 상태를 리셋하고 씬 재시작 (14 참고)
    state.lives = 3
    state.score = 0
    scene.load("Assets/Scenes/Level1.nscene",
      { fade_out = 0.6, fade_in = 0.6, color = { r=0, g=0, b=0 } })
  end
end
```

마지막으로 `Player_Update` 안에 무적 시간 처리(타이머 감소 +
깜빡임)를 추가합니다:

```lua
-- 피격 후 무적 시간: 타이머를 줄이면서 스프라이트를 깜빡인다.
if st.invincible > 0 then
  st.invincible = math.max(0, st.invincible - dt)
  local blink = (math.floor(st.invincible * 10) % 2 == 0)
  sprite.set_alpha(self, blink and 1.0 or 0.4)
  if st.invincible == 0 then sprite.set_alpha(self, 1.0) end
end
```

효과음 파일(`stomp.wav`, `hurt.wav`)이 없으면 `audio.play` 줄은
지우거나 가지고 있는 다른 파일로 바꿔도 됩니다.

### 18.4 밟으면 코인이 나오게 — 숨긴 엔티티 활성화 패턴

Lua에서는 스프라이트 자산을 처음부터 만들어 붙일 수 없으므로,
**드롭될 코인을 미리 씬에 숨겨 두고 필요할 때 활성화**하는 패턴을
씁니다 (오브젝트 풀의 가장 단순한 형태입니다).

1. 11장에서 만든 코인 엔티티 하나를 **Duplicate** 합니다.
2. 이름을 `Mossling1_Coin` 으로 바꿉니다 — 18.3의 코드가
   `적 이름 .. "_Coin"` 규칙으로 찾습니다.
3. Inspector 상단의 **enabled 체크박스를 해제**해 비활성화합니다.
   비활성 엔티티는 그려지지도, 트리거로 반응하지도 않습니다.
4. 적마다 하나씩: `Mossling2_Coin`, `Mossling3_Coin`, ...

밟히면 18.3의 코드가 코인을 적 위치로 옮기고 활성화합니다. 코인에는
이미 `Coin.lua` 가 붙어 있으므로(11.2) 주우면 `state.coins` 가
올라가고 사라집니다.

> **왜 코인을 Mossling의 자식으로 두지 않나?**
> `scene.destroy_entity` 는 자식 트리를 통째로 파괴합니다. 코인이
> 적의 자식이면 적이 죽는 순간 코인도 같이 사라지므로, 반드시 씬
> 루트(또는 다른 부모) 아래에 둬야 합니다.

### 18.5 실행해 보기

Play를 켜면:

* Mossling이 시작 지점을 중심으로 좌우 순찰합니다.
* 가까이 가면 (시선이 닿는 순간) 속도를 올려 쫓아옵니다. 벽이나
  지면 뒤에 숨으면 다시 순찰로 돌아갑니다.
* 옆에서 닿으면 라이프가 1 줄고 잠깐 무적(깜빡임) + 넉백.
* 위에서 밟으면 적이 사라지고 +100점, 그 자리에 코인이 나옵니다.

콘솔에 `state.score` 를 찍어 보거나, 19장의 HUD를 만들어 화면에서
바로 확인하세요.

---

## 19. HUD 만들기 — 점수 · 스테이지 · 라이프 표시

엔진에는 아직 화면 고정 텍스트를 그리는 전용 UI API가 없으므로,
HUD는 **카메라의 자식 스프라이트**로 만듭니다. 부모(카메라)가
움직이면 자식도 따라 움직이므로 화면에 고정된 것처럼 보이고, 숫자는
0~9 스프라이트 시트의 서브스프라이트를 갈아 끼우는 방식으로
표시합니다.

### 19.1 숫자/하트 스프라이트 준비

1. `Assets/Sprites/digits.png` — `0123456789` 가 가로로 나란한
   10프레임 시트를 임포트합니다 (Filter: Point).
2. Sprite Editor 에서 **By Cell Count** `10 x 1` 로 슬라이스하고
   이름을 `digit_0` ~ `digit_9` 로 바꿉니다.
3. `Assets/Sprites/heart.png` — 라이프 표시용 하트 한 장을
   임포트합니다.

### 19.2 HUD 엔티티 트리

Hierarchy에서 다음 구조를 만듭니다. **HUD를 MainCamera의 자식**으로
만드는 것이 핵심입니다 (Hierarchy에서 엔티티를 드래그해 MainCamera
위에 떨어뜨리면 자식이 됩니다).

```
MainCamera
└─ HUD                    <- 빈 엔티티, Position (0, 0, 0)
   ├─ ScoreD3             <- 1000의 자리
   ├─ ScoreD2             <- 100의 자리
   ├─ ScoreD1             <- 10의 자리
   ├─ ScoreD0             <- 1의 자리
   ├─ StageD              <- 스테이지 번호
   ├─ Heart1
   ├─ Heart2
   └─ Heart3
```

각 자식의 설정:

* **ScoreD3~D0**: SpriteRenderer — Sprite `digits.png`, Sub-sprite
  `digit_0`, **Sorting Layer: `UI`** (1.4에서 만든 레이어).
  Position은 화면 왼쪽 위, 부모 기준 로컬 오프셋입니다. 카메라
  줌 80px/u 기준 화면 반폭이 (10, 5.6) 이므로 예를 들어
  `(-9.2, 4.8)`, `(-8.6, 4.8)`, `(-8.0, 4.8)`, `(-7.4, 4.8)`.
* **StageD**: 같은 digits 스프라이트, Position `(0, 4.8)` (상단
  중앙). "STAGE" 글자가 필요하면 라벨 이미지를 한 장 만들어 왼쪽에
  붙이면 됩니다.
* **Heart1~3**: Sprite `heart.png`, Sorting Layer `UI`, Position
  오른쪽 위 `(7.6, 4.8)`, `(8.4, 4.8)`, `(9.2, 4.8)`.

> 자식 엔티티의 Position은 **부모 기준 로컬 좌표**입니다. 카메라가
> 어디로 가든 HUD는 항상 화면의 같은 자리에 보입니다. 화면 가장자리
> 좌표는 카메라 줌에 따라 달라지므로 (`반폭 = 창크기/2/zoom`) 줌을
> 바꿨다면 오프셋도 함께 조정하세요.

### 19.3 Hud.lua

`Assets/Scripts/Hud.lua`:

```lua
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
```

`HUD` 엔티티에 **Add Component... → Script**:

* Script: `Scripts/Hud.lua`
* Entry function: `Hud`

### 19.4 스테이지 번호 갱신

여러 스테이지를 쓴다면 (14장의 Goal 트리거) 씬을 넘길 때
`state.stage` 를 갱신합니다:

```lua
if other == "Goal" then
  state.stage = 2
  scene.load("Assets/Scenes/Level2.nscene",
    { fade_out = 0.5, fade_in = 0.5, color = { r=0, g=0, b=0 } })
end
```

Level2 씬에도 같은 구조의 HUD(카메라 자식)를 만들어 두면 점수와
라이프가 그대로 이어져 표시됩니다.

### 19.5 실행해 보기

Play를 켜고:

* 코인을 줍거나 적을 밟으면 왼쪽 위 점수가 즉시 올라갑니다.
* 적에게 부딪히면 오른쪽 위 하트가 하나 꺼집니다.
* 라이프가 다 닳으면 씬이 재시작되고 하트 3개 / 점수 0으로
  돌아갑니다.

---

## 20. 움직이는 플랫폼

두 지점을 왕복하는 발판을 만듭니다. 핵심은 두 가지입니다:

* 플랫폼은 **Rigidbody2D 없이** 스크립트로 직접 움직입니다.
  Rigidbody를 붙이면 중력으로 떨어져 버립니다. Rigidbody 없는
  BoxCollider는 "고정 콜라이더"인데, 매 프레임 위치를 옮기면
  사실상 키네마틱 플랫폼이 됩니다.
* 물리 엔진은 발판이 움직여도 **위에 서 있는 것을 자동으로 태워
  주지 않으므로**, 플레이어가 올라타 있으면 플랫폼의 이동량만큼
  플레이어를 같이 옮겨 주는 "캐리" 코드를 스크립트에 넣습니다.

### 20.1 Platform 엔티티 만들기

1. `Assets/Sprites/platform.png` (예: 96x16 발판 이미지)를
   임포트합니다 (Pivot: Center, Filter: Point).
2. Hierarchy → **+ Entity** → 이름 `Platform1` → 왕복 시작 지점에
   배치합니다.
3. **Add Component...** → **SpriteRenderer**: Sprite `platform.png`,
   Order in Layer `5`.
4. **Add Component...** → **BoxCollider**:
   * Size: 스프라이트에 맞게 (96x16, PPU 48이면 `2.0, 0.33`)
   * **Is Trigger: 해제** (밟고 설 수 있어야 하므로)
   * Layer: `Default`
5. **Rigidbody2D는 붙이지 않습니다.**

### 20.2 Platform.lua

`Assets/Scripts/Platform.lua`:

```lua
-- Assets/Scripts/Platform.lua
-- 시작 지점에서 (TRAVEL_X, TRAVEL_Y)만큼 떨어진 지점까지
-- 부드럽게 왕복하는 발판. 올라탄 플레이어는 이동량만큼
-- 같이 움직인다 (캐리).

local TRAVEL_X = 4.0   -- 왕복 이동량 (world unit). 수직 발판이면
local TRAVEL_Y = 0.0   -- X를 0으로, Y에 거리를 넣는다.
local PERIOD   = 4.0   -- 한 번 갔다 돌아오는 데 걸리는 시간 (초)

local state = {}
local function s(name)
  local t = state[name]
  if not t then
    local x, y = transform.get_position(name)
    t = { homeX = x, homeY = y, phase = 0 }
    state[name] = t
  end
  return t
end

function Platform_Update(self, dt)
  local st = s(self)
  local x, y = transform.get_position(self)

  -- 사인 왕복: phase 0 -> 1 이 한 사이클. cos 곡선을 쓰면 양 끝에서
  -- 속도가 0으로 줄어 부드럽게 방향을 바꾼다 (등속 왕복보다 자연스럽다).
  st.phase = (st.phase + dt / PERIOD) % 1.0
  local k = 0.5 - 0.5 * math.cos(st.phase * 2.0 * math.pi)
  local nx = st.homeX + TRAVEL_X * k
  local ny = st.homeY + TRAVEL_Y * k
  local dx = nx - x
  local dy = ny - y
  transform.set_position(self, nx, ny, 0)

  -- 캐리: 플레이어의 발밑 raycast가 "이 플랫폼"을 맞추고 있고
  -- 상승 중이 아니면 (점프 순간 제외) 올라탄 것으로 보고
  -- 이동량만큼 같이 옮긴다. raycast를 쓰면 콜라이더 크기를
  -- 상수로 다시 적을 필요가 없다 (8장의 isGrounded와 같은 원리).
  if scene.find("Player") then
    local px, py = transform.get_position("Player")
    local _, pvy = rigidbody.get_velocity("Player")
    if pvy <= 0.1 and
       scene.raycast(px, py, px, py - 2.0, "Player") == self then
      transform.translate("Player", dx, dy, 0)
    end
  end
end
```

`Platform1` 에 **Add Component... → Script**:

* Script: `Scripts/Platform.lua`
* Entry function: `Platform`

플랫폼을 여러 개 두려면 Duplicate 후 이름만 `Platform2`, ... 로
바꾸면 됩니다. 각자 자기 시작 위치(`homeX/homeY`)를 기준으로
왕복합니다.

### 20.3 변형과 튜닝

* **수직 엘리베이터**: `TRAVEL_X = 0.0`, `TRAVEL_Y = 2.5`.
  플레이어의 낙하 속도보다 발판이 빨리 내려가면 떨어졌다 다시
  닿기를 반복하며 덜컹거립니다. 그때는 `PERIOD` 를 늘리세요.
* **대각선**: `TRAVEL_X`, `TRAVEL_Y` 를 둘 다 사용.
* **정지 구간이 있는 왕복**: `k` 를 계산한 뒤
  `k = math.clamp((k - 0.1) / 0.8, 0, 1)` 처럼 양 끝 10%를 잘라
  내면 끝에서 잠깐 머무는 연출이 됩니다.
* 8장의 `isGrounded` 는 raycast 기반이라 플랫폼의 BoxCollider도
  지면으로 인식합니다 — 발판 위에서 점프/착지 애니메이션이 그대로
  동작합니다.

### 20.4 실행해 보기

Play를 켜면 발판이 좌우로 왕복합니다. 올라타면 발판과 함께
이동하고, 발판 위에서 점프하면 캐리가 잠시 풀렸다가 착지하면
다시 붙습니다. 발판 이동 방향과 반대로 걸으면 제자리걸음이 되는
것도 확인해 보세요 — 캐리 이동량과 걷기 속도가 합산되기 때문입니다.

---

## 21. 깨지는 블록과 ? 블록 (아이템 블록)

클래식 플랫포머에서 자주 쓰이는 두 가지 블록을 만듭니다:

* **벽돌(Brick)**: 아래에서 머리로 치면 깨져 사라지고 점수를 줍니다.
* **선물 블록(SurpriseBlock)**: 아래에서 치면 아이템(코인)이 위로 나오고,
  "빈 블록" 모양으로 바뀌어 다시는 반응하지 않습니다.

두 블록 모두 원리는 같습니다 — 블록은 Rigidbody 없는 솔리드
BoxCollider(= 고정 블록)이고, 플레이어가 부딪히면 블록 쪽 스크립트의
`CollisionEnter` 가 불립니다. 거기서 "아래에서 쳤는지"만 판별하면
됩니다.

> **왜 속도가 아니라 위치로 판별하나?** `CollisionEnter` 콜백은
> 물리 해소(밀어내기 + 속도 반사)가 끝난 **뒤에** 불립니다. 머리로
> 치는 순간 위쪽 속도는 이미 0으로 깎여 있으므로, 속도 대신
> "플레이어가 블록보다 충분히 아래에 있고 수평으로 겹친다"는
> 위치 관계로 판별하는 것이 안정적입니다.

### 21.1 벽돌 (깨지는 블록)

1. `Assets/Sprites/brick.png` 를 임포트합니다 (Pivot: Center,
   Filter: Point).
2. Hierarchy → **+ Entity** → 이름 `Brick1` → 플레이어가 점프로
   머리를 닿을 수 있는 높이에 배치.
3. **Add Component...** → **SpriteRenderer**: Sprite `brick.png`.
4. **Add Component...** → **BoxCollider**: Size는 스프라이트에 맞게,
   **Is Trigger 해제**, Layer `Default`. **Rigidbody2D는 붙이지
   않습니다** (붙이면 떨어집니다).

`Assets/Scripts/Brick.lua`:

```lua
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
```

`Brick1` 에 **Add Component... → Script**: Script
`Scripts/Brick.lua`, Entry function `Brick`. 벽돌 여러 개는
Duplicate — 스크립트가 이름에 의존하지 않으므로 이름은
`Brick2`, `Brick3`, ... 아무거나 됩니다 (단 `_CollisionEnter` 는
같은 Entry function 을 공유).

### 21.2 선물 블록 (아이템 블록)

1. `Assets/Sprites/surprise_block.png` — 선물 모양과 "빈 블록" 모양이 가로로
   나란한 2프레임 시트를 임포트하고, Sprite Editor에서 **By Cell
   Count** `2 x 1` 로 슬라이스 → 이름을 `surprise_active`,
   `surprise_used` 로 바꿉니다.
2. 엔티티 `SurpriseBlock1` 을 만들고 SpriteRenderer (Sub-sprite:
   `surprise_active`) + BoxCollider (Is Trigger 해제)를 붙입니다.
   역시 Rigidbody2D는 없습니다.
3. **나올 아이템 준비** — 18.4의 숨긴 엔티티 활성화 패턴 그대로:
   11장의 코인을 Duplicate 해서 이름을 `SurpriseBlock1_Item` 으로 바꾸고
   **enabled 체크를 해제**합니다. 스크립트가
   `블록 이름 .. "_Item"` 규칙으로 찾습니다.

`Assets/Scripts/SurpriseBlock.lua`:

```lua
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
```

`SurpriseBlock1` 에 Script 컴포넌트: Script `Scripts/SurpriseBlock.lua`, Entry
function `SurpriseBlock`. 블록마다 대응하는 `_Item` 엔티티를 하나씩
준비합니다 (`SurpriseBlock2` → `SurpriseBlock2_Item`).

**파워업 변형**: 아이템 엔티티에 Rigidbody2D + BoxCollider를 주면
활성화되는 순간 중력을 받아 블록 위에서 떨어지는 버섯형 아이템이
됩니다. 거기에 18장의 Mossling처럼 일정 속도로 걷는 스크립트를
붙이면 화면을 굴러다니는 파워업이 완성됩니다. 효과는
`Player_TriggerEnter` 에서 `state.lives = state.lives + 1` 처럼
원하는 보상으로 처리하면 됩니다.

### 21.3 실행해 보기

Play를 켜고:

* 벽돌 아래에서 점프해 머리로 치면 벽돌이 사라지고 +50점
  (19장의 HUD가 있으면 점수가 바로 올라갑니다).
* ? 블록을 치면 "빈 블록" 모양으로 바뀌면서 코인이 위로 나오고,
  코인을 주우면 `state.coins` 가 올라갑니다. 빈 블록을 다시 쳐도
  아무 일도 일어나지 않습니다.
* 옆에서 걸어가 부딪히거나 블록 위에 올라서는 것으로는 발동하지
  않는 것도 확인해 보세요 — `hitFromBelow` 의 위치 판별 덕분입니다.

---

## 부록 A. Lua API 요약표

### input.*

| 함수 | 반환 |
|---|---|
| `input.is_key_down(name)` | bool |
| `input.is_key_pressed(name, repeat?)` | bool |
| `input.is_key_released(name)` | bool |
| `input.mouse_position()` | x, y |
| `input.is_mouse_down(btn=0)` / `_pressed` / `_released` | bool |
| `input.mouse_wheel()` | number |
| `input.bind(action, key_or_keys)` / `unbind(action)` / `binding(action)` | — |
| `input.is_action_down/pressed/released(action)` | bool |

### transform.*

| 함수 | 반환 |
|---|---|
| `transform.set_position(e, x, y, z)` / `get_position(e)` | — / x,y,z |
| `transform.set_rotation(e, deg)` / `get_rotation(e)` | — / deg |
| `transform.set_scale(e, sx, sy, sz)` / `get_scale(e)` | — / sx,sy,sz |
| `transform.translate(e, dx, dy, dz)` | — |

### rigidbody.*

| 함수 | 반환 |
|---|---|
| `rigidbody.set_velocity(e, vx, vy)` / `get_velocity(e)` | — / vx,vy |
| `rigidbody.add_force(e, fx, fy)` (즉시 impulse) | — |
| `rigidbody.set_gravity(e, gx, gy)` / `get_gravity(e)` | — / gx,gy |
| `rigidbody.set_drag(e, d)` / `get_drag(e)` | — / d |

### sprite.*

| 함수 | 반환 |
|---|---|
| `sprite.set_color(e, r, g, b, a)` / `get_color(e)` | — / 4 |
| `sprite.set_alpha(e, a)` / `get_alpha(e)` | — / a |
| `sprite.set_sub_sprite(e, name)` / `get_sub_sprite(e)` | — / name |
| `sprite.set_flip_x(e, bool)` / `set_flip_y(e, bool)` (+get) | |
| `sprite.set_size(e, w, h)` / `get_size(e)` | |
| `sprite.set_order(e, n)` / `get_order(e)` | |

### animator.*

| 함수 | 예시 |
|---|---|
| `animator.set_bool(e, name, b)` / `get_bool` | — |
| `animator.set_float(e, name, x)` / `get_float` | — |
| `animator.set_int(e, name, i)` / `get_int` | — |
| `animator.set_trigger(e, name)` | — |
| `animator.set_vec2/get_vec2/set_color/get_color/set_string/get_string` | — |
| `animator.current_state(e)` / `in_state(e, name)` | str / bool |

### scene.*

| 함수 | 반환 |
|---|---|
| `scene.create_entity(name?, parent?)` / `destroy_entity(name)` | 실이름 / bool |
| `scene.find(name)` / `entity_count()` / `all_entities()` | bool / int / table |
| `scene.add_component(e, type)` / `has_component` / `remove_component` | bool |
| `scene.set_enabled(e, bool)` / `is_enabled(e)` | bool |
| `scene.parent_of(e)` / `children_of(e)` / `set_parent(e, p)` | |
| `scene.destroy_self()` | 이벤트 훅 안에서만 |
| `scene.entity_at(x, y)` | 이름 str, "" = 없음 |
| `scene.find_by_component(type)` | table |
| `scene.broadcast(method, ...)` | int |
| `scene.raycast(x0, y0, x1, y1, exclude?)` | name, hx, hy, dist (miss = "") |
| `scene.overlap_box(cx, cy, w, h)` / `overlap_circle(cx, cy, r)` | table |
| `scene.find_path(sx, sy, ex, ey, smooth?)` | table 또는 nil |
| `scene.load(path, opts?)` | bool (지연 실행) |

### camera.*

| 함수 | 반환 |
|---|---|
| `camera.set_zoom(e, z)` / `get_zoom(e)` | bool / num |
| `camera.set_priority(e, p)` / `get_priority(e)` | bool / int |
| `camera.get_active()` | 이름 str, "" = 없음 |

### audio.*

| 함수 | 반환 |
|---|---|
| `audio.play(path, vol?=1)` | bool |
| `audio.stop()` | — |
| `audio.play_music(path, vol?=1, loop?=false)` / `stop_music()` | bool |
| `audio.set_bus_volume(bus, v)` / `get_bus_volume(bus)` | bus ∈ {"master","sfx","music"} |
| `audio.spawn(path, vol?)` → voice handle | handle int |
| `audio.stop_voice/pause_voice/resume_voice/set_voice_volume/is_voice_playing(h)` | |
| `audio.play_positional(path, vol, pan, freq?)` | pan ∈ [-1,1] |
| `audio.set_reverb_wet(w)` / `get_reverb_wet()` | |

### particle.*

| 함수 | 반환 |
|---|---|
| `particle.emit(e, count?=1)` | 실제 방출 수 |
| `particle.clear(e)` / `set_emission_rate(e, r)` / `get_count(e)` | |
| `particle.set_playing(e, bool)` / `is_playing(e)` | bool |

### dialog.*

| 함수 | 반환 |
|---|---|
| `dialog.show(text)` / `dialog.choices(text, {opts})` | — |
| `dialog.hide()` / `dialog.is_visible()` | / bool |
| `dialog.last_choice()` | int (1-based, 0 = 미선택) |

### component.*

| 함수 | 반환 |
|---|---|
| `component.register(name, defaults)` | — |
| `component.get(e, name)` | table (복사본) |
| `component.set(e, name, tbl)` | bool |
| `component.types()` | table (등록된 이름들) |

### time.*, savegame.*, settings.*, math.* 확장

* `time.delta()` / `time.elapsed()` / `time.frame_count()` / `time.fps()`
* `savegame.save(slot?)` / `savegame.load(slot?)` / `savegame.clear()` /
  `savegame.exists(slot?)` / `savegame.delete_slot(slot?)`
* `settings.show()` / `settings.hide()` / `settings.toggle()` /
  `settings.is_visible()` — 인게임 오디오/키 리바인딩 오버레이
* `math.lerp(a, b, t)`, `math.clamp(x, lo, hi)`, `math.sign(x)`,
  `math.smoothstep(e0, e1, x)`, `math.distance(x1, y1, x2, y2)`,
  `math.length(x, y)`, `math.normalize(x, y)`,
  `math.dot(x1, y1, x2, y2)`

전역 테이블 `state` 는 빈 테이블로 시작하며 `savegame.save` 시
JSON 으로 직렬화됩니다.

---

## 부록 B. Lua 라이프사이클 훅

Script 컴포넌트의 **Entry function** 필드에 넣은 이름이 훅의
접두어가 됩니다. 예: `Entry function = "Player"` 이면
`Player_Start`, `Player_Update` 순으로 호출되고, 없으면 bare
`OnStart`, `OnUpdate` 로 폴백합니다.

| 훅 | 시그니처 | 호출 시점 |
|---|---|---|
| `<Entry>_Start(self)` / `OnStart(self)` | (엔티티 이름) | 첫 프레임 |
| `<Entry>_Update(self, dt)` / `OnUpdate` | (name, dt) | 매 프레임 |
| `<Entry>_Destroy(self)` / `OnDestroy` | (name) | 파괴 직전 |
| `<Entry>_Enable/Disable(self)` / `OnEnable/OnDisable` | (name) | enabled 토글 |
| `<Entry>_TriggerEnter/Stay/Exit(self, other)` / `OnTrigger*` | (name, otherName) | isTrigger 접촉 |
| `<Entry>_CollisionEnter/Stay/Exit(self, other)` / `OnCollision*` | (name, otherName) | 실체 충돌 |
| `OnAnimationEvent(self, anim, ev, s, f, i, b, vx, vy, r, g, bl, a)` | 다수 | 애니메이션 이벤트 트랙 |
| `scene.broadcast("Damage", 10)` → `<Entry>_Damage(self, 10)` / `OnDamage` | (name, ...) | 임의 브로드캐스트 |

`scene.destroy_self()` 는 위 훅 안에서만 유효합니다 (현재 dispatch
중인 엔티티를 파괴).

---

축하합니다. 여기까지 따라 하시면 클래식 플랫포머의 뼈대가
완성됩니다. 이후 확장 아이디어:

* Enemy 엔티티 + AI 상태 머신 (Animator Controller 재사용).
* Power-up (버섯, 별) — `component.register("PowerUp", {...})` 로
  타입별 로직 분리.
* 오디오 이벤트 트랙 — Animation Editor 의 Events 섹션에 발자국
  이벤트를 심고 `OnAnimationEvent` 에서 `audio.play` 호출.
* Dialog 시스템 — `dialog.show`, `dialog.choices` 로 NPC 대화.
* 세이브 슬롯 — `savegame.save("run1")` 로 진행 상황 저장.

문제가 생기면 **Console** 창의 REPL 프롬프트 `>` 에서 직접
`print(...)`, `scene.all_entities()` 등을 쳐 보며 상태를 조사할 수
있습니다.
