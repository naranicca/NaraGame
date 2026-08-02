# NaraGame 개발 인수인계

이 문서는 NaraGame 저장소에서 작업하는 개발자와 에이전트를 위한 현재 상태,
검증 방법, 알려진 제약 및 다음 작업의 우선순위를 기록한다. 코드나 동작이 바뀌면
관련 항목도 함께 갱신한다.

## 프로젝트 개요

NaraGame은 C++20과 CMake로 작성된 2D 게임 엔진 및 Unity 스타일 편집기다.

- `engine/`: 공용 엔진, 플랫폼 창/입력/오디오, D3D11·Vulkan·Metal RHI
- `tools/editor/`: `NaraGame.exe` 편집기
- `tools/player/`: 프로젝트 및 패키지를 실행하는 `NaraGamePlayer.exe`
- `samples/`: 문서, 에셋, 생성 스크립트와 실행 가능한 튜토리얼 프로젝트
- `test/`: CTest 검증 스크립트와 엔진 예제
- `docs/LUA-SCRIPTING.md`: 편집기와 Player가 제공하는 Lua API

주 개발 및 자동 검증 환경은 Windows + Visual Studio 2022다. Android와 Apple
플랫폼은 네이티브 프로젝트를 내보내며, 최종 서명과 실제 기기 검증은 해당 SDK와
운영체제가 필요하다.

## 지금까지 구현된 주요 작업

최근 작업은 다음 커밋들에 정리되어 있다.

- `eb698dd`: 검증된 튜토리얼 프로젝트, Player 표시 설정 및 런타임 확장
- `8850ba4`: 모바일 빌드 검증과 네이티브 오디오
- `00ddfb1`: 크로스 플랫폼 NaraGamePlayer
- `b707110`: 모바일 빌드 내보내기와 추가 튜토리얼
- `7c06abd`: Android/macOS/iOS 플랫폼 기반
- `bf4436c`: macOS 스타일 Preferences UI

구현 완료 범위:

- File > Build...에서 Windows, Android, Mac, iPhone 타겟 선택
- Windows 패키지와 Android Gradle/NDK, Apple Xcode/CMake 프로젝트 내보내기
- 프로젝트별 Player 해상도, Windowed/Fullscreen, 창 크기 변경 허용 설정
- Lua `screen.set_resolution`, `screen.set_mode`, `screen.width/height/mode`
- Windows와 macOS의 창 크기 및 전체화면 전환
- 키보드, 마우스, 게임패드 및 모바일 터치 입력 기반
- WAV/OGG/MP3 재생, 버스 볼륨, 음악, 위치 오디오 및 리버브
- Lua 고정 업데이트, 충돌/트리거, 씬, 애니메이터, 파티클 및 저장 API
- SpriteRenderer 크기, 커스텀 쿼드 및 UV 영역을 Player 렌더링에 반영
- Player의 `--project`, `--scene`, `--frames` 실행 옵션과 Lua 오류 로그 출력
- Preferences 메뉴와 macOS 스타일 컨트롤
- Build 팝업의 자동 가로 확장 문제 수정
- 플레이 중 포커스된 Viewport/Game 창이 키보드 입력을 소유하여 방향키와
  스페이스가 편집기 버튼의 포커스 이동이나 활성화로 이어지지 않음

## 튜토리얼과 샘플

튜토리얼은 `tools/editor/docs`가 아니라 `samples` 아래에서 관리한다.

- `samples/platformer`: 횡스크롤 플랫포머
- `samples/sky-hopper`: Flappy Bird 유형 세로 게임
- `samples/nightfall-arena`: Vampire Survivors 유형 아레나 게임
- `samples/ashen-vale`: 아이소메트릭 액션 RPG
- `samples/turbo-trails`: 의사 3D 카트 레이싱

각 디렉터리는 다음을 포함한다.

- 따라 만들기 문서 `README.md`
- 바로 사용할 수 있는 `Assets/`
- 원본 이미지가 필요한 경우 `Source/`
- 실행 가능한 `DemoProject/`
- 데모를 문서에서 다시 만드는 `CreateDemoProject.ps1`
- 육안 검증용 스크린샷

에셋을 일괄 재생성할 때는 `samples/GenerateSampleAssets.ps1`을 사용한다. 생성기를
수정했다면 해당 DemoProject를 다시 만들고 CTest를 실행한다. 플랫포머 점수 증가는
스크립트 재로드 뒤에도 안전하도록 반드시 다음 형태를 유지한다.

```lua
state.score = (state.score or 0) + 100
```

