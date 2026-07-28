#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "entt.hpp"
#include "Core.h"
#include "Renderer/PerspectiveCamera.h"
#include "Scripting/ScriptEngine.h"

#include <box2d/id.h>

namespace CCEngine
{
    class Entity; // 전방 선언
    enum class SceneState { Edit = 0, Play = 1, Pause = 2 }; // 씬의 현재 상태를 나타내는 열거형

    class CC_API Scene
    {
    public:
        Scene();
        ~Scene();

        // 씬 복사 생성자
        static Scene* Copy(Scene* other);

        // 엔티티를 생성하는 팩토리 함수
        Entity CreateEntity(const std::string& name = "Empty Entity");
        void DestroyEntity(Entity entity); // 엔티티 파괴 함수
        Entity DuplicateEntity(Entity source); // 선택한 엔티티와 자식들을 같은 씬 안에서 복제
        ScriptComponent& AddScriptComponent(Entity entity, const std::string& className = "", bool enabled = true);
        void RemoveScriptComponent(Entity entity);

        // 매 프레임 이 씬 안의 컴포넌트들을 업데이트
        void OnUpdate(float deltaTime);

        // 3D 카메라로 씬을 렌더링하는 함수
        void OnRender2D(const PerspectiveCamera& camera);
        void OnRender3D(const PerspectiveCamera& camera);

        // 
        void OnRuntimeStart();
        void OnRuntimeStop();

        // 뷰포트 크기가 변경될 때마다 호출되는 함수
        void OnViewportResize(unsigned int width, unsigned int height);

        // 씬의 현재 상태를 반환하는 함수
        SceneState GetState() const { return m_State; }

        // 씬의 상태를 변경하는 함수
        void SetSceneState(SceneState state) { m_State = state; }

        bool IsEntityActiveSelf(Entity entity) const;
        bool IsEntityActiveInHierarchy(Entity entity) const;
        void SetEntityActiveSelf(Entity entity, bool active);

        entt::registry& GetRegistry() { return m_Registry; }

        Entity FindEntityByName(std::string_view name);

    private:
        struct PhysicsPair
        {
            entt::entity A = entt::null;
            entt::entity B = entt::null;

            bool operator==(const PhysicsPair& other) const
            {
                return A == other.A && B == other.B;
            }
        };

        struct PhysicsPairHash
        {
            std::size_t operator()(const PhysicsPair& pair) const
            {
                return (static_cast<std::size_t>(pair.A) << 32) ^ static_cast<std::size_t>(pair.B);
            }
        };

        struct QueuedPhysicsEvent
        {
            ScriptPhysicsEvent EventType = ScriptPhysicsEvent::OnCollisionEnter2D;
            entt::entity Entity = entt::null;
            entt::entity Other = entt::null;
        };

        void ResetScriptRuntimeState();
        void StartScriptRuntime();
        void StopScriptRuntime();
        void SyncScriptEnabledState();
        void InvokeScriptStartQueue();
        void InvokeScriptUpdatePass(ScriptLifecycleEvent eventType, float deltaTime);
        PhysicsPair MakePhysicsPair(entt::entity a, entt::entity b) const;
        void CollectPhysicsEvents();
        void DispatchPhysicsEventQueue();
        void DestroyRuntimeScript(entt::entity handle);
        void DestroyEntityImmediate(Entity entity);
        void QueueDestroyEntity(entt::entity handle);
        void FlushDestroyQueue();

        entt::registry m_Registry; // 모든 엔티티와 컴포넌트를 관리하는 EnTT의 관리자
        std::vector<entt::entity> m_DestroyQueue;
        std::vector<QueuedPhysicsEvent> m_PhysicsEventQueue;
        std::unordered_set<PhysicsPair, PhysicsPairHash> m_ActiveCollisionPairs;
        std::unordered_set<PhysicsPair, PhysicsPairHash> m_ActiveTriggerPairs;

        b2WorldId m_PhysicsWorldId = b2_nullWorldId; // Box2D 물리 월드의 ID
        float m_PhysicsUnitScale = 1.0f; // 렌더링 유닛 <-> 미터 비율
        float m_FixedTimeStep = 1.0f / 60.0f;
        float m_FixedAccumulator = 0.0f;

        // 씬의 현재 상태
        SceneState m_State = SceneState::Edit;

        friend class Entity; // Entity 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneSerializer; // SceneSerializer 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneHierarchyPanel; // SceneHierarchyPanel 클래스가 m_Registry에 접근할 수 있게 허락
    };
}
