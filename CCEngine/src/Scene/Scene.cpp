#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer2D.h"
#include <box2d/box2d.h>

namespace CCEngine
{
    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };

        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        // entity 객체를 넣으면 내부적으로 (entt::entity) 타입으로 자동 변환됩니다.
        // 더 이상 m_EntityHandle을 private에서 억지로 꺼낼 필요가 없습니다!
        m_Registry.destroy(entity);
    }

    // ====================================================================
    // 씬 클로닝 (Play 버튼을 눌렀을 때 씬 복사본 생성)
    // ====================================================================
    Scene* Scene::Copy(Scene* other)
    {
        Scene* newScene = new Scene();

        other->m_Registry.view<TagComponent>().each([&](auto entityID, auto& tag)
            {
                // 1. 원본 엔티티를 유저님의 Entity 클래스로 예쁘게 포장합니다!
                Entity srcEntity = { entityID, other };

                // 2. 새 씬에 빈 엔티티를 생성합니다.
                Entity newEntity = newScene->CreateEntity(tag.Tag);

                // 3. 이제 srcEntity의 멋진 멤버 함수들을 마음껏 사용합니다.
                if (srcEntity.HasComponent<TransformComponent>())
                {
                    newEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();
                }
                    

                if (srcEntity.HasComponent<SpriteRendererComponent>())
                {
                    newEntity.AddComponent<SpriteRendererComponent>(srcEntity.GetComponent<SpriteRendererComponent>());
                }
                    

                // 물리 컴포넌트: 설정값만 복사 (런타임 ID는 제외)
                if (srcEntity.HasComponent<Rigidbody2DComponent>())
                {
                    auto& srcRb = srcEntity.GetComponent<Rigidbody2DComponent>();
                    auto& dstRb = newEntity.AddComponent<Rigidbody2DComponent>();
                    dstRb.Type = srcRb.Type;
                    dstRb.FixedRotation = srcRb.FixedRotation;
                }

                if (srcEntity.HasComponent<BoxCollider2DComponent>())
                {
                    auto& srcBc = srcEntity.GetComponent<BoxCollider2DComponent>();
                    auto& dstBc = newEntity.AddComponent<BoxCollider2DComponent>();
                    dstBc.Offset = srcBc.Offset;
                    dstBc.Size = srcBc.Size;
                    dstBc.Density = srcBc.Density;
                    dstBc.Friction = srcBc.Friction;
                    dstBc.Restitution = srcBc.Restitution;
                }

                // 스크립트: 함수 포인터만 복사
                if (srcEntity.HasComponent<NativeScriptComponent>())
                {
                    auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                    auto& dstNsc = newEntity.AddComponent<NativeScriptComponent>();
                    dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                    dstNsc.DestroyScript = srcNsc.DestroyScript;
                }
            });

        return newScene;
    }

    // ====================================================================
    // 런타임 시작 (Play 모드 진입)
    // ====================================================================
    void Scene::OnRuntimeStart()
    {
        m_State = SceneState::Play; // 상태 변경

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = { 0.0f, -9.8f };
        m_PhysicsWorldId = b2CreateWorld(&worldDef);

        auto view = m_Registry.view<Rigidbody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e, this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

            b2BodyDef bodyDef = b2DefaultBodyDef();

            if (rb2d.Type == Rigidbody2DComponent::BodyType::Static)
                bodyDef.type = b2_staticBody;
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic)
                bodyDef.type = b2_dynamicBody;
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Kinematic)
                bodyDef.type = b2_kinematicBody;

            bodyDef.position = { transform.Translation.x, transform.Translation.y };
            bodyDef.rotation = b2MakeRot(transform.Rotation.z);
            bodyDef.fixedRotation = rb2d.FixedRotation;

            rb2d.RuntimeBodyId = b2CreateBody(m_PhysicsWorldId, &bodyDef);

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = bc2d.Density;
                shapeDef.material.friction = bc2d.Friction;
                shapeDef.material.restitution = bc2d.Restitution;

                float hx = bc2d.Size.x * transform.Scale.x * 0.5f;
                float hy = bc2d.Size.y * transform.Scale.y * 0.5f;
                b2Polygon box = b2MakeBox(hx, hy);

                bc2d.RuntimeShapeId = b2CreatePolygonShape(rb2d.RuntimeBodyId, &shapeDef, &box);
            }
        }
    }

    // ====================================================================
    // 런타임 종료 (Edit 모드 복귀)
    // ====================================================================
    void Scene::OnRuntimeStop()
    {
        m_State = SceneState::Edit; // 상태 변경

        if (b2World_IsValid(m_PhysicsWorldId))
        {
            b2DestroyWorld(m_PhysicsWorldId);
            m_PhysicsWorldId = b2_nullWorldId;
        }

        m_Registry.view<NativeScriptComponent>().each([](auto entityID, auto& nsc)
            {
                if (nsc.Instance)
                {
                    nsc.DestroyScript(&nsc);
                }
            });
    }

    // ====================================================================
    // 매 프레임 업데이트 루프
    // ====================================================================
    void Scene::OnUpdate(float deltaTime)
    {
        // 1. [렌더링] 에디트/플레이 상관없이 항상 화면을 그립니다.
        auto renderView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        renderView.each([](auto entityID, auto& transform, auto& sprite)
            {
                DirectX::XMFLOAT2 size = { transform.Scale.x, transform.Scale.y };
                Renderer2D::DrawQuad(transform.Translation, size, sprite.Color);
            });

        // 2. [로직 & 물리] 플레이 모드일 때만 연산합니다.
        if (m_State == SceneState::Play)
        {
            // 물리 스텝 및 Transform 동기화
            if (b2World_IsValid(m_PhysicsWorldId))
            {
                b2World_Step(m_PhysicsWorldId, deltaTime, 4);

                auto rbView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
                rbView.each([](auto entityID, auto& rb2d, auto& transform)
                    {
                        b2Vec2 position = b2Body_GetPosition(rb2d.RuntimeBodyId);
                        b2Rot rotation = b2Body_GetRotation(rb2d.RuntimeBodyId);

                        transform.Translation.x = position.x;
                        transform.Translation.y = position.y;
                        transform.Rotation.z = b2Rot_GetAngle(rotation);
                    });
            }

            // 네이티브 스크립트 실행
            m_Registry.view<NativeScriptComponent>().each([=](auto entityID, auto& nsc)
                {
                    if (!nsc.Instance)
                    {
                        nsc.Instance = nsc.InstantiateScript();
                        nsc.Instance->m_Entity = Entity{ entityID, this };
                        nsc.Instance->OnCreate();
                    }
                    nsc.Instance->OnUpdate(deltaTime);
                });
        }
    }
}