## 빌드와 검증

Windows Release 편집기와 Player 빌드:

```powershell
cmake -S . -B build/windows-x64
cmake --build build/windows-x64 --config Release --target NaraGame
```

결과물:

- `build/windows-x64/bin/Release/NaraGame.exe`
- `build/windows-x64/bin/Release/NaraGamePlayer.exe`

전체 자동 검증:

```powershell
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

현재 등록된 검사는 다음 다섯 개다.

1. 튜토리얼 문서와 필수 에셋
2. 문서에 포함된 Lua 코드의 실제 로딩
3. 다섯 데모 프로젝트의 주요 씬 일곱 개 실행
4. Windows 게임 패키징
5. 플랫폼별 Player 템플릿

변경을 완료하기 전에 `git diff --check`도 통과해야 한다. 테스트가 Ashen Vale의
저장 데이터를 만들 수 있으므로 루트 `saves/slot1.json`은 커밋하지 않는다.

## 프로젝트 표시 설정

편집기의 `Window > Player Settings...`에서 기본 폭과 높이, 표시 모드 및 창 크기
변경 허용 여부를 설정한다. 값은 `ProjectSettings/project.json`의 `display` 객체에
저장된다. 기존 프로젝트의 `fullscreen` 불리언도 하위 호환으로 읽는다.

```json
"display": {
  "width": 1280,
  "height": 720,
  "mode": "windowed",
  "fullscreen": false,
  "resizable": true
}
```

모바일에서는 운영체제가 렌더링 표면 크기와 표시 모드를 소유하므로 데스크톱과 같은
임의 해상도 전환을 보장하지 않는다.

## 알려진 제약과 주의사항

- 엔진은 현재 2D 중심이다. 아이소메트릭과 카트 샘플은 2D/의사 3D 표현이다.
- Android와 Apple 내보내기는 구현됐지만 실제 스토어 배포에는 SDK, 서명, 기기별
  최종 검증이 필요하다.
- 스크립트 리로드는 Lua 파일의 로컬 상태를 다시 만든다. `Start`에서만 초기화한
  값에 의존하는 콜백은 nil-safe 기본값을 사용해야 한다. 장기적으로는 리로드 때
  lifecycle 상태를 재초기화하거나 명시적인 상태 마이그레이션 훅을 제공한다.
- 샘플 이미지에는 생성 원본과 최종 에셋이 함께 있어 용량이 크다. 파일을 제거하기
  전에 생성 스크립트와 문서의 재현 경로를 확인한다.
- 사용자의 별도 프로젝트는 저장소 밖에 있을 수 있다. 예를 들어 최근 플랫포머
  프로젝트는 `C:/Users/naran/Documents/PlatformerTest`에 있었으며 저장소 샘플과
  별도로 관리된다.

## 다음 개발 계획

우선순위 순서:

1. 스크립트 핫 리로드 lifecycle을 수정해 `Start` 초기화와 런타임 상태가 일관되게
   유지되도록 하고 회귀 테스트를 추가한다.
2. Player Settings에 Unity와 유사한 종횡비/스케일 정책(letterbox, expand,
   integer scaling)과 기준 해상도를 추가한다.
3. Android 실제 기기에서 입력, 오디오, Vulkan surface 재생성 및 APK/AAB 빌드를
   검증한다.
4. macOS/iPhone에서 Metal 렌더링, 오디오, 전체화면/회전, Xcode 서명 흐름을 실제
   하드웨어로 검증한다.
5. 튜토리얼 생성 결과에 단순 Lua 로딩뿐 아니라 캐릭터 위치, 충돌, 점수 및 씬 전환
   같은 게임 상태 assertion을 추가한다.
6. 샘플의 에셋 생성 재현성을 높이고 중복된 Source/DemoProject 바이너리의 배포
   정책을 정한다.
7. README의 오래되었거나 깨진 인코딩 구간을 UTF-8 한국어로 정리한다.

## 작업 원칙

- 사용자 변경과 무관한 파일은 되돌리지 않는다.
- 빌드 산출물, 사용자 저장 데이터와 로컬 도구 권한 설정은 커밋하지 않는다.
- 플랫폼 공용 API를 바꾸면 Windows만 빌드하지 말고 Android/macOS/iOS 구현의
  컴파일 영향도 함께 확인한다.
- 튜토리얼 코드를 고치면 문서, 생성기, DemoProject 및 자동 검사를 함께 갱신한다.
- 기능 완료 보고에는 실행한 빌드와 테스트, 검증하지 못한 플랫폼을 구분해 적는다.
