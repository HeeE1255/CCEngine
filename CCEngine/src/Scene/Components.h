#pragma once
#include <DirectXMath.h>
#include <string>
#include <box2d/id.h>
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

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;

        b2ShapeId RuntimeShapeId = b2_nullShapeId;

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
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
        enum class MeshType { Custom = 0, Cube, Sphere, Plane };
        MeshType Type = MeshType::Cube;

        // 실제 GPU에 올라간 버퍼 데이터를 가리키는 포인터
        std::shared_ptr<Mesh> MeshData;

        DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 기본값을 흰색으로 변경 (텍스처 색을 그대로 보기 위함)
        std::shared_ptr<Texture2D> AlbedoMap;

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