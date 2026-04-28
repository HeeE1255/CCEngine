#pragma once
#include "Core.h"
#include "UI/Panel.h"
#include "Core/Window.h"
#include <DirectXMath.h>

namespace CCEngine
{
    namespace UI
    {
        class CC_API WindowPanel : public Panel
        {
        public:
            WindowPanel(const std::string& name = "WindowPanel", const std::string& title = "Window");

            virtual void OnRender() override;

            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
            virtual bool OnMouseMoved(MouseMovedEvent& e) override;
            virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override;
            void Redock(Widget* newParent);

            DirectX::XMFLOAT2 GetContentPosition() const
            {
                return { m_CalculatedPos.x, m_CalculatedPos.y + m_TitleBarHeight };
            }

            DirectX::XMFLOAT2 GetContentSize() const
            {
                return { m_CalculatedSize.x, m_CalculatedSize.y - m_TitleBarHeight };
            }

            void SetOwnerWindow(Window* window) { m_OwnerWindow = window; }
            Window* GetOwnerWindow() const { return m_OwnerWindow; }


        private:
            std::string m_Title;
            float m_TitleBarHeight = 24.0f; // 타이틀 바 높이를 멤버 변수로 관리

            bool m_IsDragging = false;
            bool m_IsFloating = false; // 도킹 해제(플로팅) 여부

            float m_LastMouseX = 0.0f;
			float m_LastMouseY = 0.0f;

            Window* m_OwnerWindow = nullptr;

            // 드래그할 때 마우스 포인터와 창 기준점(0,0) 사이의 거리
            float m_DragOffsetX = 0.0f;
            float m_DragOffsetY = 0.0f;
        };
    }
}