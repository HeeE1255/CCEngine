#pragma once
#include <DirectXMath.h>
#include <string>
#include "Scene/ScriptableEntity.h"

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
        // 나중에 여기에 Texture2D* 도 추가할 수 있습니다.

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

        // Box2D 내부 객체 포인터 (런타임 시 생성됨)
        void* RuntimeBody = nullptr;

        Rigidbody2DComponent() = default;
        Rigidbody2DComponent(const Rigidbody2DComponent&) = default;
    };

    // 2D 박스 충돌체 컴포넌트 (충돌 범위)
    struct BoxCollider2DComponent
    {
        DirectX::XMFLOAT2 Offset = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 Size = { 0.5f, 0.5f };

        // 물리적 마찰력, 탄성 등
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;

        // Box2D 내부 피스처 포인터
        void* RuntimeFixture = nullptr;

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
    };

    //NativeScriptComponent 대신 ECS를 테스트하기 위해 추가
    // OOP 방식이 아닌 ECS 방식으로 파도 움직임을 구현하는 컴포넌트
    struct WaveComponent 
    {
        float StartX = 0.0f;
        float StartY = 0.0f;
        float TimeOffset = 0.0f;

        WaveComponent() = default;
        WaveComponent(const WaveComponent&) = default;
    };
}