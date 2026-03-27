#pragma once
#include <string>
#include "entt.hpp"
#include "Core.h"


namespace CCEngine
{
    class Entity; // 전방 선언

    class CC_API Scene
    {
    public:
        Scene();
        ~Scene();

        // 엔티티를 생성하는 팩토리 함수
        Entity CreateEntity(const std::string& name = "Empty Entity");

        // 매 프레임 이 씬 안의 컴포넌트들을 업데이트합니다.
        void OnUpdate(float deltaTime);

    private:
        entt::registry m_Registry; // [핵심] 모든 엔티티와 컴포넌트를 관리하는 EnTT의 메모리 창고!

        friend class Entity; // Entity 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneSerializer; // SceneSerializer 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneHierarchyPanel; // SceneHierarchyPanel 클래스가 m_Registry에 접근할 수 있게 허락
    };
}