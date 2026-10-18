# CCEngine

CCEngine is a Windows C++ game engine and editor with C# scripting, animation authoring, physics tools, and a standalone game build pipeline.
DirectX 11 is the active rendering backend. DirectX 12, OpenGL, and Vulkan backend sources are under development; they are not yet available as runtime alternatives.

[English](#english) | [한국어](#한국어)

## English

### Engine Overview

| Area | Implemented capabilities |
| --- | --- |
| Editor | Multi-window docking, hierarchy, Inspector, scene/game views, configurable scene grid, asset selection windows |
| Assets | Metadata/GUID references, file watching, asset undo/redo, scene and prefab serialization |
| Animation | Animator controller/state/transition editing, layers, parameters, blend trees, clip settings, root motion and retargeting tools, property animation |
| Physics | 2D/3D collider tools, multi-object collider editing, physics debug visualization |
| Scripting | C# components, Inspector fields, Play Mode lifecycle and diagnostic logs |
| Game builds | Build Settings, standalone CCGame player, CCBuild command-line tool, dependency collection, shader compilation and build manifests |
| Distribution | Staged publishing, incremental file reuse, first-frame verification, package/hash verification, patch and signing tools |

These are implemented development features, not a claim of production certification. Hardware-dependent validation and backend integration remain ongoing.

### Rendering Status

| Backend | Current status |
| --- | --- |
| DirectX 11 | Active backend; shared graphics device and per-window context restructuring |
| DirectX 12 | Backend source and shared-device groundwork; runtime activation blocked pending integration and validation |
| OpenGL / Vulkan | Backend source present; shared-resource integration and runtime activation pending |

The RHI (Rendering Hardware Interface) separates common rendering code from graphics API implementations. Offline shader conversion tooling exists in `tools/CompileShaderBackends.py`; non-DX11 shader packages are not yet integrated into the normal game build/runtime path. Recent RHI changes still require build and GPU validation.

### Repository Layout

| Path | Purpose |
| --- | --- |
| `CCEngine/` | Engine, editor UI, rendering backends and runtime services |
| `Sandbox/` | Editor application, sample project and redistributable test assets |
| `CCGame/` | Standalone game player |
| `CCBuild/` | Command-line game build application |
| `tools/` | Build, release, patch, signing and shader tools |
| `docs/` | Public user documentation |

처음 사용하는 분은 [사용자 설명서](docs/CCEngine_User_Guide.md)부터 읽어 주세요. 스크립트 작성은 [C# 스크립팅](docs/CCEngine_Scripting.md), 로그 확인은 [로그와 문제 확인](docs/SCRIPTING_DEBUG_LOGS.md)에서 안내합니다.

## Requirements

- Windows
- Visual Studio 2022
- C++ desktop development workload
- PowerShell
- .NET 8 x64 runtime and the Visual Studio C# compiler for gameplay scripts
- .NET 8 SDK for game build/publishing workflows

The current `BuildScripts.ps1` uses the Visual Studio 2022 Community compiler path. For another edition, update its `$csc` variable to the installed compiler path.

## Project Setup

Generate the Visual Studio solution with Premake:

```bat
GenerateProjects.bat
```

This creates or refreshes the Visual Studio project files used to build the engine and Sandbox application.

## Build

Open `CCEngine.sln` in Visual Studio and build the solution.

Recommended configurations:

- `Debug x64` for development and debugging
- `Release x64` for normal editor testing

Alternatively, build from PowerShell with automatic amd64 MSBuild discovery:

```powershell
.\tools\BuildEngine.ps1 -Configuration Debug
```

For a standalone game, configure the start scene in Project Settings, then use `File > Build Settings...`. See the [game build and distribution guide](docs/CCEngine_User_Guide.md#게임-빌드와-배포) for output contents, validation, packaging and patching. Ship the complete output folder, not just the executable.

## Run

After building, run the `Sandbox` project from Visual Studio.

Use the repository's `Sandbox` folder as the working directory so the editor can find `assets` and project settings.

The editor should open with the default editor layout and the included Sandbox assets.

Useful test scenes:

- `assets/scenes/Collision_Test.ccscene`
- `assets/scenes/Animator_Test.ccscene`
- `assets/scenes/Physics3D_QA.ccscene`

## C# Script Build

C# gameplay scripts are source-controlled, but generated script DLLs are not committed.

To test scripting features, build the script assemblies once:

```powershell
cd Sandbox
.\assets\Scripts\BuildScripts.ps1
```

This generates the runtime script files under:

```text
Sandbox/assets/Scripts/Build/
```

Generated DLLs are local build outputs and should not be committed.

## Documentation

Public guide documents are stored in `docs/`.

Current guide files:

- [User Guide](docs/CCEngine_User_Guide.md): setup, scenes, Play Mode, physics visualization.
- [C# Scripting](docs/CCEngine_Scripting.md): first script, Inspector fields, lifecycle, troubleshooting.
- [Debug Logs](docs/SCRIPTING_DEBUG_LOGS.md): Console messages and physics event logs.

`docs/CCEngine_Scripting_Guide.pptx` and `docs/CCEngine_Scripting_Guidebook.pptx` are earlier learning materials. Use the Markdown guides above for current API names and workflows.

The `docs/` folder is intended to be pushed to GitHub when it contains public portfolio documentation.

## Notes

This project is still under active development. The included scenes are meant for basic feature verification rather than final game content.

## 한국어

CCEngine은 C++로 개발하는 Windows 게임 엔진 및 에디터입니다. 씬 편집, C# 스크립팅, 애니메이션 제작, 물리 편집 도구와 독립 실행 게임 빌드 기능을 제공합니다.

### 주요 기능

| 영역 | 구현 내용 |
| --- | --- |
| 에디터 | 멀티 윈도우와 도킹, Hierarchy, Inspector, Scene/Game View, 씬 그리드, 에셋 선택 창 |
| 에셋 | GUID 기반 참조, 파일 변경 감지, 에셋 작업 실행 취소/다시 실행, 씬과 프리팹 저장 |
| 애니메이션 | Animator Controller, 상태와 전이 그래프, 레이어, 파라미터, Blend Tree, 클립 설정, Root Motion, Retargeting, 속성 애니메이션 |
| 물리 | 2D/3D 콜라이더 편집, 여러 오브젝트의 콜라이더 동시 편집, 물리 디버그 표시 |
| 스크립팅 | C# 컴포넌트, Inspector 필드, Play Mode 생명주기, 진단 로그 |
| 빌드 | Build Settings, 독립 실행 CCGame Player, CCBuild 명령줄 도구, 에셋 의존성 수집, 셰이더 컴파일, 빌드 명세 |
| 배포 | 임시 폴더에서 완성 후 게시, 변경 없는 파일 재사용, 첫 화면 검증, 패키지/해시 검증, 차이 패치와 코드 서명 도구 |

위 목록은 구현된 기능 범위입니다. 모든 환경에서 검증을 마쳤거나 상용 배포 품질을 보증한다는 뜻은 아닙니다.

### 그래픽 API 상태

- **DirectX 11:** 현재 사용하는 렌더링 백엔드입니다. 공유 디바이스와 창별 컨텍스트로 구조를 정리하고 있습니다.
- **DirectX 12:** 백엔드 코드와 공유 디바이스 기반을 작성했습니다. 실제 연결 및 검증 전까지 실행 선택을 차단합니다.
- **OpenGL / Vulkan:** 백엔드 코드는 있으나 공유 리소스 연결과 런타임 활성화가 남았습니다.
- **RHI:** 그래픽 API에 종속된 코드를 공통 렌더링 코드와 분리하는 인터페이스입니다. 다른 API용 셰이더 변환 도구도 있지만 일반 게임 빌드와 실행 경로에는 아직 연결하지 않았습니다.

최근 RHI 수정은 빌드 및 실제 GPU 동작 검증이 추가로 필요합니다. 현재 다른 API로 전환해 사용할 수 있는 상태는 아닙니다.

### 설치와 실행

1. Windows에 Visual Studio 2022와 C++ 데스크톱 개발 도구를 설치합니다.
2. C# 스크립트에는 .NET 8 x64 Runtime과 Visual Studio C# 컴파일러가 필요합니다. 게임 빌드/배포 작업에는 .NET 8 SDK도 설치합니다.
3. `GenerateProjects.bat`으로 프로젝트를 생성하거나 갱신합니다.
4. `CCEngine.sln`을 열어 `Debug x64` 또는 `Release x64`로 빌드합니다. PowerShell에서는 `.\tools\BuildEngine.ps1 -Configuration Debug`를 사용할 수 있습니다.
5. `Sandbox`를 실행합니다. 작업 디렉터리는 저장소의 `Sandbox` 폴더로 지정합니다.

C# 샘플은 `Sandbox`에서 `.\assets\Scripts\BuildScripts.ps1`을 실행해 빌드합니다. 이 스크립트는 현재 Visual Studio 2022 Community의 C# 컴파일러 경로를 사용하므로 다른 에디션은 `$csc` 경로를 수정해야 합니다. 생성된 DLL은 커밋하지 않습니다.

게임 배포는 Project Settings에서 시작 씬을 지정한 뒤 `File > Build Settings...`에서 진행합니다. 빌드 결과 폴더 전체를 배포해야 합니다. Self-contained 설정은 .NET Runtime을 함께 포함합니다. 자세한 옵션과 오류 해결은 [사용자 설명서](docs/CCEngine_User_Guide.md#게임-빌드와-배포)를 참고하세요.

### 설명서와 샘플

- [사용자 설명서](docs/CCEngine_User_Guide.md): 설치, 씬 편집, 물리 표시, 게임 빌드와 배포.
- [C# 스크립팅](docs/CCEngine_Scripting.md): 첫 스크립트, Inspector 필드, 생명주기와 문제 해결.
- [로그와 문제 확인](docs/SCRIPTING_DEBUG_LOGS.md): Console과 물리 이벤트 로그.
- `Sandbox/assets/scenes/`: `Collision_Test`, `Animator_Test`, `Physics3D_QA` 테스트 씬.

기존 PPTX 학습 자료보다 위 Markdown 설명서의 API와 작업 순서를 우선합니다.
