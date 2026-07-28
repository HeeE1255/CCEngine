#pragma once
#include <DirectXMath.h>
#include <string>
#include <unordered_map>
#include <box2d/id.h>
#include "entt.hpp"
#include "Renderer/Mesh.h"
#include "Scene/ScriptableEntity.h"
#include "Renderer/Texture.h"
#include "Renderer/Model.h"
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
        Animator AnimPlayer;

        AnimatorComponent() = default;
        AnimatorComponent(const AnimatorComponent&) = default;
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
