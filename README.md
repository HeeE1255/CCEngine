# CCEngine

CCEngine is a Windows C++ game engine and editor with C# scripting, animation authoring, physics tools, and a standalone game build pipeline.
DirectX 11, DirectX 12, OpenGL 4.5, and Vulkan are integrated through a shared Rendering Hardware Interface (RHI), with editor and standalone player support on Windows.

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

These are implemented development features, not a claim of production certification. Compatibility depends on the graphics device, driver, and enabled build options.

### Rendering Status

| Backend | Current status |
| --- | --- |
| DirectX 11 | Default backend; editor and standalone player |
| DirectX 12 | Integrated editor/player backend with shared resources and per-window contexts |
| OpenGL 4.5 | Integrated editor/player backend with shared resources and per-window contexts |
| Vulkan | Integrated editor/player backend with shared resources and per-window contexts |

The RHI separates common rendering code from graphics API implementations. Shader packages for non-DX11 backends are integrated into editor startup and game builds. The toolchain includes offline compilation, reflection/layout validation, incremental shader caching, and first-frame validation for packaged players.

Choose the editor API in Project Settings, save, and restart the editor. The selected backend drives both scene rendering and editor UI; this is not a live viewport-only switch. Build Settings selects the standalone player's API separately. A backend must be compiled in, supported by the device, and supplied with compatible shader packages.

The current RHI implementation and execution-validation scope is complete, including editor multi-window rendering, player packaging, and minimize/restore behavior. This does not certify every GPU or driver combination.

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

To include all graphics backends, run this from a Visual Studio Developer PowerShell after installing the Vulkan SDK:

```powershell
MSBuild.exe CCEngine.sln /m:1 /p:Configuration=Release /p:Platform=x64 /p:CCEnableDX12=true /p:CCEnableOpenGL=true /p:CCEnableVulkan=true /p:CCVulkanSDK="$env:VULKAN_SDK"
```

Non-DX11 shader preparation uses Python, DXC, and SPIRV-Cross. Configure their paths in the editor's shader tool settings. `tools/CompileShaderBackends.py --help` lists standalone tool options. The default build does not enable the optional backend flags above.

The build helper also accepts `-EnableDX12 -EnableOpenGL -EnableVulkan` and an optional `-VulkanSDK` path (otherwise `VULKAN_SDK` is used). See the [graphics API guide](docs/CCEngine_User_Guide.md#그래픽-api-선택과-복구) for selection and startup recovery.

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

- **DirectX 11:** 기본 백엔드이며 에디터와 독립 실행 Player를 지원합니다.
- **DirectX 12 / OpenGL 4.5 / Vulkan:** 에디터와 Player 실행 경로에 연결되어 있으며, 공유 리소스와 창별 컨텍스트를 지원합니다.
- **RHI:** 공통 렌더링 코드와 그래픽 API 구현을 분리합니다. 셰이더 패키지 준비, reflection 기반 데이터 배치 검증, 증분 캐시와 Player 첫 화면 검증도 연결되어 있습니다.

현재 계획한 RHI 구현 및 실행 검증 범위는 완료했습니다. 멀티 윈도우 렌더링, Player 패키징, 최소화와 복원을 확인했으며, 모든 GPU와 드라이버 조합을 보증한다는 의미는 아닙니다.

에디터 API는 Project Settings에서 선택하고 저장한 뒤 에디터를 재시작하면 적용됩니다. 씬 화면만 바꾸는 방식이 아니라 에디터 UI까지 같은 백엔드로 구동합니다. 배포할 Player의 API는 Build Settings에서 별도로 선택합니다. 해당 백엔드가 빌드에 포함되어 있고 GPU 지원과 셰이더 패키지가 준비되어 있어야 합니다.

모든 백엔드를 포함하려면 Vulkan SDK 설치 후 위 [Build](#build)의 MSBuild 명령을 사용하세요. 기본 빌드에서는 선택 백엔드가 자동 활성화되지 않습니다. 비 DX11 셰이더 준비에는 Python, DXC, SPIRV-Cross가 필요하며 에디터의 셰이더 도구 설정에서 경로를 지정합니다.

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
