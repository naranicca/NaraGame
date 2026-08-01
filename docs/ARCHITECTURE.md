# NaraGame 엔진 아키텍처

## 레이어 구조

```
   ┌─────────────────────────────────────────────┐
   │  Game (test/breakout, space_invaders, mario) │   게임 코드
   └───────────────────────┬─────────────────────┘
                           │  Application 상속 + On* 훅
   ┌───────────────────────▼─────────────────────┐
   │  Core: Application / Log / Time / Input       │   플랫폼 공통
   └───────┬───────────────────────────┬─────────┘
           │                           │
   ┌───────▼────────┐         ┌────────▼─────────┐
   │  Window (추상)  │         │  RHIDevice (추상) │   인터페이스
   └───────┬────────┘         └────────┬─────────┘
   ┌───────▼────────┐         ┌────────▼─────────┐
   │ Win32 / Cocoa  │         │ D3D11 / Metal /  │   플랫폼 백엔드
   │ UIKit / NDK    │         │ Vulkan           │
   └────────────────┘         └──────────────────┘
```

## 핵심 개념

### Application 과 진입점
게임은 `NaraGame::Application`을 상속하고 `OnStart/OnUpdate/OnRender/OnStop`을
오버라이드합니다. `NaraGame::CreateApplication()`을 정의하면, `EntryPoint.h`가
제공하는 `main()`이 이를 생성·실행합니다. `main()`을 직접 작성하지 않습니다.

메인 루프(`Application::Run`)의 매 프레임 순서:
1. `Input::NewFrame()` → `Window::PollEvents()`
2. 창 크기 변경 감지 시 `RHIDevice::Resize()`
3. `OnUpdate(dt)`
4. `BeginFrame` → `OnRender()` → `EndFrame` → `Present`

### RHI (Render Hardware Interface)
`RHIDevice`는 모든 네이티브 그래픽 API가 구현하는 추상 인터페이스입니다.
`RHIDevice::Create()` 팩토리가 컴파일 대상 플랫폼에 맞는 백엔드를 생성합니다.

| 플랫폼 | 윈도우 | RHI 백엔드 | 상태 |
| --- | --- | --- | --- |
| Windows | Win32 | Direct3D 11 | 에디터 및 프로젝트 플레이어 |
| macOS | Cocoa | Metal | 2D 프로젝트 플레이어, Xcode 검증 필요 |
| iOS | UIKit | Metal | 2D 프로젝트 플레이어, Xcode/실기기 검증 필요 |
| Android | NativeActivity | Vulkan | 프로젝트 플레이어, SDK/NDK 및 실기기 검증 필요 |

RHI는 버퍼, 텍스처, 셰이더, 파이프라인, 렌더 타겟과 인덱스 드로를
추상화합니다. Metal은 현재 프로젝트 플레이어의 2D 렌더링 경로를 지원하며,
3D·Depth·HDR 후처리는 아직 다른 백엔드와 완전한 기능 동등성이 없습니다.

### 플랫폼 감지
`core/Platform.h`가 컴파일러 매크로(`_WIN32`, `__APPLE__`, `__ANDROID__`)로부터
`NARAGAME_PLATFORM_*` 매크로 하나를 정의합니다. 플랫폼별 소스는 CMake가 대상
플랫폼에서만 컴파일 목록에 넣고, 헤더/구현 본문도 `#if`로 가드되어 있어
타 플랫폼 빌드에 새지 않습니다.

### 좌표계 / 행렬 규약
- 수학 라이브러리(`math/`)는 외부 의존성 없이 직접 구현.
- `Mat4`는 **열 우선(column-major)** 저장 — GPU 상수 버퍼 레이아웃과 일치.
- 투영 행렬(`Ortho`/`Perspective`)은 **깊이 범위 [0, 1]**, **오른손 좌표계**.
  Direct3D / Vulkan / Metal 클립 공간 규약과 맞춥니다(OpenGL의 [-1,1] 아님).

## 디렉토리에 새 기능 추가하기
- 공개 API 헤더 → `engine/include/NaraGame/<영역>/`, `#include "NaraGame/..."`로 사용.
- 내부 구현 → `engine/src/<영역>/`, `engine/src` 기준 상대 경로로 include.
- 새 플랫폼 백엔드 → 해당 `src/.../<platform>/` 폴더에 추가하고
  `engine/CMakeLists.txt`의 플랫폼 분기에 소스 등록.
- 새 테스트 게임 → `test/<game>/`에 `CMakeLists.txt` + `src/`,
  `test/CMakeLists.txt`에 `add_subdirectory()` 추가.
