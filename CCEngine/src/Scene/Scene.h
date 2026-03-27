#pragma once
#include <string>
#include "entt.hpp"
#include "Core.h"

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

        // 매 프레임 이 씬 안의 컴포넌트들을 업데이트합니다.
        void OnUpdate(float deltaTime);

        // 
        void OnRuntimeStart();
        void OnRuntimeStop();

        // 뷰포트 크기가 변경될 때마다 호출되는 함수
        void OnViewportResize(unsigned int width, unsigned int height);

        // 씬의 현재 상태를 반환하는 함수
        SceneState GetState() const { return m_State; }

        // 씬의 상태를 변경하는 함수
        void SetSceneState(SceneState state) { m_State = state; }

    private:
        entt::registry m_Registry; // [핵심] 모든 엔티티와 컴포넌트를 관리하는 EnTT의 메모리 창고!

        b2WorldId m_PhysicsWorldId = b2_nullWorldId; // Box2D 물리 월드의 ID
        float m_PhysicsUnitScale = 1.0f; // 렌더링 유닛 <-> 미터 비율

        // 씬의 현재 상태
        SceneState m_State = SceneState::Edit;

        friend class Entity; // Entity 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneSerializer; // SceneSerializer 클래스가 m_Registry에 접근할 수 있게 허락
        friend class SceneHierarchyPanel; // SceneHierarchyPanel 클래스가 m_Registry에 접근할 수 있게 허락
    };
}