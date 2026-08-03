# CCEngine

CCEngine is a Windows desktop game engine/editor project built with C++ and DirectX 11.
It is developed as an engine portfolio project with editor tooling, scene editing, asset browsing, scripting, and runtime test scenes.

## Current Scope

The repository includes the engine source, Sandbox editor application, public sample scripts, shaders, font asset, and small test scenes.

Included Sandbox assets:

- `Sandbox/assets/Scripts/`
- `Sandbox/assets/fonts/NotoSansKR-VariableFont_wght.ttf`
- `Sandbox/assets/shaders/*.hlsl`
- `Sandbox/assets/scenes/ActiveLifecycleTest.ccscene`
- `Sandbox/assets/scenes/PhysicsDebugTest.ccscene`

Large third-party model/texture packs are intentionally not included because of redistribution restrictions.

## Requirements

- Windows
- Visual Studio 2022
- C++ desktop development workload
- PowerShell
- .NET SDK, only required when building C# gameplay scripts

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

The user is expected to build the C++ solution directly in Visual Studio.

## Run

After building, run the `Sandbox` project from Visual Studio.

The editor should open with the default editor layout and the included Sandbox assets.

Useful test scenes:

- `assets/scenes/ActiveLifecycleTest.ccscene`
- `assets/scenes/PhysicsDebugTest.ccscene`

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

- `docs/CCEngine_Scripting_Guide.pptx`
- `docs/CCEngine_Scripting_Guidebook.pptx`
- `docs/SCRIPTING_DEBUG_LOGS.md`

The `docs/` folder is intended to be pushed to GitHub when it contains public portfolio documentation.

## Local-Only Workspace

Private notes, temporary QA files, screenshots, and scratch files should go under:

```text
local/
```

This folder is ignored by Git and will not be pushed.

Suggested usage:

- `local/qa/`
- `local/screenshots/`
- `local/notes/`
- `local/scratch/`

## Asset Policy

Only redistributable or engine-owned sample assets should be committed.

Do not commit:

- licensed character/model packs
- private test images
- generated thumbnails
- editor cache/trash files
- build intermediate files

The `.gitignore` is configured to keep `Sandbox/assets/Chocolate rice/` and other local-only test assets out of Git.

## Main Implemented Areas

- Multi-window editor UI and docking workflow
- Scene hierarchy, inspector, scene view, and game view
- Asset browser with metadata/GUID handling
- Asset operation undo/redo
- File watcher and asset refresh flow
- Scene and prefab serialization
- Mesh, light, camera, Rigidbody2D, collider, and script components
- C# gameplay scripting foundation
- Play mode lifecycle events
- Physics debug visualization
- 2D and 3D collider debug rendering

## Notes

This project is still under active development. The included scenes are meant for basic feature verification rather than final game content.
