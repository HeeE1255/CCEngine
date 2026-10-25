# CCEngine 사용자 설명서

CCEngine으로 씬을 편집하고 C# 스크립트를 연결하는 첫 사용 과정을 안내합니다. 메뉴 이름은 실제 에디터의 영문 표기를 사용합니다.

## 읽는 순서

1. 이 문서에서 설치와 씬 편집을 익힙니다.
2. [C# 스크립팅](CCEngine_Scripting.md)의 이동 예제를 따라 합니다.
3. [로그와 문제 확인](SCRIPTING_DEBUG_LOGS.md)으로 실행 결과를 확인합니다.

## 설치와 첫 실행

Windows, Visual Studio 2022의 C++ 데스크톱 개발 도구와 Windows SDK가 필요합니다. C# 기능에는 .NET 8 x64 런타임과 C# 컴파일러도 필요합니다.

1. 저장소 루트의 `GenerateProjects.bat`를 실행합니다.
2. `CCEngine.sln`을 Visual Studio로 엽니다.
3. 구성은 `Debug`, 플랫폼은 `x64`를 선택하고 솔루션을 빌드합니다.
4. `Sandbox`를 시작 프로젝트로 지정하고 실행합니다.

명령줄에서는 저장소 루트에서 다음과 같이 실행합니다.

```powershell
Set-Location .\Sandbox
& ..\bin\Debug-windows-x86_64\Sandbox.exe
```

작업 폴더는 `Sandbox`여야 합니다. 엔진이 이 폴더를 기준으로 `assets`와 프로젝트 설정을 찾습니다.

## 그래픽 API 선택과 복구

DirectX 11이 기본값입니다. DirectX 12, OpenGL 4.5, Vulkan은 해당 백엔드를 포함해 빌드한 실행 파일과 이를 지원하는 GPU/드라이버가 필요합니다.

### 백엔드 포함 빌드

Vulkan SDK 설치 후 저장소 루트에서 실행합니다. `VULKAN_SDK` 환경 변수가 없다면 `-VulkanSDK`로 SDK 폴더를 지정하세요.

```powershell
.\tools\BuildEngine.ps1 -Configuration Release -EnableDX12 -EnableOpenGL -EnableVulkan
```

필요한 옵션만 지정해도 됩니다. 옵션을 생략한 백엔드는 포함되지 않으며 DX11은 기본으로 유지됩니다. Vulkan을 선택하면 컴파일 전에 SDK 헤더와 x64 라이브러리 존재 여부를 검사합니다.

### 에디터와 Player 설정

1. Project Settings에서 에디터의 Graphics API를 선택하고 설정을 저장합니다. `N/A`는 현재 실행 파일에서 사용할 수 없는 백엔드입니다.
2. 편집 중인 씬을 따로 저장한 뒤 에디터를 종료하고 다시 실행합니다. 프로젝트 설정 저장이 씬 저장을 대신하지는 않습니다.
3. 선택한 API는 Scene View뿐 아니라 에디터 UI와 보조 창에도 적용됩니다. 실행 도중 장치를 교체하지 않습니다.
4. Player는 Build Settings에서 API를 별도로 지정합니다. 에디터 API를 바꿨다고 기존 Player 패키지가 바뀌지는 않습니다.

비 DX11 백엔드는 Python, DXC, SPIRV-Cross로 셰이더 패키지를 준비합니다. 에디터의 Shader Tools 설정에서 도구 경로를 지정하고 진단을 확인하세요. 배포 시에는 생성된 셰이더 패키지를 포함한 빌드 폴더 전체가 필요합니다.

### API 변경 후 시작하지 못할 때

에디터를 종료하고 작업 폴더를 프로젝트 폴더로 지정한 뒤 `-dx11`로 실행합니다. 이 옵션은 저장된 API보다 우선하지만 설정 파일 자체를 바꾸지는 않습니다.

```powershell
Set-Location .\Sandbox
& ..\bin\Release-windows-x86_64\Sandbox.exe -dx11
```

열린 Project Settings에서 DirectX 11을 선택하고 저장하면 이후 일반 실행에도 적용됩니다. API 옵션은 한 번에 하나만 사용하세요. 다른 API 실행 옵션은 `-dx12`, `-opengl`, `-vulkan`입니다.

| 증상 | 확인할 내용 |
| --- | --- |
| API가 N/A이거나 실행 불가 | 백엔드 빌드 옵션과 GPU/드라이버 지원 여부 |
| 셰이더 준비 실패 | Shader Tools의 Python/DXC/SPIRV-Cross 경로와 첫 오류 메시지 |
| Player에서 패키지 로드 실패 | 선택 API와 패키지 일치 여부, 빌드 폴더 전체 배포 여부 |
| 새 빌드 검증 실패 | 실패 로그를 확인하고 원인을 수정한 뒤 다시 빌드. 이전 성공 결과를 새 결과로 혼동하지 않기 |

현재 API 전환은 수동 저장·종료·재실행 방식입니다. 자동 재시작과 미저장 씬 확인 대화상자는 별도 후속 기능입니다.

## 화면 구성

| 창 | 용도 |
| --- | --- |
| Hierarchy | 씬의 오브젝트 선택 |
| Inspector | 선택한 오브젝트, 에셋, Animator 상태 등의 속성 편집 |
| Scene View | 오브젝트 배치와 편집 |
| Game View | 게임 카메라 화면 확인 |
| Asset Browser | 에셋 찾기와 선택 |
| Console | 로그와 오류 확인 |
| History | 편집 작업 이력 확인 |

닫은 창은 상단 `Window` 메뉴에서 다시 엽니다. Inspector 내용은 현재 선택한 대상에 따라 바뀝니다.

## 씬 열기와 저장

1. `File > Open Scene`에서 `.ccscene` 파일을 선택합니다.
2. `Sandbox/assets/scenes/Collision_Test.ccscene`으로 오브젝트 선택과 Inspector 편집을 연습합니다.
3. 같은 파일에 저장하려면 `File > Save`, 별도 파일로 저장하려면 `File > Save As...`를 사용합니다.

씬은 오브젝트 배치와 컴포넌트 값을 저장합니다. 프리팹은 오브젝트 구성을 재사용하는 에셋입니다. 선택한 오브젝트를 `File > Save Prefab`으로 저장하고 `File > Load Prefab`으로 불러올 수 있습니다.

`Animator_Test.ccscene`, `Physics3D_QA.ccscene`도 저장소에 포함됩니다.

## 실행과 편집

상단 재생 버튼으로 Play를 시작하고 일시정지 버튼으로 실행을 멈춥니다. 정지 버튼으로 편집 상태에 돌아옵니다.

Play는 편집 씬의 복사본에서 동작합니다. 실행 중 바꾼 위치나 컴포넌트 값을 유지하려면 정지한 뒤 편집 씬에 다시 적용하고 저장하세요. 스크립트나 공유 에셋 파일을 직접 저장한 변경은 디스크에 남으며, Play 종료로 취소되지 않습니다.

## 물리 표시 확인

`View > Physics`에서 `Enabled`를 On으로 설정합니다.

| 옵션 | 의미 |
| --- | --- |
| Dimension | 2D, 3D 또는 둘 다 표시 |
| Scope | Selected는 선택 대상, All은 전체 대상 표시 |
| Collider Shapes | 충돌 형상의 외곽선 표시 |
| Mesh Wire | Mesh Collider의 메시 선 표시 |
| Body Centers | Rigidbody가 있는 오브젝트의 중심 마커 표시 |
| Triggers | Highlight는 강조, Only는 트리거 대상만 표시 |

트리거는 주황색입니다. 중심 마커는 Static 회색, Dynamic 초록, Kinematic 파랑입니다. 중심 마커는 물체에 가려져도 보이며, 오브젝트 원점 기준입니다. 정밀한 질량 중심 측정값을 뜻하지는 않습니다.

표시되지 않으면 Enabled, Dimension, Scope와 선택 대상의 컴포넌트를 확인하세요. Rigidbody가 없는 Collider에는 Body Center가 표시되지 않습니다. 이 옵션은 Scene View 확인용이며 실제 충돌 설정을 바꾸지 않습니다.

## 게임 빌드와 배포

게임 빌드는 에디터와 분리된 `CCGame` Player와 필요한 에셋만 출력 폴더에 만듭니다. 배포 PC에는 Visual Studio, 엔진 소스, .NET 설치가 필요하지 않습니다. 빌드하는 PC에는 C++ 도구가 포함된 Visual Studio와 .NET 8 SDK가 필요합니다.

### 빌드 순서

1. `Project Settings`에서 Start Scene, 게임 해상도를 지정하고 저장합니다.
2. `File > Build Settings...`를 엽니다.
3. Product Name, Product Version, Update Channel, Executable Name, Output Folder를 확인합니다.
4. 배포본은 `Release`, `Self-contained .NET 8`을 선택합니다.
5. 동적 경로로 불러오는 에셋은 `Always Include > Add Asset...`으로 추가합니다.
   - 포함된 에셋은 `폴더/파일.ext` 또는 `assets/폴더/파일.ext` 주소로 Runtime Manifest에 등록됩니다.
   - C# 공개 문자열 필드에 `[AssetReference("Texture")]`처럼 표시하면 Scene/Prefab에 저장된 값도 빌드 의존성으로 수집됩니다.
6. Material의 Toggle 속성은 Shader Keyword로 취급됩니다. 빌드는 실제 Material에서 사용한 조합만 Variant로 컴파일합니다.
   - Release에서는 Shader 오류가 즉시 빌드를 실패시킵니다.
   - Development의 `Shader Fallback`을 켜면 오류 Variant를 Error Shader로 대체하고 경고를 남깁니다.
7. `Verify First Frame`을 켭니다. 시작 씬을 실제 Player로 열고 첫 화면을 GPU에서 읽어 검증합니다.
8. `Build`를 누릅니다. 바로 실행하려면 `Build And Run`을 사용합니다.

`Development`는 진단용입니다. Debug Symbols를 켜면 네이티브 PDB를 `Symbols` 폴더에 포함합니다. `Release`는 심볼과 HLSL 원본을 제외하고, 빌드 시 검증된 Shader bytecode만 사용합니다.

### 결과 폴더

| 항목 | 내용 |
| --- | --- |
| `<Game>.exe`, `CCEngine.dll` | 게임 Player와 엔진 Runtime |
| `assets/` | 시작 씬에서 실제로 참조한 에셋 |
| `ShaderCache/` | 사전 컴파일된 Shader bytecode |
| `Runtime/dotnet/` | Self-contained .NET Runtime |
| `GameProject.json` | 시작 씬, 해상도, 창 모드 등 Player 설정 |
| `BuildManifest.json` | 제품 버전, 업데이트 채널, 파일별 크기/해시, 전체 Build Fingerprint |

같은 설정으로 다시 빌드하면 이전 성공 빌드에서 해시가 같은 파일을 재사용합니다. 빌드는 임시 Staging에서 완성한 뒤에만 결과 폴더를 교체하므로, 실패해도 직전 성공 빌드는 유지됩니다.

`Verify First Frame`은 단순히 프로세스가 켜졌는지만 확인하지 않습니다. 시작 씬 로드, GPU Back Buffer 읽기, 화면 크기와 명암 분포를 검사합니다. 검은 화면이나 단색 오류 화면이면 빌드를 게시하지 않습니다. 의도적으로 단색 화면만 사용하는 프로젝트는 이 옵션을 끌 수 있지만, 배포 전에는 실제 게임 씬으로 다시 켜서 확인하는 것을 권장합니다.

배포할 때는 결과 폴더 전체를 함께 옮기세요. 실행 파일만 따로 복사하면 DLL, Shader 또는 .NET Runtime이 누락됩니다.

### 출시 후보 검증과 배포 패키지

Release 결과는 배포 전에 다음 도구로 검사합니다.

```powershell
.\tools\VerifyReleaseCandidate.ps1 -BuildRoot .\Build\Windows
.\tools\New-ReleasePackage.ps1 -BuildRoot .\Build\Windows
```

검증 도구는 Manifest Version, Release 구성, 경로 탈출, 누락/추가 파일, 크기와 해시를 확인합니다. 패키지 도구는 검증을 통과한 결과만 ZIP과 SHA-256 체크섬으로 만듭니다.

이전 버전과 새 버전의 차이 패치는 다음처럼 만듭니다. 패치에는 추가/교체 파일, 삭제 목록, 기준/대상 Build Fingerprint가 기록됩니다.

```powershell
.\tools\New-BuildPatch.ps1 -BaseBuildRoot .\Build\0.1.0 -TargetBuildRoot .\Build\0.1.1 -OutputDirectory .\Build\Patch
.\tools\Apply-BuildPatch.ps1 -InstallRoot .\InstalledGame -PatchRoot .\Build\Patch
```

패치 적용은 설치본과 기준 Fingerprint가 다르면 중단합니다. 일치하면 별도 staging을 만든 뒤 전체 해시를 검증하고 폴더를 교체하므로 실패 중인 설치본을 직접 훼손하지 않습니다. 코드 서명은 인증서를 Windows 인증서 저장소에 설치한 뒤 `SignRelease.ps1`에 Thumbprint를 전달합니다. 개인 키나 인증서 암호는 프로젝트 파일에 저장하지 않습니다.

Player 로그는 `%LOCALAPPDATA%/CCEngine/Logs/<Product>/`에 최대 5개 세션까지 회전 보관됩니다. 각 파일은 기본 5MB에서 회전해 장기 실행 중 디스크를 무제한 사용하지 않습니다.

### 빌드 오류 확인

| 오류 | 확인할 항목 |
| --- | --- |
| Start scene is missing | Project Settings의 Start Scene 저장 여부 |
| Referenced GUID is missing | Inspector의 Missing 에셋 슬롯과 `.meta` GUID |
| required asset is excluded | `local`, QA 폴더 또는 라이선스 제외 폴더 에셋을 씬이 참조하는지 확인 |
| Shader compile failed | 표시된 HLSL 경로와 VSMain/PSMain, Material 슬롯 규칙 확인 |
| Missing script class | 씬의 Script Class 이름과 C# namespace/class 이름 확인 |
| Visual C++ runtime not found | Visual Studio Installer에서 MSVC v143 재배포 구성요소 설치 |
| Required DLL is neither packaged nor provided by Windows | 표시된 DLL을 Binary 출력에 포함하거나 잘못된 네이티브 플러그인을 제거 |
| Existing game executable is running | 표시된 PID의 Player를 종료한 뒤 다시 빌드 |
| Build output file is locked | Player, 디버거, DLL/PDB 뷰어가 표시된 파일을 사용 중인지 확인 |
| Player Smoke Test failed | Start Scene의 Primary Camera, Shader, 화면 출력과 Player 로그 확인 |

빌드 서비스는 Player와 루트 DLL의 PE Import Table을 검사합니다. Windows 기본 DLL도 아니고 패키지에도 없는 종속 DLL이 있으면 긴 빌드가 끝난 뒤 실행 실패로 발견하지 않고 Staging 단계에서 중단합니다.

출력 폴더를 수동으로 재사용할 때 `.ccengine-build` Marker가 없으면 엔진은 해당 폴더를 삭제하거나 덮어쓰지 않습니다. 다른 폴더를 선택하거나 비어 있는 출력 폴더를 사용하세요.

엔진 개발자는 `tools/BuildEngine.ps1`로 검증된 최신 amd64 MSBuild를 선택할 수 있습니다. `tools/VerifyBuildMatrix.ps1`은 Debug/Release Editor, Development/Release Player, 이동된 Release Player를 검사하고, `tools/VerifyBuildStability.ps1`은 같은 입력의 반복 빌드와 Manifest 결정성을 검사합니다. 결과 파일은 Git에 포함되지 않는 `local/` 아래에 저장됩니다.

## 기존 학습 자료

[스크립팅 PPT](CCEngine_Scripting_Guide.pptx)와 [가이드북 PPT](CCEngine_Scripting_Guidebook.pptx)는 이전 버전 자료입니다. 최신 API와 사용 절차는 [C# 스크립팅](CCEngine_Scripting.md)을 기준으로 확인하세요.
