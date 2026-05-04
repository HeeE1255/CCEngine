#include "Button.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"


namespace CCEngine {
    namespace UI {

        Button::Button(const std::string& name, const std::string& text)
            : Widget(name), m_Text(text) {
        }

        void Button::OnRender()
        {
            if (!m_IsVisible) return;

            auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
            m_IsHovered = IsPointInside(mouseX, mouseY);
            if (!m_IsHovered) m_IsPressed = false;

            UIColor currentColor = m_NormalColor;

            if (m_IsActive)
            {
                // 작동 중일 때의 기본 색상
                currentColor = m_ActiveColor;
                //currentColor = { 1.0f, 0.2f, 0.2f, 1.0f };
                //currentColor = { 255, 50, 50, 255 };

                // 작동 중인데 마우스가 올라가면? (살짝 더 밝게 톤업 효과)
                // 만약 UIColor 내부 값이 float(0.0~1.0) 기반이라면 아래처럼 +0.1f 씩 더해줍니다.
                if (m_IsHovered && !m_IsPressed)
                {
                    currentColor.r = std::min(currentColor.r + 0.1f, 1.0f);
                    currentColor.g = std::min(currentColor.g + 0.1f, 1.0f);
                    currentColor.b = std::min(currentColor.b + 0.1f, 1.0f);
                }
            }
            else
            {
                // 작동 중이 아닐 때는 일반 Hover 색상 적용
                if (m_IsHovered) currentColor = m_HoverColor;
            }

            // 마우스를 꾹 누르고 있는 순간은 모든 상태를 무시하고 Click 색상 적용
            if (m_IsPressed) currentColor = m_ClickColor;

            DirectX::XMFLOAT4 dxColor = { currentColor.r, currentColor.g, currentColor.b, currentColor.a };

            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, dxColor);

            float textX = m_CalculatedPos.x + 10.0f;
            float textY = m_CalculatedPos.y + (m_CalculatedSize.y * 0.5f) + 8.0f;

            DirectX::XMFLOAT4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            UIRenderer::DrawString(m_Text, textX, textY, textColor);

            Widget::OnRender();
        }

        bool Button::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            if (IsPointInside(e.GetX(), e.GetY()) && e.GetButton() == 0) // 좌클릭
            {
                m_IsPressed = true; // 1. 누른 상태로 변경
                e.Handled = true;
                return true;
            }
            return false;
        }

        bool Button::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
        {
            if (e.GetButton() == 0)
            {
                // 눌려있던 버튼 안에서 마우스를 뗐을 때만 OnClick 발동
                if (m_IsPressed && IsPointInside(e.GetX(), e.GetY()))
                {
                    if (m_OnClick) m_OnClick();
                }
                m_IsPressed = false; // 무조건 상태 해제
            }
            return false;
        }

        // OnMouseMoved는 이제 OnRender에서 처리하므로 false만 반환
        bool Button::OnMouseMoved(MouseMovedEvent& e)
        {
            return false;
        }

    }
}