#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
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
        m_Registry.destroy(entity);
    }

    // ====================================================================
    // Play 버튼 씬 복사
    // ====================================================================
    Scene* Scene::Copy(Scene* srcScene)
    {
        Scene* newScene = new Scene();

        std::unordered_map<entt::entity, entt::entity> enttMap;

        srcScene->m_Registry.view<TagComponent>().each([&](auto srcHandle, auto& tagComp)
            {
                Entity srcEntity = { srcHandle, srcScene };

                // 1. 새 씬에 엔티티 생성 (Tag 이름 그대로 사용)
                Entity dstEntity = newScene->CreateEntity(tagComp.Tag);

                // 2. 맵핑 테이블에 기록 (구 ID -> 신 ID)
                enttMap[srcHandle] = (entt::entity)dstEntity;

                // 3. RelationshipComponent 복사 (옛날 ID 그대로 복사됨)
                if (srcEntity.HasComponent<RelationshipComponent>())
                {
                    auto& srcRel = srcEntity.GetComponent<RelationshipComponent>();
                    auto& dstRel = dstEntity.HasComponent<RelationshipComponent>() ?
                        dstEntity.GetComponent<RelationshipComponent>() :
                        dstEntity.AddComponent<RelationshipComponent>();

                    dstRel.Parent = srcRel.Parent;
                    dstRel.Children = srcRel.Children;
                }

                if (srcEntity.HasComponent<TransformComponent>())
                {
                    dstEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();
                }

                if (srcEntity.HasComponent<CameraComponent>())
                {
                    dstEntity.AddComponent<CameraComponent>(srcEntity.GetComponent<CameraComponent>());
                }

                if (srcEntity.HasComponent<SpriteRendererComponent>())
                {
                    dstEntity.AddComponent<SpriteRendererComponent>(srcEntity.GetComponent<SpriteRendererComponent>());
                }

                if (srcEntity.HasComponent<MeshComponent>())
                {
                    dstEntity.AddComponent<MeshComponent>(srcEntity.GetComponent<MeshComponent>());
                }

                if (srcEntity.HasComponent<ModelComponent>())
                {
                    dstEntity.AddComponent<ModelComponent>(srcEntity.GetComponent<ModelComponent>());
                }

                if (srcEntity.HasComponent<AnimatorComponent>())
                {
                    dstEntity.AddComponent<AnimatorComponent>(srcEntity.GetComponent<AnimatorComponent>());
                }

                if (srcEntity.HasComponent<LightComponent>())
                {
                    dstEntity.AddComponent<LightComponent>(srcEntity.GetComponent<LightComponent>());
                }

                // 물리 설정 복사
                if (srcEntity.HasComponent<Rigidbody2DComponent>())
                {
                    auto& srcRb = srcEntity.GetComponent<Rigidbody2DComponent>();
                    auto& dstRb = dstEntity.AddComponent<Rigidbody2DComponent>();
                    dstRb.Type = srcRb.Type;
                    dstRb.FixedRotation = srcRb.FixedRotation;
                }

                if (srcEntity.HasComponent<BoxCollider2DComponent>())
                {
                    auto& srcBc = srcEntity.GetComponent<BoxCollider2DComponent>();
                    auto& dstBc = dstEntity.AddComponent<BoxCollider2DComponent>();
                    dstBc.Offset = srcBc.Offset;
                    dstBc.Size = srcBc.Size;
                    dstBc.Density = srcBc.Density;
                    dstBc.Friction = srcBc.Friction;
                    dstBc.Restitution = srcBc.Restitution;
                }

                // 스크립트 복사
                if (srcEntity.HasComponent<NativeScriptComponent>())
                {
                    auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                    auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>();
                    dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                    dstNsc.DestroyScript = srcNsc.DestroyScript;
                }
            });

        newScene->GetRegistry().view<RelationshipComponent>().each([&](auto dstHandle, auto& rel)
            {
                // 1. 부모 ID 갱신
                if (rel.Parent != entt::null)
                {
                    if (enttMap.find(rel.Parent) != enttMap.end())
                        rel.Parent = enttMap[rel.Parent]; // 새 ID로 교체
                    else
                        rel.Parent = entt::null; // 맵핑 실패 시 고아 처리
                }

                // 2. 자식들 ID 갱신
                for (size_t i = 0; i < rel.Children.size(); ++i)
                {
                    if (enttMap.find(rel.Children[i]) != enttMap.end())
                        rel.Children[i] = enttMap[rel.Children[i]]; // 새 ID로 교체
                }
            });

        return newScene;
    }

    // ====================================================================
    // Play 모드 시작
    // ====================================================================
    void Scene::OnRuntimeStart()
    {
        m_State = SceneState::Play;

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
            {
                bodyDef.type = b2_staticBody;
            }
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic)
            {
                bodyDef.type = b2_dynamicBody;
            }
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Kinematic)
            {
                bodyDef.type = b2_kinematicBody;
            }

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
    // Edit 모드 복귀
    // ====================================================================
    void Scene::OnRuntimeStop()
    {
        m_State = SceneState::Edit;

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
    // 매 프레임 업데이트
    // ====================================================================
    void Scene::OnUpdate(float deltaTime)
    {
        // 1. 2D 렌더링
        auto renderView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        renderView.each([](auto entityID, auto& transform, auto& sprite)
            {
                DirectX::XMFLOAT2 size = { transform.Scale.x, transform.Scale.y };
                Renderer2D::DrawQuad(transform.Translation, size, sprite.Color, (int)entityID);
            });

        // 2. 물리 & 로직 (오직 Play 모드에서만!)
        if (m_State == SceneState::Play)
        {
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

        // =========================================================
        // 3. 애니메이터 재생 업데이트 
        // =========================================================
        auto animView = m_Registry.view<AnimatorComponent>();
        animView.each([&](auto entityID, auto& animComp)
            {
                Entity entity{ entityID, this };

                Entity current = entity;
                while (current.HasComponent<RelationshipComponent>() && !current.HasComponent<ModelComponent>())
                {
                    entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        current = { parentID, this };
                    }
                    else
                    {
                        break;
                    }
                }

                if (current.HasComponent<ModelComponent>())
                {
                    auto& model = current.GetComponent<ModelComponent>().TargetModel;

                    // [핵심] 어제 만들었던 '씬 전체를 뒤지는 로직'을 위해 this를 꼭 넘겨준다!
                    animComp.AnimPlayer.Update(deltaTime, model.get(), this);
                }
            });
    }

    // ====================================================================
    // 3D 렌더링
    // ====================================================================
    void Scene::OnRender3D(const PerspectiveCamera& camera)
    {
        SceneLightData sceneLight;
        sceneLight.LightCount = 0; // 초기화

        auto lightView = m_Registry.view<TransformComponent, LightComponent>();

        lightView.each([&](auto entityID, auto& tc, auto& lc)
            {
                // 이미 조명을 4개(배열 꽉 참) 찾았다면, 더 이상 계산하지 않고 스킵
                if (sceneLight.LightCount >= 4)
                {
                    return;
                }

                auto q = DirectX::XMLoadFloat4(&tc.QuaternionRotation);
                DirectX::XMVECTOR forward = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                DirectX::XMVECTOR rotatedForward = DirectX::XMVector3Rotate(forward, q);

                DirectX::XMStoreFloat3(&sceneLight.Lights[sceneLight.LightCount].Direction, rotatedForward);
                sceneLight.Lights[sceneLight.LightCount].Color = lc.LightColor;
                sceneLight.Lights[sceneLight.LightCount].Intensity = lc.Intensity;

                sceneLight.LightCount++; // 저장했으니 카운트 1 증가
            });

        Renderer3D::BeginScene(camera, sceneLight);

        std::function<DirectX::XMMATRIX(Entity)> getTransform = [&](Entity e) -> DirectX::XMMATRIX
            {
                auto& tc = e.GetComponent<TransformComponent>();
                auto q = DirectX::XMLoadFloat4(&tc.QuaternionRotation);
                DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(tc.Scale.x, tc.Scale.y, tc.Scale.z) *
                    DirectX::XMMatrixRotationQuaternion(q) *
                    DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);

                if (e.HasComponent<RelationshipComponent>())
                {
                    entt::entity parentID = e.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        Entity parent{ parentID, this };
                        DirectX::XMMATRIX parentWorld;

                        // [핵심 로직 추가] 부모가 애니메이터를 가지고 있는지 확인
                        if (parent.HasComponent<AnimatorComponent>())
                        {
                            auto& anim = parent.GetComponent<AnimatorComponent>().AnimPlayer;
                            auto& tag = e.GetComponent<TagComponent>().Tag;

                            // 부모의 모델 데이터가 필요함 (이름으로 인덱스를 찾기 위해)
                            // 현재 구조상 부모가 ModelComponent도 같이 가지고 있다고 가정
                            if (parent.HasComponent<ModelComponent>())
                            {
                                auto model = parent.GetComponent<ModelComponent>().TargetModel.get();
                                int boneIdx = anim.GetBoneIndex(tag, model);

                                if (boneIdx != -1)
                                {
                                    // 부모의 단순 Transform이 아니라 애니메이션이 적용된 '뼈 행렬'을 부모 행렬로 사용!
                                    parentWorld = anim.GetFinalMatrix(boneIdx);
                                }
                                else
                                {
                                    parentWorld = getTransform(parent);
                                }
                            }
                            else
                            {
                                parentWorld = getTransform(parent);
                            }
                        }
                        else
                        {
                            parentWorld = getTransform(parent);
                        }

                        transform = transform * parentWorld;
                    }
                }
                return transform;
            };

        std::function<AnimatorComponent* (Entity)> findAnimator = [&](Entity e) -> AnimatorComponent*
            {
                if (e.HasComponent<AnimatorComponent>())
                {
                    return &e.GetComponent<AnimatorComponent>();
                }

                if (e.HasComponent<RelationshipComponent>())
                {
                    entt::entity parentID = e.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        return findAnimator({ parentID, this });
                    }
                }
                return nullptr;
            };

        auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
        meshView.each([&](auto entityID, auto& tc, auto& mesh)
            {
                Entity entity{ entityID, this };

                // 애니메이터와 루트 엔티티를 찾기 위한 변수
                Entity current = entity;
                AnimatorComponent* animatorComp = nullptr;
                Entity rootEntity = entity; // 루트를 기억할 변수

                // 부모를 타고 올라가며 애니메이터 찾기
                while (true)
                {
                    if (current.HasComponent<AnimatorComponent>())
                    {
                        animatorComp = &current.GetComponent<AnimatorComponent>();
                        rootEntity = current; // 애니메이터를 가진 놈이 바로 진짜 루트!
                        break;
                    }

                    if (current.HasComponent<RelationshipComponent>() && current.GetComponent<RelationshipComponent>().Parent != entt::null)
                    {
                        current = { current.GetComponent<RelationshipComponent>().Parent, this };
                    }
                    else
                    {
                        break;
                    }
                }

                if (animatorComp)
                {
                    DirectX::XMMATRIX rootWorldTransform = getTransform(entity);
                    auto& animator = animatorComp->AnimPlayer;

                    Renderer3D::DrawSkinnedMesh(
                        rootWorldTransform, // <--- 내 트랜스폼이 아니라 루트 트랜스폼을 넘김!
                        mesh.MeshData,
                        mesh.AlbedoMap,
                        mesh.BaseColor,
                        (int)entityID,
                        animator.GetFinalBoneMatrices()
                    );
                }
                else
                {
                    DirectX::XMMATRIX worldTransform = getTransform(entity);
                    Renderer3D::DrawMesh(worldTransform, mesh.MeshData, mesh.AlbedoMap, mesh.BaseColor, (int)entityID);
                }
            });

        Renderer3D::EndScene();
    }

    // ====================================================================
    // 엔티티 이름으로 찾기 (에디터 본 조작 연동용)
    // ====================================================================
    Entity Scene::FindEntityByName(std::string_view name)
    {
        entt::entity found = entt::null;

        m_Registry.view<TagComponent>().each([&](auto entity, auto& tag)
            {
                if (found != entt::null) return; // 이미 찾았으면 스킵
                if (tag.Tag == name)
                {
                    found = entity;
                }
            });

        return found != entt::null ? Entity{ found, this } : Entity{};
    }
}