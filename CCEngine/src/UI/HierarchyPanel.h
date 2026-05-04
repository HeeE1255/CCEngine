#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include <map>

namespace CCEngine 
{
    namespace UI 
    {
        class CC_API HierarchyPanel : public WindowPanel
        {
        public:
            HierarchyPanel(const std::string& name = "HierarchyPanel");

            void SetContext(Scene* context);
            void SetSelectedEntity(Entity entity);
            void UpdateSelectionVisuals(Widget* widget);
            Entity GetSelectedEntity() const { return m_SelectionContext; }

            void Refresh();
            virtual void UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize) override;
            virtual void OnRender() override;

        private:
            // 트리 구조를 만들 때 부모 위젯을 인자로 받습니다.
            void BuildEntityTree(Entity entity, int depth, Widget* parentWidget);

        private:
            Scene* m_Context = nullptr;
            Entity m_SelectionContext = {};
            std::map<uint32_t, bool> m_ExpandedStates;
            bool m_NeedsRefresh = false;
            bool m_NeedsSelectionUpdate = false;

            // Simple deferred UI ops queue (prototype).
            // NOTE: (Deferred UI OPS 아직 미적용) -> 기능은 구현되어 있지만 기본적으로 주석 처리되어 있습니다.
            // You can enable by calling ProcessDeferredUIOps at a safe point.
            struct DeferredOp { std::function<void()> Op; };
            std::vector<DeferredOp> m_DeferredUIOps; // queued operations
            void ProcessDeferredUIOps();

			float m_startYOffset = 35.0f; // 트리 렌더링 시작 Y 오프셋
            float m_itemSpacing = 2.0f;
        };
    }
}