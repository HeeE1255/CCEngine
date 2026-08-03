# C# Debug Logs

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
