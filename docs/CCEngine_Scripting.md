# C# 스크립팅

[사용자 설명서로 돌아가기](CCEngine_User_Guide.md)

스크립트는 실행 중 오브젝트가 무엇을 할지 정하는 C# 코드입니다. 파일을 만든 뒤 오브젝트에 컴포넌트로 연결해야 실행됩니다.

## 준비와 파일 위치

현재 빌드 도구는 .NET 8 x64 런타임과 Visual Studio 2022 Community의 C# 컴파일러를 사용합니다. 다른 에디션을 사용한다면 `BuildScripts.ps1`의 `$csc` 경로를 설치된 컴파일러 위치에 맞추세요.

| Sandbox 기준 위치 | 내용 |
| --- | --- |
| `assets/Scripts/Game/` | 직접 작성하는 게임 스크립트 |
| `assets/Scripts/ScriptCore/` | 엔진에서 제공하는 C# API |
| `assets/Scripts/Build/` | 생성되는 DLL과 필드 목록 |
| `assets/Scripts/BuildScripts.ps1` | 스크립트 컴파일 도구 |

## 첫 스크립트 만들기

1. Play를 정지하고 Hierarchy에서 움직일 오브젝트를 선택합니다. 첫 연습에는 Dynamic Rigidbody가 없는 오브젝트를 사용하세요.
2. Inspector의 `Add Component > New C# Script...`를 선택합니다.
3. `assets/Scripts/Game/FirstMove.cs`로 저장합니다.
4. 코드 편집기에서 파일을 열고 다음 내용으로 바꿉니다.

```csharp
using CCEngine;

namespace Game
{
    public sealed class FirstMove : GameScript
    {
        [Range(0.0f, 10.0f)]
        public float Speed = 1.0f;

        protected override void Start()
        {
            Debug.Log("FirstMove started.");
        }

        protected override void Update(float deltaTime)
        {
            Translation += new Vector3(0.0f, Speed * deltaTime, 0.0f);
        }
    }
}
```

5. 저장하고 Console에서 컴파일 성공을 확인합니다.
6. Inspector에서 `Game.FirstMove` 연결과 Speed 필드를 확인합니다. Speed를 1로 설정하고 씬을 저장합니다.
7. Play를 누릅니다. 시작 로그가 한 번 출력되고 Y 위치가 매초 약 1씩 증가하면 정상입니다.

`Translation`은 스크립트가 붙은 오브젝트의 위치입니다. `deltaTime`은 프레임 사이에 흐른 시간(초)입니다. 속도에 이 값을 곱하면 프레임 수가 달라도 이동 속도를 일정하게 유지할 수 있습니다.

## 기존 스크립트 연결

직접 만든 `.cs` 파일은 Game 폴더에 저장합니다. 컴파일 성공 후 오브젝트의 `Add Component`에서 `Game.FirstMove`처럼 클래스 이름을 선택합니다. 파일 이름과 클래스 이름을 같게 관리하면 찾기 쉽습니다.

현재 일반 Script 컴포넌트는 오브젝트당 하나입니다. 이미 Script가 있다면 새 스크립트 추가가 제한됩니다.

## Inspector 필드

지원하는 타입의 public 인스턴스 필드를 편집할 수 있습니다.

| 타입 | 사용 예 |
| --- | --- |
| float | 속도, 시간 |
| int | 체력, 횟수 |
| bool | 기능 사용 여부 |
| string | 이름, 메시지 |
| Vector3 | X/Y/Z 값 |

private, static, const, C# readonly 필드와 일반 프로퍼티(`get; set;`)는 이 목록에 포함되지 않습니다.

```csharp
[Range(0.0f, 10.0f)]
public float Speed = 1.0f;

[Drag(0.1f)]
public float TurnSpeed = 2.0f;

[Step(1)]
public int Health = 100;

[ReadOnly]
public float DisplayValue = 0.0f;
```

Range는 범위 조절, Drag는 드래그 조절, Step은 일정 간격 증감용입니다. `[ReadOnly]`는 Inspector 편집을 막는 표시 설정이며 C# readonly와 다릅니다. 실시간 런타임 값 감시 기능을 뜻하지는 않습니다.

Inspector에서 바꾼 값은 씬이나 프리팹을 저장해야 남습니다. 코드 초기값을 변경해도 오브젝트에 이미 저장된 값이 우선할 수 있습니다. 필드나 클래스 이름을 변경했다면 기존 오브젝트의 연결과 값을 다시 확인하세요.

## 함수 호출 시점

| 함수 | 용도 |
| --- | --- |
| Awake() | 인스턴스 초기화 |
| OnEnable() | 활성화될 때 처리 |
| Start() | 처음 실행을 시작할 때 처리 |
| FixedUpdate(float deltaTime) | 고정 시간 간격 처리 |
| Update(float deltaTime) | 프레임마다 처리 |
| LateUpdate(float deltaTime) | Update 이후 처리 |
| OnDisable() | 비활성화될 때 처리 |
| OnDestroy() | 인스턴스 제거 시 정리 |

오브젝트 자신이나 부모의 Active, Script의 Enabled 상태가 실행 여부에 영향을 줍니다.

이전 예제의 OnCreate와 OnUpdate도 호환됩니다. 기본 Awake가 OnCreate를, 기본 Update가 OnUpdate를 호출합니다. Awake나 Update를 재정의하면 기본 연결이 실행되지 않으므로 같은 역할을 두 함수에 나눠 넣지 마세요.

## 컴파일과 저장

에디터가 `.cs` 변경을 감지해 자동 컴파일합니다. 파일을 저장한 뒤 Console에서 성공 여부를 확인하세요. 오류가 있으면 표시된 파일과 줄 번호를 확인하고 수정 후 다시 저장합니다.

수동 빌드는 저장소 루트에서 다음과 같이 실행합니다.

```powershell
Set-Location .\Sandbox
.\assets\Scripts\BuildScripts.ps1
```

성공하면 Build 폴더에 `CCEngine.ScriptCore.dll`, `GameScripts.dll`, `GameScripts.manifest.json`이 생성됩니다. manifest는 에디터가 클래스와 필드 목록을 읽는 파일입니다. 생성 파일은 직접 편집하지 마세요.

코드를 수정한 뒤에는 Play를 정지하고 컴파일 성공을 확인한 다음 다시 실행하세요. 코드 저장과 씬 저장은 별도 작업입니다.

## 문제 해결

| 증상 | 확인 순서 |
| --- | --- |
| 클래스가 목록에 없음 | 컴파일 오류, GameScript 상속, public 클래스, Game 폴더 확인 |
| 필드가 없음 | 컴파일 성공, 지원 타입의 public 인스턴스 필드인지 확인 |
| 움직이지 않음 | 스크립트 연결, Active/Enabled, Pause, Speed 확인 |
| 정지 후 값이 돌아감 | 편집 상태에서 다시 적용하고 씬 저장 |
| C# compiler was not found | BuildScripts.ps1의 컴파일러 경로 확인 |
| .NET 8 x64 runtime was not found | .NET 8 x64 런타임 설치 확인 |

충돌과 트리거 예제는 [로그와 문제 확인](SCRIPTING_DEBUG_LOGS.md)을 참고하세요.
