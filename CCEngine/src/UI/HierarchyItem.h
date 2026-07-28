#pragma once
#include "Core.h"
#include "UI/VBoxContainer.h"
#include <functional>

namespace CCEngine 
{
    namespace UI 
    {
        class CC_API HierarchyItem : public VBoxContainer
        {
        public:
            HierarchyItem(const std::string& name, const std::string& text);

            virtual void OnRender() override;
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;

            // 이벤트 처리
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

            // 상태 설정
            void SetSelected(bool selected) { m_IsSelected = selected; }
            void SetEntityID(uint32_t id) { m_EntityID = id; }
            uint32_t GetEntityID() const { return m_EntityID; }
            void SetExpanded(bool expanded) { m_IsExpanded = expanded; }
            void SetHasChildren(bool has) { m_HasChildren = has; }
            void SetActiveInHierarchy(bool active) { m_IsActiveInHierarchy = active; }
            bool GetExpanded() const { return m_IsExpanded; }
            void SetRenderClipRange(float top, float bottom);

            // 콜백 설정
            void SetOnSelect(const std::function<void()>& callback) { m_OnSelect = callback; }
            void SetOnToggleExpand(const std::function<void()>& callback) { m_OnToggleExpand = callback; }

        private:
            std::string m_Text;
            uint32_t m_EntityID = 0;
            bool m_IsSelected = false;
            bool m_IsExpanded = false;
            bool m_HasChildren = false;
            bool m_IsActiveInHierarchy = true;
            float m_RenderClipTop = 0.0f;
            float m_RenderClipBottom = 0.0f;


            std::function<void()> m_OnSelect = nullptr;
            std::function<void()> m_OnToggleExpand = nullptr;
        };
    }
}
