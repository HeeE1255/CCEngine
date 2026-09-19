#pragma once
#include <DirectXMath.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <box2d/id.h>
#include "entt.hpp"
#include "Renderer/Mesh.h"
#include "Scene/ScriptableEntity.h"
#include "Renderer/Texture.h"
#include "Renderer/Model.h"
#include "Renderer/MaterialAsset.h"
#include "Animation/Animator.h"

namespace CCEngine
{
    // 이름표 컴포넌트 (유니티의 GameObject 이름)
    struct TagComponent
    {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag)
            : Tag(tag) {
        }
    };

    struct ActiveComponent
    {
        // 사용자가 직접 켜고 끄는 값이다. 부모 상태는 여기에 섞지 않는다.
        // 실제 런타임 활성 여부는 Scene::IsEntityActiveInHierarchy에서 부모 체인까지 계산한다.
        bool ActiveSelf = true;

        ActiveComponent() = default;
        ActiveComponent(const ActiveComponent&) = default;
    };

    // 위치/크기/회전 컴포넌트 (Transform)
    struct TransformComponent
    {
        DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };

        DirectX::XMFLOAT3 EulerRotation = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 QuaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // Identity Quaternion

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const DirectX::XMFLOAT3& translation)
            : Translation(translation) {
        }

        void SetRotationEuler(float x, float y, float z)
        {
            Rotation = { DirectX::XMConvertToRadians(x), DirectX::XMConvertToRadians(y), DirectX::XMConvertToRadians(z) };
            DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(x, y, z);
            DirectX::XMStoreFloat4(&QuaternionRotation, quat);
        }
        // 나중에 여기서 SRT 변환 행렬을 뽑아내는 함수를 추가
    };

    // 스프라이트(그림) 렌더러 컴포넌트
    struct SpriteRendererComponent
    {
        DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const DirectX::XMFLOAT4& color)
            : Color(color) {
        }
    };

    struct NativeScriptComponent
    {
        ScriptableEntity* Instance = nullptr;

        // 클래스를 new로 생성하고 delete로 지워줄 팩토리 함수 포인터
        using InstantiateFunction = ScriptableEntity * (*)();
        using DestroyFunction = void (*)(NativeScriptComponent*);

        InstantiateFunction InstantiateScript = nullptr;
        DestroyFunction DestroyScript = nullptr;

        // 템플릿 함수로 스크립트 클래스의 인스턴스를 생성/파괴하는 함수를 바인딩
        template<typename T>
        void Bind()
        {
            InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
            DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
        }
    };

    struct ScriptComponent
    {
        // 파일 경로가 아니라 전체 클래스명을 저장한다. 네임스페이스가 바뀌지 않으면 파일 이동과 무관하게 찾을 수 있다.
        std::string ClassName = "Game.MoveUp";
        // 인스펙터에서 바꾼 public 필드 값만 저장한다. 스크립트 기본값은 C# 코드와 manifest가 기준이다.
        std::unordered_map<std::string, std::string> FieldOverrides;
        bool Enabled = true;

        // 아래 값들은 Play 모드 복사본에서만 쓰는 실행 상태다.
        // 저장 파일에는 남기지 않고, Stop 때 복사본과 함께 버린다.
        bool RuntimeInstanceCreated = false;
        bool RuntimeAwakeCalled = false;
        bool RuntimeEnabledCalled = false;
        bool RuntimeStartCalled = false;
    };

    // 2D 강체 컴포넌트 (물리적인 몸체)
    struct Rigidbody2DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Static;
        bool FixedRotation = false;

        b2BodyId RuntimeBodyId = b2_nullBodyId;

        Rigidbody2DComponent() = default;
        Rigidbody2DComponent(const Rigidbody2DComponent&) = default;
    };

    // 2D 박스 충돌체 컴포넌트 (충돌 범위)
    struct BoxCollider2DComponent
    {
        DirectX::XMFLOAT2 Offset = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 Size = { 0.5f, 0.5f };

        // true면 물리 충돌로 밀어내지 않고 겹침 이벤트만 보낸다.
        // Unity의 Is Trigger와 같은 역할이다.
        bool IsTrigger = false;

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;

        b2ShapeId RuntimeShapeId = b2_nullShapeId;

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
    };

    // 3D 박스 충돌체 컴포넌트.
    // 2D 박스와 분리해 둬야 큐브/스피어/원통 같은 3D 메시를 XY 평면 기준으로 잘못 해석하지 않는다.
    struct BoxCollider3DComponent
    {
        DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Size = { 1.0f, 1.0f, 1.0f };
        bool IsTrigger = false;

        BoxCollider3DComponent() = default;
        BoxCollider3DComponent(const BoxCollider3DComponent&) = default;
    };

    // 3D 구 충돌체 컴포넌트. Transform 스케일이 비균등이면 디버그 표시도 타원체처럼 보인다.
    struct SphereCollider3DComponent
    {
        DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        bool IsTrigger = false;

        SphereCollider3DComponent() = default;
        SphereCollider3DComponent(const SphereCollider3DComponent&) = default;
    };

    // 3D 원통 충돌체 컴포넌트. 기본 축은 Y축이며, Transform 회전/스케일을 그대로 따라간다.
    struct CylinderCollider3DComponent
    {
        DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        float Height = 1.0f;
        bool IsTrigger = false;

        CylinderCollider3DComponent() = default;
        CylinderCollider3DComponent(const CylinderCollider3DComponent&) = default;
    };

    // 복잡한 메시 충돌체용 표시 컴포넌트.
    // 실제 삼각형 충돌은 별도 3D 물리 엔진 단계에서 다루고, 에디터에서는 먼저 bounds를 확인한다.
    struct MeshCollider3DComponent
    {
        DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Size = { 1.0f, 1.0f, 1.0f };
        bool Convex = false;
        bool IsTrigger = false;

        MeshCollider3DComponent() = default;
        MeshCollider3DComponent(const MeshCollider3DComponent&) = default;
    };

    //NativeScriptComponent 대신 ECS를 테스트하기 위해 추가
    // OOP 방식이 아닌 ECS 방식으로 파도 움직임을 구현하는 컴포넌트
    //struct WaveComponent 
    //{
    //    float StartX = 0.0f;
    //    float StartY = 0.0f;
    //    float TimeOffset = 0.0f;

    //    WaveComponent() = default;
    //    WaveComponent(const WaveComponent&) = default;
    //};

    struct MeshComponent
    {
        struct MaterialSlot
        {
            std::string Name = "Element 0";
            std::shared_ptr<MaterialAsset> Material;
            std::string MaterialAssetGuid;
            std::string MaterialPath;
            bool Missing = false;
        };

        // 기존 값 순서 유지. 새 프리미티브는 뒤에 추가해 기존 프리팹 Type 숫자를 깨지 않습니다.
        enum class MeshType { Custom = 0, Cube, Sphere, Plane, Quad, Capsule, Cylinder, Torus };
        MeshType Type = MeshType::Cube;

        // 실제 GPU에 올라간 버퍼 데이터를 가리키는 포인터
        std::shared_ptr<Mesh> MeshData;

        DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 기본값을 흰색으로 변경 (텍스처 색을 그대로 보기 위함)
        std::shared_ptr<Texture2D> AlbedoMap;
        // 텍스처 파일은 이름이 바뀔 수 있으므로 저장할 때 GUID를 우선 사용한다.
        std::string AlbedoAssetGuid;
        std::string AlbedoPath;
        std::shared_ptr<MaterialAsset> Material;
        // Material은 별도 에셋이므로 MeshComponent에는 참조만 저장한다.
        // 실제 색/텍스처 값은 렌더 직전에 Material 파일을 해석해 사용한다.
        std::string MaterialAssetGuid;
        std::string MaterialPath;
        // None과 Missing은 다르다. None은 기본 머티리얼로 그리고, Missing은 에러 쉐이더로 보여야 한다.
        bool MaterialMissing = false;
        // 상용 엔진은 Mesh Renderer 안에 Element 0, Element 1 같은 Material 슬롯을 둔다.
        // 현재 렌더러는 Mesh 하나를 통째로 그리므로 slot 0을 실제 렌더에 사용하고,
        // 배열 구조는 FBX submesh와 향후 부분 렌더링을 같은 데이터 구조로 받기 위한 기반이다.
        std::vector<MaterialSlot> MaterialSlots;

        MeshComponent() = default;
        MeshComponent(const MeshComponent&) = default;
        MeshComponent(MeshType type) : Type(type) {}
    };

    struct CameraComponent
    {
        float FOV = 45.0f;
        float NearClip = 0.1f;
        float FarClip = 1000.0f;

        bool Primary = true; // 이 카메라가 현재 화면을 비추는 메인 카메라인가?

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;

        // 화면 비율(Aspect Ratio)을 받아서 즉시 투영 행렬을 뽑아주는 유틸리티 함수
        DirectX::XMMATRIX GetProjectionMatrix(float aspectRatio) const
        {
            return DirectX::XMMatrixPerspectiveFovLH(
                DirectX::XMConvertToRadians(FOV),
                aspectRatio,
                NearClip,
                FarClip
            );
        }
    };

    struct AudioComponent
    {
        // 실제 소리 출력 장치는 후속 Audio Backend가 담당한다.
        // 이 컴포넌트는 Unity AudioSource처럼 "어떤 클립을 어떤 볼륨/피치로 재생할지"라는 씬 데이터를 먼저 소유한다.
        std::string AudioAssetGuid;
        std::string AudioPath;
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool PlayOnStart = false;
        bool Enabled = true;

        // 아래 Runtime 값은 저장하지 않는다. 애니메이션 Play Trigger가 0->1로 바뀌는 순간만 재생 요청을 만들기 위한 실행 상태다.
        bool RuntimePlaying = false;
        bool RuntimePlayRequested = false;
        bool RuntimeStopRequested = false;
        float RuntimeLastPlaySignal = 0.0f;

        AudioComponent() = default;
        AudioComponent(const AudioComponent& other)
            : AudioAssetGuid(other.AudioAssetGuid), AudioPath(other.AudioPath), Volume(other.Volume), Pitch(other.Pitch),
              Loop(other.Loop), PlayOnStart(other.PlayOnStart), Enabled(other.Enabled)
        {
        }
    };

    struct ModelComponent
    {
        std::shared_ptr<Model> TargetModel;
        // 모델 파일은 이동될 수 있으므로 저장용 참조는 GUID를 우선 사용한다.
        std::string AssetGuid;
        std::unordered_map<std::string, entt::entity> NodeEntityMap;
        std::unordered_map<std::string, entt::entity> NodePathEntityMap;
        bool ShowBoneLinks = false;

        ModelComponent() = default;
        ModelComponent(const std::shared_ptr<Model>& model) : TargetModel(model) {}
    };

    struct RelationshipComponent
    {
        entt::entity Parent = entt::null;
        std::vector<entt::entity> Children;

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

    struct AnimatorComponent
    {
        enum class UpdateMode
        {
            Normal = 0,
            AnimatePhysics,
            UnscaledTime
        };

        enum class CullingMode
        {
            AlwaysAnimate = 0,
            CullUpdateTransforms,
            CullCompletely
        };

        struct State
        {
            enum class MotionType { Clip = 0, BlendTree, PropertyClip };

            struct AnimationEvent
            {
                float TimeSeconds = 0.0f;
                std::string FunctionName = "OnAnimationEvent";
                std::string StringArgument;
            };

            struct BlendTreeChild
            {
                int ClipIndex = 0;
                float Threshold = 0.0f;
                DirectX::XMFLOAT2 Position = { 0.0f, 0.0f };
                float Weight = 1.0f;
            };

            struct BlendTree
            {
                enum class Type { Direct = 0, OneD, TwoD, TwoDFreeform };
                Type TreeType = Type::Direct;
                std::string ParameterX;
                std::string ParameterY;
                std::vector<BlendTreeChild> Children;
            };

            struct ClipImportSettings
            {
                std::string DisplayName;
                // FBX 파일 안의 긴 원본 클립 이름 대신 에디터에서 보여줄 이름이다.
                bool UseCustomRange = false;
                // 한 클립 전체가 아니라 시작/끝 구간만 State에서 쓰고 싶을 때 켠다.
                float StartSeconds = 0.0f;
                float EndSeconds = 0.0f;
                bool LoopPose = false;
                bool BakeRootTransform = false;
                bool LockRootPositionXZ = false;
                bool LockRootPositionY = false;
                bool LockRootRotation = false;
            };

            struct PropertyKey
            {
                enum class Interpolation { Constant = 0, Linear, EaseInOut };

                float TimeSeconds = 0.0f;
                DirectX::XMFLOAT4 Value = { 0.0f, 0.0f, 0.0f, 0.0f };
                Interpolation Interp = Interpolation::Linear;
                bool Selected = false;
            };

            struct PropertyTrack
            {
                enum class ValueType { Float = 0, Bool, Float3, Float4 };

                // 본 애니메이션이 아닌 일반 컴포넌트 값을 시간에 따라 바꾸는 경로다.
                // 예: Transform.Position, Light.Intensity, Camera.FOV.
                std::string EntityPath;
                std::string ComponentName;
                std::string PropertyName;
                ValueType Type = ValueType::Float;
                std::vector<PropertyKey> Keys;
            };

            std::string Name = "State";
            // State는 Motion 하나를 가진다. Clip, Blend Tree, Property Clip을 같은 슬롯에서 다루기 위한 기준값이다.
            MotionType Motion = MotionType::Clip;
            int ClipIndex = 0;
            std::string MotionAssetGuid;
            std::string MotionPath;
            bool Loop = true;
            float Speed = 1.0f;
            bool ApplyRootMotion = false;
            BlendTree Tree;
            ClipImportSettings ImportSettings;
            std::vector<PropertyTrack> PropertyTracks;
            // 그래프 에디터에서 보이는 노드 위치다. 애니메이션 재생값과 분리해 UI 배치만 저장한다.
            DirectX::XMFLOAT2 GraphPosition = { 260.0f, 180.0f };
            // true면 클립에 없는 본/값을 원본 기본 포즈로 되돌린다.
            // false면 클립이 건드리는 값만 쓰고 나머지는 직전 포즈를 유지한다.
            bool WriteDefaults = true;
            // 이벤트는 클립 시간 위에 찍는 마커다.
            // 런타임은 재생 시간이 이 지점을 지나갈 때 한 번만 처리하고, 저장 파일에는 마커 정보만 남긴다.
            std::vector<AnimationEvent> Events;
            int SelectedEventIndex = -1;
        };

        struct Parameter
        {
            enum class Type { Float = 0, Bool, Trigger };
            std::string Name = "Parameter";
            Type ParamType = Type::Float;
            float FloatValue = 0.0f;
            bool BoolValue = false;
        };

        struct TransitionCondition
        {
            enum class CompareMode { If = 0, IfNot, Greater, Less, Equals, NotEquals };
            std::string ParameterName;
            CompareMode Mode = CompareMode::If;
            float FloatValue = 0.0f;
            bool BoolValue = true;
        };

        struct Transition
        {
            static constexpr int AnyStateIndex = -2;
            static constexpr int ExitStateIndex = -3;

            int FromStateIndex = -1;
            int ToStateIndex = -1;
            bool HasExitTime = false;
            float ExitTime = 1.0f;
            float BlendTime = 0.15f;
            bool CanInterrupt = true;
            int Priority = 0;
            int ExitTargetStateIndex = -1;
            // 전이는 상태 인덱스와 파라미터 이름을 같이 저장한다.
            // 상태 이름은 바뀔 수 있고, 파라미터 이름은 Rename 시 조건도 같이 갱신해야 한다.
            // ExitTargetStateIndex는 하위 상태머신이 생겼을 때 Exit가 부모 그래프의 어느 State로 나갈지 남겨두는 값이다.
            // 값이 -1이면 지금처럼 레이어를 종료하고, 유효한 State 인덱스면 그 State로 이어서 전환한다.
            std::vector<TransitionCondition> Conditions;
        };

        struct Layer
        {
            enum class BlendMode { Override = 0, Additive };

            std::string Name = "Base Layer";
            float Weight = 1.0f;
            std::string MaskRootBone;
            // MaskRootBone 하나로 부족할 때 특정 본 이름이나 본 경로를 직접 넣는 목록이다.
            std::vector<std::string> MaskBoneNames;
            BlendMode Blending = BlendMode::Override;
            bool Sync = false;
            bool IKPass = false;
            // MaskRootBone이 비어 있으면 전체 본을 대상으로 한다.
            // 값이 있으면 그 본 아래만 레이어 포즈가 영향을 준다.
            bool Visible = true;
            int ActiveStateIndex = -1;
            int EntryStateIndex = -1;
            int SelectedTransitionIndex = -1;
            // 특수 노드는 State 배열의 항목이 아니지만 그래프 편집 위치는 레이어별로 보존해야 한다.
            // 고정 좌표로 그리면 레이어를 바꾸거나 큰 그래프를 정리할 때 Entry/Any State/Exit만 움직일 수 없다.
            DirectX::XMFLOAT2 EntryNodePosition = { 120.0f, 180.0f };
            DirectX::XMFLOAT2 AnyStateNodePosition = { 120.0f, 320.0f };
            DirectX::XMFLOAT2 ExitNodePosition = { 120.0f, 460.0f };
            bool Exited = false;
            // 아래 값들은 저장용 설정이 아니라 재생 중에만 쓰는 상태다.
            // State 전환과 Cross Fade는 이전 State 시간과 새 State 시간을 따로 들고 있어야 자연스럽게 섞을 수 있다.
            int PreviousStateIndex = -1;
            float StateTime = 0.0f;
            float PreviousLoopTime = 0.0f;
            float PreviousStateTime = 0.0f;
            float BlendElapsed = 0.0f;
            float BlendDuration = 0.0f;
            std::vector<int> FiredEventIndices;
            // 전환이 시작된 순간의 이전 클립을 붙잡아 둔다.
            // 매 프레임 파일에서 다시 찾지 않아야 Blend 중 프레임 끊김과 클립 교체 흔들림을 막을 수 있다.
            std::shared_ptr<AnimationClip> PreviousRuntimeClip;
            // 레이어마다 별도 상태 그래프를 가진다.
            // 이렇게 해야 새 레이어를 눌렀을 때 기존 레이어의 노드를 공유하지 않고 독립적으로 편집할 수 있다.
            std::vector<State> States;
            std::vector<Transition> Transitions;
        };

        Animator AnimPlayer;
        std::string ControllerAssetGuid;
        std::string ControllerPath;
        std::string SourceAssetGuid;
        std::string SourcePath;
        int SelectedClipIndex = 0;
        std::string SelectedClipName;
        bool AutoPlay = true;
        bool PreviewInEdit = false;
        bool Loop = true;
        float Speed = 1.0f;
        bool ApplyRootMotion = false;
        UpdateMode UpdateModeValue = UpdateMode::Normal;
        CullingMode CullingModeValue = CullingMode::CullUpdateTransforms;
        std::string AvatarGuid;
        std::string AvatarPath;
        // Source Avatar는 애니메이션 클립이 만들어진 원본 리그를 설명한다.
        // Target Avatar(AvatarGuid)는 현재 오브젝트 리그이고, 둘을 Humanoid 이름으로 이어서 Retarget한다.
        std::string SourceAvatarGuid;
        std::string SourceAvatarPath;
        // Root Motion을 계산할 때 기준으로 삼는 본 이름이다.
        // 보통 Humanoid 캐릭터는 Hips를 루트 이동 기준으로 쓴다.
        std::string HumanoidRootBone = "Hips";
        // 다른 캐릭터 골격에 애니메이션을 재사용할 때 켠다.
        // Avatar 에셋이 표준 Humanoid 본 이름과 실제 FBX 본 이름을 이어 준다.
        bool RetargetToHumanoid = false;
        bool IsPlaying = false;
        int ActiveStateIndex = -1;
        int EntryStateIndex = -1;
        int ActiveLayerIndex = 0;
        std::vector<State> States;
        // 레이어는 같은 Animator 안에서 여러 애니메이션 흐름을 겹치기 위한 단위다.
        // 아래쪽 레이어가 더 높은 우선순위를 가지며, Weight와 Mask 설정에 따라 최종 포즈에 섞인다.
        std::vector<Layer> Layers = { Layer{} };
        std::vector<Parameter> Parameters;
        std::vector<Transition> Transitions;
        int SelectedTransitionIndex = -1;

        // 에디터 전용 신호다. Inspector는 요청만 남기고, 실제 창 생성은 EditorLayer가 처리한다.
        bool EditorOpenGraphRequested = false;
        bool EditorPickControllerRequested = false;
        bool EditorOpenAvatarRequested = false;
        bool EditorPickAvatarRequested = false;

        // RuntimeClip은 실행 중에만 쓰는 캐시다.
        // 씬 파일에는 경로/GUID와 ClipIndex만 저장하고, 실제 클립 데이터는 필요할 때 다시 읽는다.
        std::shared_ptr<AnimationClip> RuntimeClip;
        std::string RuntimeClipKey;
        int RuntimePlaybackLayerIndex = -1;
        DirectX::XMFLOAT3 RuntimeRootMotionDelta = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 RuntimeRootMotionRotationDelta = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 RuntimeRootMotionRootWorld = { 0.0f, 0.0f, 0.0f };
        // 아래 값들은 Root Motion을 적용한 결과가 아니라, 에디터가 현재 프레임을 설명하기 위한 표시용 상태다.
        // 런타임 로직과 디버그 표시를 분리해 두면 잠금 옵션을 켜도 화면에서 이유를 분명히 보여줄 수 있다.
        bool RuntimeRootMotionRootMissing = false;
        bool RuntimeRootMotionWrappedLoop = false;
        bool RuntimeRootMotionLockXZ = false;
        bool RuntimeRootMotionLockY = false;
        bool RuntimeRootMotionLockRotation = false;
        bool RuntimeRootMotionBaked = false;
        float RuntimeRootMotionRotationDegrees = 0.0f;
        size_t RuntimeRootMotionMaxPathPoints = 128;
        // Root Motion 디버그 경로는 실행 중에만 쓰는 짧은 기록이다.
        // 씬 저장 데이터가 아니라, 에디터가 이동 궤적을 그리기 위한 임시 값이다.
        std::vector<DirectX::XMFLOAT3> RuntimeRootMotionPath;

        AnimatorComponent() = default;
        AnimatorComponent(const AnimatorComponent& other)
            : ControllerAssetGuid(other.ControllerAssetGuid),
            ControllerPath(other.ControllerPath),
            SourceAssetGuid(other.SourceAssetGuid),
            SourcePath(other.SourcePath),
            SelectedClipIndex(other.SelectedClipIndex),
            SelectedClipName(other.SelectedClipName),
            AutoPlay(other.AutoPlay),
            PreviewInEdit(other.PreviewInEdit),
            Loop(other.Loop),
            Speed(other.Speed),
            ApplyRootMotion(other.ApplyRootMotion),
            UpdateModeValue(other.UpdateModeValue),
            CullingModeValue(other.CullingModeValue),
            AvatarGuid(other.AvatarGuid),
            AvatarPath(other.AvatarPath),
            SourceAvatarGuid(other.SourceAvatarGuid),
            SourceAvatarPath(other.SourceAvatarPath),
            HumanoidRootBone(other.HumanoidRootBone),
            RetargetToHumanoid(other.RetargetToHumanoid),
            IsPlaying(false),
            ActiveStateIndex(other.ActiveStateIndex),
            EntryStateIndex(other.EntryStateIndex),
            ActiveLayerIndex(other.ActiveLayerIndex),
            States(other.States),
            Layers(other.Layers),
            Parameters(other.Parameters),
            Transitions(other.Transitions),
            SelectedTransitionIndex(other.SelectedTransitionIndex)
        {
            // Animator와 RuntimeClip은 현재 재생 위치를 들고 있는 실행 상태다.
            // 복제/Play Scene 생성 시에는 설정만 복사하고, 실제 클립은 새 씬에서 다시 로드한다.
            for (auto& layer : Layers)
            {
                layer.PreviousStateIndex = -1;
                layer.StateTime = 0.0f;
                layer.PreviousLoopTime = 0.0f;
                layer.PreviousStateTime = 0.0f;
                layer.BlendElapsed = 0.0f;
                layer.BlendDuration = 0.0f;
                layer.FiredEventIndices.clear();
                layer.PreviousRuntimeClip.reset();
                layer.Exited = false;
            }
            RuntimePlaybackLayerIndex = -1;
            RuntimeRootMotionDelta = { 0.0f, 0.0f, 0.0f };
            RuntimeRootMotionRotationDelta = { 0.0f, 0.0f, 0.0f, 1.0f };
            RuntimeRootMotionRootWorld = { 0.0f, 0.0f, 0.0f };
            RuntimeRootMotionRootMissing = false;
            RuntimeRootMotionWrappedLoop = false;
            RuntimeRootMotionLockXZ = false;
            RuntimeRootMotionLockY = false;
            RuntimeRootMotionLockRotation = false;
            RuntimeRootMotionBaked = false;
            RuntimeRootMotionRotationDegrees = 0.0f;
            RuntimeRootMotionMaxPathPoints = other.RuntimeRootMotionMaxPathPoints;
            RuntimeRootMotionPath.clear();
        }
    };

    // ==========================================================
    // 조명 (Light) 컴포넌트
    // ==========================================================
    struct LightComponent
    {
        DirectX::XMFLOAT3 LightColor = { 1.0f, 1.0f, 1.0f }; // 기본은 흰색 빛
        float Intensity = 1.0f;                              // 빛의 강도

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };
}
