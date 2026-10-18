# 로그와 문제 확인

[사용자 설명서](CCEngine_User_Guide.md) · [C# 스크립팅](CCEngine_Scripting.md)

## 먼저 확인하기

1. `Window > Console`에서 Console 창을 엽니다.
2. 아래 예제를 `assets/Scripts/Game/PlayerProbe.cs`로 저장하고 컴파일 성공을 확인합니다.
3. 오브젝트를 선택하고 `Add Component`에서 `Game.PlayerProbe`를 연결합니다.
4. Play를 누릅니다. `PlayerProbe started.`가 한 번 표시되면 스크립트가 실행된 것입니다.

충돌 로그까지 확인하려면 같은 차원의 Collider를 양쪽에 준비하고 물리로 움직일 대상에 Rigidbody를 설정합니다. 트리거 콜백은 Collider의 Is Trigger 설정이 필요합니다. 단순히 스크립트만 연결하면 충돌 로그가 나오지는 않습니다.

CCEngine 스크립트는 `CCEngine.Debug`를 통해 에디터 Console에 메시지를 남길 수 있다.
엔진은 충돌이나 트리거 이벤트를 자동으로 전부 출력하지 않는다.
필요한 지점에서 사용자가 직접 로그를 남기는 방식이 기본이다.

## 기본 사용

```csharp
using CCEngine;

namespace Game
{
    public sealed class PlayerProbe : GameScript
    {
        protected override void Start()
        {
            Debug.Log("PlayerProbe started.");
        }

        protected override void OnCollisionEnter2D(uint otherEntityID)
        {
            Debug.Log($"Hit entity {otherEntityID}");
        }

        protected override void OnTriggerExit2D(uint otherEntityID)
        {
            Debug.Warn($"Left trigger {otherEntityID}");
        }
    }
}
```

## 로그 레벨

- `Debug.Log(message)`는 일반 정보를 남긴다.
- `Debug.Warn(message)`는 경고를 남긴다.
- `Debug.Error(message)`는 오류를 남긴다.

Console 패널에서는 레벨에 맞는 색으로 표시된다.
네이티브 Console에는 `[C#]` 접두어가 붙어서 엔진 로그와 스크립트 로그를 구분할 수 있다.

## 이벤트 로그 작성 방식

아래 함수는 앞의 `PlayerProbe` 클래스 안에 작성하는 예입니다. 같은 이름의 함수를 중복으로 넣지 말고 기존 함수를 수정하세요. 3D 물리에서는 `OnCollisionEnter3D`와 `OnTriggerEnter3D`처럼 `3D` 콜백을 사용합니다. 인자는 동일하게 `uint otherEntityID`입니다.

충돌과 트리거 콜백은 일반 스크립트 콜백이다.
게임플레이 동작을 확인하고 싶은 콜백 안에 직접 로그를 넣는다.

```csharp
protected override void OnCollisionEnter2D(uint otherEntityID)
{
    Debug.Log($"Collision Enter with entity {otherEntityID}");
}

protected override void OnTriggerEnter2D(uint otherEntityID)
{
    Debug.Log($"Trigger Enter with entity {otherEntityID}");
}
```

이 방식은 플레이 중 Console이 불필요한 이벤트 로그로 도배되는 것을 막는다.
엔진 내부는 QA용 이벤트 큐를 유지할 수 있지만, 사용자에게 보이는 로그는 스크립트가 직접 제어한다.

## 기존 헬퍼

`GameScript.Log(message)`도 간단한 스크립트를 위해 유지된다.
동작은 `Debug.Log(message)`와 같은 일반 로그다.

새 스크립트에서는 `Debug.Log`, `Debug.Warn`, `Debug.Error`를 우선 사용한다.

## 로그가 보이지 않을 때

| 증상 | 확인할 내용 |
| --- | --- |
| 시작 로그도 없음 | 컴파일 성공, 스크립트 연결, Active/Enabled, Play 상태 |
| 시작 로그만 나옴 | Collider 차원, Rigidbody와 Trigger 설정, 실제 접촉 여부 |
| 로그가 너무 많이 나옴 | Update나 Stay 콜백에서 매번 출력하는지 확인 |
| Debug 이름이 모호하다는 컴파일 오류 | `CCEngine.Debug.Log(...)`처럼 전체 이름 사용 |

로그의 `otherEntityID`는 상대 오브젝트 식별 번호입니다. 오브젝트 이름이 아닙니다. 반복 출력이 필요 없다면 Stay 대신 접촉 시작을 뜻하는 Enter나 종료를 뜻하는 Exit에 로그를 작성하세요.
