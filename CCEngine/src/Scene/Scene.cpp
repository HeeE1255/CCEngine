#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Scripting/ScriptEngine.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CCEngine
{
    namespace
    {
        bool EntityNameExists(Scene* scene, const std::string& name)
        {
            auto view = scene->GetRegistry().view<TagComponent>();
            for (auto handle : view)
            {
                Entity entity{ handle, scene };
                if (entity.GetComponent<TagComponent>().Tag == name)
                    return true;
            }
            return false;
        }

        std::string MakeDuplicateName(Scene* scene, const std::string& sourceName)
        {
            std::string baseName = sourceName.empty() ? "Entity" : sourceName;
            int firstSuffix = 1;

            size_t numberStart = baseName.find_last_not_of("0123456789");
            if (numberStart != std::string::npos && numberStart + 1 < baseName.size() && baseName[numberStart] == ' ')
            {
                std::string numberText = baseName.substr(numberStart + 1);
                baseName = baseName.substr(0, numberStart);
                firstSuffix = std::max(1, std::stoi(numberText) + 1);
            }

            // 이미 번호가 붙은 복제본을 다시 복제해도 "Name 1 1"이 아니라 "Name 2"처럼 이어 붙인다.
            int suffix = firstSuffix;
            std::string candidate = baseName + " " + std::to_string(suffix);

            while (EntityNameExists(scene, candidate))
            {
                ++suffix;
                candidate = baseName + " " + std::to_string(suffix);
            }

            return candidate;
        }

        void CopyEntityComponents(Entity srcEntity, Entity dstEntity)
        {
            if (srcEntity.HasComponent<ActiveComponent>())
                dstEntity.GetComponent<ActiveComponent>() = srcEntity.GetComponent<ActiveComponent>();

            if (srcEntity.HasComponent<TransformComponent>())
                dstEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();

            if (srcEntity.HasComponent<CameraComponent>())
                dstEntity.AddComponent<CameraComponent>(srcEntity.GetComponent<CameraComponent>());

            if (srcEntity.HasComponent<SpriteRendererComponent>())
                dstEntity.AddComponent<SpriteRendererComponent>(srcEntity.GetComponent<SpriteRendererComponent>());

            if (srcEntity.HasComponent<MeshComponent>())
                dstEntity.AddComponent<MeshComponent>(srcEntity.GetComponent<MeshComponent>());

            if (srcEntity.HasComponent<ModelComponent>())
                dstEntity.AddComponent<ModelComponent>(srcEntity.GetComponent<ModelComponent>());

            if (srcEntity.HasComponent<AnimatorComponent>())
                dstEntity.AddComponent<AnimatorComponent>(srcEntity.GetComponent<AnimatorComponent>());

            if (srcEntity.HasComponent<LightComponent>())
                dstEntity.AddComponent<LightComponent>(srcEntity.GetComponent<LightComponent>());

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
                dstBc.IsTrigger = srcBc.IsTrigger;
                dstBc.Density = srcBc.Density;
                dstBc.Friction = srcBc.Friction;
                dstBc.Restitution = srcBc.Restitution;
            }

            if (srcEntity.HasComponent<BoxCollider3DComponent>())
                dstEntity.AddComponent<BoxCollider3DComponent>(srcEntity.GetComponent<BoxCollider3DComponent>());

            if (srcEntity.HasComponent<SphereCollider3DComponent>())
                dstEntity.AddComponent<SphereCollider3DComponent>(srcEntity.GetComponent<SphereCollider3DComponent>());

            if (srcEntity.HasComponent<CylinderCollider3DComponent>())
                dstEntity.AddComponent<CylinderCollider3DComponent>(srcEntity.GetComponent<CylinderCollider3DComponent>());

            if (srcEntity.HasComponent<MeshCollider3DComponent>())
                dstEntity.AddComponent<MeshCollider3DComponent>(srcEntity.GetComponent<MeshCollider3DComponent>());

            if (srcEntity.HasComponent<NativeScriptComponent>())
            {
                auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>();
                dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                dstNsc.DestroyScript = srcNsc.DestroyScript;
            }

            if (srcEntity.HasComponent<ScriptComponent>())
            {
                auto script = srcEntity.GetComponent<ScriptComponent>();
                script.RuntimeInstanceCreated = false;
                script.RuntimeAwakeCalled = false;
                script.RuntimeEnabledCalled = false;
                script.RuntimeStartCalled = false;
                dstEntity.AddComponent<ScriptComponent>(script);
            }
        }

        void* EntityHandleToUserData(entt::entity handle)
        {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(handle)) + 1u);
        }

        entt::entity UserDataToEntityHandle(void* userData)
        {
            uintptr_t value = reinterpret_cast<uintptr_t>(userData);
            if (value == 0)
                return entt::null;
            return static_cast<entt::entity>(static_cast<uint32_t>(value - 1u));
        }

    }

    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };

        entity.AddComponent<ActiveComponent>();
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        entt::entity handle = (entt::entity)entity;
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (m_State == SceneState::Play)
        {
            QueueDestroyEntity(handle);
            return;
        }

        DestroyEntityImmediate(entity);
    }

    void Scene::DestroyEntityImmediate(Entity entity)
    {
        entt::entity handle = (entt::entity)entity;
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (m_State == SceneState::Play && m_Registry.all_of<ScriptComponent>(handle))
            DestroyRuntimeScript(handle);

        if (m_Registry.all_of<RelationshipComponent>(handle))
        {
            auto& relationship = m_Registry.get<RelationshipComponent>(handle);
            std::vector<entt::entity> children = relationship.Children;
            for (entt::entity child : children)
            {
                if (m_Registry.valid(child))
                    DestroyEntityImmediate(Entity{ child, this });
            }

            if (relationship.Parent != entt::null && m_Registry.valid(relationship.Parent) &&
                m_Registry.all_of<RelationshipComponent>(relationship.Parent))
            {
                auto& parentRelationship = m_Registry.get<RelationshipComponent>(relationship.Parent);
                parentRelationship.Children.erase(
                    std::remove(parentRelationship.Children.begin(), parentRelationship.Children.end(), handle),
                    parentRelationship.Children.end());
            }
        }

        m_Registry.destroy(handle);
    }

    void Scene::QueueDestroyEntity(entt::entity handle)
    {
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (std::find(m_DestroyQueue.begin(), m_DestroyQueue.end(), handle) != m_DestroyQueue.end())
            return;

        // Play 중 Destroy는 즉시 registry를 지우지 않는다.
        // 프레임 중간에 삭제하면 같은 프레임의 view 반복자가 깨질 수 있어서, 프레임 끝에서 한 번에 처리한다.
        if (m_Registry.all_of<ActiveComponent>(handle))
            m_Registry.get<ActiveComponent>(handle).ActiveSelf = false;
        m_DestroyQueue.push_back(handle);
    }

    void Scene::FlushDestroyQueue()
    {
        if (m_DestroyQueue.empty())
            return;

        std::vector<entt::entity> pending;
        pending.swap(m_DestroyQueue);

        for (entt::entity handle : pending)
        {
            if (!m_Registry.valid(handle))
                continue;

            DestroyEntityImmediate(Entity{ handle, this });
        }
    }

    bool Scene::IsEntityActiveSelf(Entity entity) const
    {
        if (!entity || entity.GetScene() != this)
            return false;

        entt::entity handle = (entt::entity)entity;
        if (!m_Registry.valid(handle))
            return false;

        if (!m_Registry.all_of<ActiveComponent>(handle))
            return true;

        return m_Registry.get<ActiveComponent>(handle).ActiveSelf;
    }

    bool Scene::IsEntityActiveInHierarchy(Entity entity) const
    {
        if (!entity || entity.GetScene() != this)
            return false;

        entt::entity current = (entt::entity)entity;
        while (current != entt::null)
        {
            if (!m_Registry.valid(current))
                return false;

            if (m_Registry.all_of<ActiveComponent>(current) &&
                !m_Registry.get<ActiveComponent>(current).ActiveSelf)
            {
                return false;
            }

            if (!m_Registry.all_of<RelationshipComponent>(current))
                break;

            current = m_Registry.get<RelationshipComponent>(current).Parent;
        }

        return true;
    }

    void Scene::SetEntityActiveSelf(Entity entity, bool active)
    {
        if (!entity || entity.GetScene() != this)
            return;

        entt::entity handle = (entt::entity)entity;
        if (!m_Registry.valid(handle))
            return;

        auto& activeComponent = m_Registry.all_of<ActiveComponent>(handle) ?
            m_Registry.get<ActiveComponent>(handle) :
            m_Registry.emplace<ActiveComponent>(handle);

        activeComponent.ActiveSelf = active;
    }

    ScriptComponent& Scene::AddScriptComponent(Entity entity, const std::string& className, bool enabled)
    {
        assert(entity && entity.GetScene() == this && "Script component target must belong to this scene.");
        assert(!entity.HasComponent<ScriptComponent>() && "Entity already has ScriptComponent.");

        auto& script = entity.AddComponent<ScriptComponent>();
        script.ClassName = className;
        script.Enabled = enabled;
        script.RuntimeInstanceCreated = false;
        script.RuntimeAwakeCalled = false;
        script.RuntimeEnabledCalled = false;
        script.RuntimeStartCalled = false;

        if (m_State == SceneState::Play && ScriptEngine::IsRunning() && IsEntityActiveInHierarchy(entity) && !script.ClassName.empty())
        {
            script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>((entt::entity)entity), script);
            if (script.RuntimeInstanceCreated)
            {
                // Play 중 컴포넌트를 붙이면 씬 시작 때 붙어 있던 스크립트와 같은 순서로 진입한다.
                // Start는 다음 Update 전 대기열에서 처리되어, 모든 Awake/OnEnable 뒤에 호출된다.
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>((entt::entity)entity), ScriptLifecycleEvent::Awake);
                script.RuntimeAwakeCalled = true;

                if (script.Enabled)
                {
                    ScriptEngine::InvokeLifecycle(static_cast<uint32_t>((entt::entity)entity), ScriptLifecycleEvent::OnEnable);
                    script.RuntimeEnabledCalled = true;
                }
            }
        }

        return script;
    }

    void Scene::RemoveScriptComponent(Entity entity)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<ScriptComponent>())
            return;

        if (m_State == SceneState::Play)
            DestroyRuntimeScript((entt::entity)entity);

        entity.RemoveComponent<ScriptComponent>();
    }

    Entity Scene::DuplicateEntity(Entity source)
    {
        if (!source || !m_Registry.valid((entt::entity)source))
            return {};

        std::vector<entt::entity> sourceHandles;
        std::function<void(Entity)> collectSubtree = [&](Entity current)
        {
            sourceHandles.push_back((entt::entity)current);

            if (!current.HasComponent<RelationshipComponent>())
                return;

            for (entt::entity childHandle : current.GetComponent<RelationshipComponent>().Children)
            {
                if (m_Registry.valid(childHandle))
                    collectSubtree(Entity{ childHandle, this });
            }
        };

        collectSubtree(source);

        std::unordered_map<entt::entity, entt::entity> entityMap;
        Entity duplicatedRoot;

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity srcEntity{ srcHandle, this };
            std::string name = srcEntity.HasComponent<TagComponent>() ? srcEntity.GetComponent<TagComponent>().Tag : "Entity";

            if (srcHandle == (entt::entity)source)
                name = MakeDuplicateName(this, name);

            Entity dstEntity = CreateEntity(name);
            entityMap[srcHandle] = (entt::entity)dstEntity;

            if (srcHandle == (entt::entity)source)
                duplicatedRoot = dstEntity;

            CopyEntityComponents(srcEntity, dstEntity);
        }

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity srcEntity{ srcHandle, this };
            Entity dstEntity{ entityMap[srcHandle], this };

            if (!srcEntity.HasComponent<RelationshipComponent>())
                continue;

            auto& srcRel = srcEntity.GetComponent<RelationshipComponent>();
            auto& dstRel = dstEntity.HasComponent<RelationshipComponent>() ? dstEntity.GetComponent<RelationshipComponent>() : dstEntity.AddComponent<RelationshipComponent>();
            dstRel.Children.clear();

            // 복제 대상 내부의 부모는 새 엔티티로 바꾸고, 루트의 원래 부모는 그대로 공유한다.
            auto parentIt = entityMap.find(srcRel.Parent);
            if (parentIt != entityMap.end())
            {
                dstRel.Parent = parentIt->second;
            }
            else
            {
                dstRel.Parent = (srcHandle == (entt::entity)source) ? srcRel.Parent : entt::null;
            }

            for (entt::entity srcChild : srcRel.Children)
            {
                auto childIt = entityMap.find(srcChild);
                if (childIt != entityMap.end())
                    dstRel.Children.push_back(childIt->second);
            }
        }

        if (source.HasComponent<RelationshipComponent>())
        {
            entt::entity originalParent = source.GetComponent<RelationshipComponent>().Parent;
            if (originalParent != entt::null && m_Registry.valid(originalParent))
            {
                Entity parent{ originalParent, this };
                auto& parentRel = parent.HasComponent<RelationshipComponent>() ? parent.GetComponent<RelationshipComponent>() : parent.AddComponent<RelationshipComponent>();
                parentRel.Children.push_back((entt::entity)duplicatedRoot);
            }
        }

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity dstEntity{ entityMap[srcHandle], this };
            if (!dstEntity.HasComponent<ModelComponent>())
                continue;

            auto& model = dstEntity.GetComponent<ModelComponent>();

            // 모델 컴포넌트는 노드 경로별 엔티티 맵을 들고 있다. 복제 후에는 새 엔티티 핸들로 다시 연결해야 한다.
            for (auto& [name, mappedEntity] : model.NodeEntityMap)
            {
                auto it = entityMap.find(mappedEntity);
                if (it != entityMap.end())
                    mappedEntity = it->second;
            }

            for (auto& [path, mappedEntity] : model.NodePathEntityMap)
            {
                auto it = entityMap.find(mappedEntity);
                if (it != entityMap.end())
                    mappedEntity = it->second;
            }
        }

        return duplicatedRoot;
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

                if (srcEntity.HasComponent<ActiveComponent>())
                {
                    dstEntity.GetComponent<ActiveComponent>() = srcEntity.GetComponent<ActiveComponent>();
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
                    dstBc.IsTrigger = srcBc.IsTrigger;
                    dstBc.Density = srcBc.Density;
                    dstBc.Friction = srcBc.Friction;
                    dstBc.Restitution = srcBc.Restitution;
                }

                if (srcEntity.HasComponent<BoxCollider3DComponent>())
                    dstEntity.AddComponent<BoxCollider3DComponent>(srcEntity.GetComponent<BoxCollider3DComponent>());

                if (srcEntity.HasComponent<SphereCollider3DComponent>())
                    dstEntity.AddComponent<SphereCollider3DComponent>(srcEntity.GetComponent<SphereCollider3DComponent>());

                if (srcEntity.HasComponent<CylinderCollider3DComponent>())
                    dstEntity.AddComponent<CylinderCollider3DComponent>(srcEntity.GetComponent<CylinderCollider3DComponent>());

                if (srcEntity.HasComponent<MeshCollider3DComponent>())
                    dstEntity.AddComponent<MeshCollider3DComponent>(srcEntity.GetComponent<MeshCollider3DComponent>());

                // 스크립트 복사
                if (srcEntity.HasComponent<NativeScriptComponent>())
                {
                    auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                    auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>();
                    dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                    dstNsc.DestroyScript = srcNsc.DestroyScript;
                }

                if (srcEntity.HasComponent<ScriptComponent>())
                {
                    auto script = srcEntity.GetComponent<ScriptComponent>();
                    script.RuntimeInstanceCreated = false;
                    script.RuntimeAwakeCalled = false;
                    script.RuntimeEnabledCalled = false;
                    script.RuntimeStartCalled = false;
                    dstEntity.AddComponent<ScriptComponent>(script);
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

        newScene->GetRegistry().view<ModelComponent>().each([&](auto dstHandle, auto& modelComp)
            {
                for (auto& [name, mappedEntity] : modelComp.NodeEntityMap)
                {
                    auto it = enttMap.find(mappedEntity);
                    mappedEntity = it != enttMap.end() ? it->second : entt::null;
                }

                for (auto& [path, mappedEntity] : modelComp.NodePathEntityMap)
                {
                    auto it = enttMap.find(mappedEntity);
                    mappedEntity = it != enttMap.end() ? it->second : entt::null;
                }
            });

        return newScene;
    }

    void Scene::ResetScriptRuntimeState()
    {
        m_Registry.view<ScriptComponent>().each([](auto, auto& script)
            {
                script.RuntimeInstanceCreated = false;
                script.RuntimeAwakeCalled = false;
                script.RuntimeEnabledCalled = false;
                script.RuntimeStartCalled = false;
            });
    }

    void Scene::StartScriptRuntime()
    {
        ResetScriptRuntimeState();

        if (!ScriptEngine::Start(this))
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity))
                continue;

            if (script.ClassName.empty())
                continue;

            script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>(entityID), script);
            if (script.RuntimeInstanceCreated)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Awake);
                script.RuntimeAwakeCalled = true;
            }
        }

        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity) || !script.Enabled || !script.RuntimeInstanceCreated)
                continue;

            ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnEnable);
            script.RuntimeEnabledCalled = true;
        }

        InvokeScriptStartQueue();
    }

    void Scene::StopScriptRuntime()
    {
        if (ScriptEngine::IsRunning())
        {
            auto scriptView = m_Registry.view<ScriptComponent>();
            for (auto entityID : scriptView)
            {
                DestroyRuntimeScript(entityID);
            }
        }

        ScriptEngine::Stop();
        ResetScriptRuntimeState();
        m_DestroyQueue.clear();
    }

    void Scene::SyncScriptEnabledState()
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            const bool activeInHierarchy = IsEntityActiveInHierarchy(entity);

            if (activeInHierarchy && !script.RuntimeInstanceCreated && !script.ClassName.empty())
            {
                script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>(entityID), script);
                if (script.RuntimeInstanceCreated)
                {
                    ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Awake);
                    script.RuntimeAwakeCalled = true;
                }
            }

            if (!script.RuntimeInstanceCreated)
                continue;

            // Enabled는 인스펙터에서 즉시 바뀔 수 있다.
            // 런타임 플래그와 비교해서 변화가 있을 때만 OnEnable/OnDisable을 보낸다.
            if (activeInHierarchy && script.Enabled && !script.RuntimeEnabledCalled)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnEnable);
                script.RuntimeEnabledCalled = true;
            }
            else if ((!activeInHierarchy || !script.Enabled) && script.RuntimeEnabledCalled)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnDisable);
                script.RuntimeEnabledCalled = false;
            }
        }
    }

    void Scene::InvokeScriptStartQueue()
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity) || !script.Enabled || !script.RuntimeInstanceCreated || !script.RuntimeEnabledCalled || script.RuntimeStartCalled)
                continue;

            ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Start);
            script.RuntimeStartCalled = true;
        }
    }

    void Scene::InvokeScriptUpdatePass(ScriptLifecycleEvent eventType, float deltaTime)
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (IsEntityActiveInHierarchy(entity) && script.Enabled && script.RuntimeInstanceCreated && script.RuntimeEnabledCalled && script.RuntimeStartCalled)
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), eventType, deltaTime);
        }
    }

    Scene::PhysicsPair Scene::MakePhysicsPair(entt::entity a, entt::entity b) const
    {
        if (static_cast<uint32_t>(a) > static_cast<uint32_t>(b))
            std::swap(a, b);
        return { a, b };
    }

    void Scene::CollectPhysicsEvents()
    {
        if (!b2World_IsValid(m_PhysicsWorldId))
            return;

        std::unordered_set<PhysicsPair, PhysicsPairHash> beganCollisions;
        std::unordered_set<PhysicsPair, PhysicsPairHash> beganTriggers;

        auto eraseInvalidPairs = [this](std::unordered_set<PhysicsPair, PhysicsPairHash>& pairs)
            {
                for (auto it = pairs.begin(); it != pairs.end();)
                {
                    if (it->A == entt::null || it->B == entt::null || !m_Registry.valid(it->A) || !m_Registry.valid(it->B))
                        it = pairs.erase(it);
                    else
                        ++it;
                }
            };

        eraseInvalidPairs(m_ActiveCollisionPairs);
        eraseInvalidPairs(m_ActiveTriggerPairs);

        auto resolveShapeEntity = [this](b2ShapeId shapeId) -> entt::entity
            {
                if (!b2Shape_IsValid(shapeId))
                    return entt::null;

                entt::entity handle = UserDataToEntityHandle(b2Shape_GetUserData(shapeId));
                if (handle == entt::null || !m_Registry.valid(handle))
                    return entt::null;

                return handle;
            };

        auto queueTwoWayEvent = [this](ScriptPhysicsEvent eventType, entt::entity a, entt::entity b)
            {
                if (a == entt::null || b == entt::null || a == b)
                    return;

                // 물리 월드가 이벤트를 만든 직후 바로 C#을 호출하지 않고 큐에 복사한다.
                // 스크립트 안에서 Destroy 같은 구조 변경이 일어나도 Box2D 이벤트 배열을 건드리지 않게 하기 위해서다.
                m_PhysicsEventQueue.push_back({ eventType, a, b });
                m_PhysicsEventQueue.push_back({ eventType, b, a });
            };

        b2ContactEvents contactEvents = b2World_GetContactEvents(m_PhysicsWorldId);
        for (int i = 0; i < contactEvents.beginCount; ++i)
        {
            const b2ContactBeginTouchEvent& event = contactEvents.beginEvents[i];
            entt::entity a = resolveShapeEntity(event.shapeIdA);
            entt::entity b = resolveShapeEntity(event.shapeIdB);
            PhysicsPair pair = MakePhysicsPair(a, b);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveCollisionPairs.insert(pair);
            beganCollisions.insert(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionEnter2D, pair.A, pair.B);
        }

        for (int i = 0; i < contactEvents.endCount; ++i)
        {
            const b2ContactEndTouchEvent& event = contactEvents.endEvents[i];
            entt::entity a = resolveShapeEntity(event.shapeIdA);
            entt::entity b = resolveShapeEntity(event.shapeIdB);
            PhysicsPair pair = MakePhysicsPair(a, b);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveCollisionPairs.erase(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionExit2D, pair.A, pair.B);
        }

        b2SensorEvents sensorEvents = b2World_GetSensorEvents(m_PhysicsWorldId);
        for (int i = 0; i < sensorEvents.beginCount; ++i)
        {
            const b2SensorBeginTouchEvent& event = sensorEvents.beginEvents[i];
            entt::entity sensor = resolveShapeEntity(event.sensorShapeId);
            entt::entity visitor = resolveShapeEntity(event.visitorShapeId);
            PhysicsPair pair = MakePhysicsPair(sensor, visitor);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveTriggerPairs.insert(pair);
            beganTriggers.insert(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerEnter2D, pair.A, pair.B);
        }

        for (int i = 0; i < sensorEvents.endCount; ++i)
        {
            const b2SensorEndTouchEvent& event = sensorEvents.endEvents[i];
            entt::entity sensor = resolveShapeEntity(event.sensorShapeId);
            entt::entity visitor = resolveShapeEntity(event.visitorShapeId);
            PhysicsPair pair = MakePhysicsPair(sensor, visitor);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveTriggerPairs.erase(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerExit2D, pair.A, pair.B);
        }

        for (const PhysicsPair& pair : m_ActiveCollisionPairs)
        {
            if (!beganCollisions.contains(pair))
                queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionStay2D, pair.A, pair.B);
        }

        for (const PhysicsPair& pair : m_ActiveTriggerPairs)
        {
            if (!beganTriggers.contains(pair))
                queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerStay2D, pair.A, pair.B);
        }
    }

    void Scene::DispatchPhysicsEventQueue()
    {
        if (!ScriptEngine::IsRunning() || m_PhysicsEventQueue.empty())
            return;

        std::vector<QueuedPhysicsEvent> events;
        events.swap(m_PhysicsEventQueue);

        for (const QueuedPhysicsEvent& event : events)
        {
            if (event.Entity == entt::null || event.Other == entt::null || !m_Registry.valid(event.Entity) || !m_Registry.valid(event.Other))
                continue;

            Entity entity{ event.Entity, this };
            if (!IsEntityActiveInHierarchy(entity) || !m_Registry.all_of<ScriptComponent>(event.Entity))
                continue;

            const auto& script = m_Registry.get<ScriptComponent>(event.Entity);
            if (!script.Enabled || !script.RuntimeInstanceCreated || !script.RuntimeEnabledCalled || !script.RuntimeStartCalled)
                continue;

            ScriptEngine::InvokePhysicsEvent(static_cast<uint32_t>(event.Entity), event.EventType, static_cast<uint32_t>(event.Other));
        }
    }

    void Scene::DestroyRuntimeScript(entt::entity handle)
    {
        if (!ScriptEngine::IsRunning() || !m_Registry.valid(handle) || !m_Registry.all_of<ScriptComponent>(handle))
            return;

        auto& script = m_Registry.get<ScriptComponent>(handle);
        const uint32_t entityID = static_cast<uint32_t>(handle);
        if (script.RuntimeInstanceCreated && script.RuntimeEnabledCalled)
        {
            ScriptEngine::InvokeLifecycle(entityID, ScriptLifecycleEvent::OnDisable);
            script.RuntimeEnabledCalled = false;
        }

        if (script.RuntimeInstanceCreated)
        {
            ScriptEngine::InvokeLifecycle(entityID, ScriptLifecycleEvent::OnDestroy);
            ScriptEngine::DestroyInstance(entityID);
        }

        script.RuntimeInstanceCreated = false;
        script.RuntimeAwakeCalled = false;
        script.RuntimeEnabledCalled = false;
        script.RuntimeStartCalled = false;
    }

    // ====================================================================
    // Play 모드 시작
    // ====================================================================
    void Scene::OnRuntimeStart()
    {
        m_State = SceneState::Play;
        m_FixedAccumulator = 0.0f;
        m_PhysicsEventQueue.clear();
        m_ActiveCollisionPairs.clear();
        m_ActiveTriggerPairs.clear();

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = { 0.0f, -9.8f };
        m_PhysicsWorldId = b2CreateWorld(&worldDef);

        auto view = m_Registry.view<Rigidbody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e, this };
            if (!IsEntityActiveInHierarchy(entity))
                continue;

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
            bodyDef.userData = EntityHandleToUserData(e);

            rb2d.RuntimeBodyId = b2CreateBody(m_PhysicsWorldId, &bodyDef);

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.userData = EntityHandleToUserData(e);
                shapeDef.isSensor = bc2d.IsTrigger;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableContactEvents = !bc2d.IsTrigger;
                shapeDef.density = bc2d.Density;
                shapeDef.material.friction = bc2d.Friction;
                shapeDef.material.restitution = bc2d.Restitution;

                float hx = bc2d.Size.x * transform.Scale.x * 0.5f;
                float hy = bc2d.Size.y * transform.Scale.y * 0.5f;
                b2Polygon box = b2MakeBox(hx, hy);

                bc2d.RuntimeShapeId = b2CreatePolygonShape(rb2d.RuntimeBodyId, &shapeDef, &box);
            }
        }

        StartScriptRuntime();
    }

    // ====================================================================
    // Edit 모드 복귀
    // ====================================================================
    void Scene::OnRuntimeStop()
    {
        // 관리 객체를 먼저 정리해야 OnDisable/OnDestroy에서 아직 살아 있는 엔티티 컴포넌트에 접근할 수 있다.
        FlushDestroyQueue();
        StopScriptRuntime();

        m_State = SceneState::Edit;
        m_FixedAccumulator = 0.0f;
        m_PhysicsEventQueue.clear();
        m_ActiveCollisionPairs.clear();
        m_ActiveTriggerPairs.clear();

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
        // 1. 물리 & 로직 (오직 Play 모드에서만!)
        if (m_State == SceneState::Play)
        {
            SyncScriptEnabledState();
            InvokeScriptStartQueue();

            m_FixedAccumulator += deltaTime;
            while (m_FixedAccumulator >= m_FixedTimeStep)
            {
                InvokeScriptUpdatePass(ScriptLifecycleEvent::FixedUpdate, m_FixedTimeStep);
                m_FixedAccumulator -= m_FixedTimeStep;
            }

            if (b2World_IsValid(m_PhysicsWorldId))
            {
                b2World_Step(m_PhysicsWorldId, deltaTime, 4);

                auto rbView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
                rbView.each([&](auto entityID, auto& rb2d, auto& transform)
                    {
                        if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                            return;

                        b2Vec2 position = b2Body_GetPosition(rb2d.RuntimeBodyId);
                        b2Rot rotation = b2Body_GetRotation(rb2d.RuntimeBodyId);

                        transform.Translation.x = position.x;
                        transform.Translation.y = position.y;
                        transform.Rotation.z = b2Rot_GetAngle(rotation);
                    });

                CollectPhysicsEvents();
                DispatchPhysicsEventQueue();
            }

            m_Registry.view<NativeScriptComponent>().each([=](auto entityID, auto& nsc)
                {
                    if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                        return;

                    if (!nsc.Instance)
                    {
                        nsc.Instance = nsc.InstantiateScript();
                        nsc.Instance->m_Entity = Entity{ entityID, this };
                        nsc.Instance->OnCreate();
                    }
                    nsc.Instance->OnUpdate(deltaTime);
                });

            InvokeScriptUpdatePass(ScriptLifecycleEvent::Update, deltaTime);
            InvokeScriptUpdatePass(ScriptLifecycleEvent::LateUpdate, deltaTime);
            FlushDestroyQueue();
        } 

        // =========================================================
        // 2. 애니메이터 재생 업데이트 
        // =========================================================
        auto animView = m_Registry.view<AnimatorComponent>();
        animView.each([&](auto entityID, auto& animComp)
            {
                Entity entity{ entityID, this };
                if (!IsEntityActiveInHierarchy(entity))
                    return;

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
                    auto& modelComponent = current.GetComponent<ModelComponent>();
                    auto& model = modelComponent.TargetModel;

                    const auto& nodeMap = modelComponent.NodePathEntityMap.empty() ? modelComponent.NodeEntityMap : modelComponent.NodePathEntityMap;
                    animComp.AnimPlayer.Update(deltaTime, model.get(), this, &nodeMap);
                }
            });
    }

    // ====================================================================
    // 2D 렌더링
    // ====================================================================
    void Scene::OnRender2D(const PerspectiveCamera& camera)
    {
        Renderer2D::BeginScene(camera);

        auto renderView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        renderView.each([&](auto entityID, auto& transform, auto& sprite)
            {
                if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                    return;

                DirectX::XMFLOAT2 size = { transform.Scale.x, transform.Scale.y };
                Renderer2D::DrawQuad(transform.Translation, size, sprite.Color, (int)entityID);
            });

        Renderer2D::EndScene();
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
                if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                    return;

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
                if (!IsEntityActiveInHierarchy(entity))
                    return;

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

                DirectX::XMFLOAT4 renderColor = mesh.BaseColor;
                std::shared_ptr<Texture2D> renderTexture = mesh.AlbedoMap;
                if (mesh.Material)
                {
                    // Material이 연결된 메시만 재질 값을 우선한다.
                    // 기존 씬은 BaseColor/AlbedoMap을 그대로 쓰기 때문에 구버전 데이터가 깨지지 않는다.
                    renderColor = mesh.Material->AlbedoColor;
                    if (mesh.Material->AlbedoTexture)
                        renderTexture = mesh.Material->AlbedoTexture;
                }

                if (animatorComp)
                {
                    DirectX::XMMATRIX rootWorldTransform = getTransform(rootEntity);
                    auto& animator = animatorComp->AnimPlayer;

                    Renderer3D::DrawSkinnedMesh(
                        rootWorldTransform,
                        mesh.MeshData,
                        renderTexture,
                        renderColor,
                        mesh.Material.get(),
                        (int)entityID,
                        animator.GetFinalBoneMatrices()
                    );
                }
                else
                {
                    DirectX::XMMATRIX worldTransform = getTransform(entity);
                    Renderer3D::DrawMesh(worldTransform, mesh.MeshData, renderTexture, renderColor, mesh.Material.get(), (int)entityID);
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
