#pragma once
#include "Core.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"

namespace CCEngine
{
    class CC_API SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(Scene* context);

        // 어떤 씬을 보여줄지 연결합니다.
        void SetContext(Scene* context);

        // 매 프레임 ImGui 창을 그리는 함수
        void OnImGuiRender();

        // 밖에서 이 함수를 부르면 리셋 스위치가 켜집니다!
        void ResetLayout() { m_ResetLayoutFlag = true; }

    private:
        void DrawEntityNode(Entity entity); // 씬 안의 엔티티 하나를 ImGui 트리 노드로 그리는 함수
        void DrawComponents(Entity entity); // 엔티티의 컴포넌트들을 ImGui로 그리는 함수 (선택된 엔티티의 세부 정보 패널에서 사용)

    private:
        Scene* m_Context = nullptr;      // 현재 띄워진 씬
        Entity m_SelectionContext;       // 현재 마우스로 '클릭(선택)'한 엔티티
        bool m_ResetLayoutFlag = false; // 리셋 스위치
    };
}