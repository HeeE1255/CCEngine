#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include <map>
#include <unordered_set>
#include <vector>

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
            void SetSelectedEntities(const std::vector<Entity>& entities, Entity activeEntity = {});
            void ClearSelection();
            void UpdateSelectionVisuals(Widget* widget);
            void UpdateActiveVisuals(Widget* widget);
            Entity GetSelectedEntity() const { return m_SelectionContext; }
            uint64_t GetSelectionRevision() const { return m_SelectionRevision; }
            std::vector<Entity> GetSelectedEntities() const;
            bool IsSelected(Entity entity) const;
            Entity GetEntityAt(float mouseX, float mouseY) const;

            void Refresh();
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
            virtual void OnRender() override;

            virtual bool OnEvent(Event& e) override;
            virtual bool WantsMouseCapture() const override { return WindowPanel::WantsMouseCapture() || m_IsDraggingScrollbar; }


        private:
            // 트리 구조를 만들 때 부모 위젯을 인자로 받습니다.
            void BuildEntityTree(Entity entity, int depth, Widget* parentWidget);
            Entity FindEntityAtRecursive(Widget* widget, float mouseX, float mouseY) const;
            void SelectEntityFromClick(Entity entity);
            void CollectVisibleEntities(Widget* widget, std::vector<entt::entity>& outEntities) const;
            void SanitizeSelection();

        private:
            Scene* m_Context = nullptr;
            Entity m_SelectionContext = {};
            std::vector<entt::entity> m_SelectedEntities;
            std::unordered_set<entt::entity> m_SelectedEntitySet;
            entt::entity m_SelectionAnchor = entt::null;
            uint64_t m_SelectionRevision = 0;
            std::map<uint32_t, bool> m_ExpandedStates;
            bool m_NeedsRefresh = false;
            bool m_NeedsSelectionUpdate = false;
            ScrollState m_ScrollState;

            bool m_IsDraggingScrollbar = false;
            float m_DragMouseStartY = 0.0f;
            float m_DragScrollStartY = 0.0f;

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
