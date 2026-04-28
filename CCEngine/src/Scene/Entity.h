#pragma once
#include "entt.hpp"
#include "Core.h"
#include "Scene/Scene.h"
#include <assert.h>

namespace CCEngine
{
    class Scene; // 전방 선언

    class CC_API Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        // ==========================================
        // 컴포넌트 추가/가져오기
        // ==========================================
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            assert(!HasComponent<T>() && "Entity already has component!");
            return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent()
        {
            assert(HasComponent<T>() && "Entity does not have component!");
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template<typename T>
        bool HasComponent()
        {
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        template<typename T>
        void RemoveComponent()
        {
            assert(HasComponent<T>() && "Entity does not have component!");
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        // 엔티티가 유효한지 (씬에 존재하는지) 확인하는 연산자 오버로딩
        operator bool() const { return m_EntityHandle != entt::null; }

        // 엔티티 핸들과 씬이 모두 같으면 같은 엔티티로 간주
        bool operator==(const Entity& other) const
        {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }

        // 같지 않으면 다른 엔티티로 간주
        bool operator!=(const Entity& other) const 
        {
            return !(*this == other);
        }

        operator uint32_t() const { return (uint32_t)m_EntityHandle; } // 엔티티 핸들을 32비트 정수로 변환 (디버깅이나 ImGui 트리 노드 ID로 사용하기 편하게)
        operator entt::entity() const { return m_EntityHandle; } // EnTT의 entity 타입으로도 변환 가능하게

    private:
        entt::entity m_EntityHandle{ entt::null }; // EnTT가 발급하는 고유 ID (실제 엔티티의 정체성)
        Scene* m_Scene = nullptr;
    };
}