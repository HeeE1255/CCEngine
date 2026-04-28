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
            virtual void UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize) override;

            // 이벤트 처리
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

            // 상태 설정
            void SetSelected(bool selected) { m_IsSelected = selected; }
            void SetExpanded(bool expanded) { m_IsExpanded = expanded; }
            void SetHasChildren(bool has) { m_HasChildren = has; }
            bool GetExpanded() const { return m_IsExpanded; }

            // 콜백 설정
            void SetOnSelect(const std::function<void()>& callback) { m_OnSelect = callback; }
            void SetOnToggleExpand(const std::function<void()>& callback) { m_OnToggleExpand = callback; }

        private:
            std::string m_Text;
            bool m_IsSelected = false;
            bool m_IsExpanded = false;
            bool m_HasChildren = false;


            std::function<void()> m_OnSelect = nullptr;
            std::function<void()> m_OnToggleExpand = nullptr;
        };
    }
}