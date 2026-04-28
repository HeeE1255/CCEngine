#include "Button.h"
#include "Renderer/UIRenderer.h"


namespace CCEngine {
    namespace UI {

        Button::Button(const std::string& name, const std::string& text)
            : Widget(name), m_Text(text) {
        }

        void Button::OnRender()
        {
          
            if (!m_IsVisible) return;

            // 상태에 따른 색상 결정
            UIColor currentColor = m_NormalColor;
            if (m_IsPressed) currentColor = m_ClickColor;
            else if (m_IsHovered) currentColor = m_HoverColor;

            DirectX::XMFLOAT4 dxColor = { currentColor.r, currentColor.g, currentColor.b, currentColor.a };

            // 1. 자체 UI 렌더러로 버튼 배경 그리기
            UIRenderer::DrawRectFilled(
                m_CalculatedPos.x,
                m_CalculatedPos.y,
                m_CalculatedSize.x,
                m_CalculatedSize.y,
                dxColor
            );

            // ==========================================================
            // 2. 텍스트 렌더링 
            // ==========================================================
            float textX = m_CalculatedPos.x + 10.0f;
            float textY = m_CalculatedPos.y + (m_CalculatedSize.y * 0.5f) + 8.0f; // Baseline 보정

            DirectX::XMFLOAT4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 흰색 글자
			UIRenderer::DrawString(m_Text, textX, textY, textColor);  // UIRenderer의 DrawString 호출

            Widget::OnRender();
        }

        bool Button::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            // 1. 마우스가 내 사각형 안에 있는지 묻는다.
            if (IsPointInside(e.GetX(), e.GetY()))
            {
                if (e.GetButton() == 0) // 좌클릭
                {
                    // 등록된 콜백(m_OnClick) 실행
                    if (m_OnClick) m_OnClick();

                    // ★ 내가 클릭을 먹었으니, 이벤트를 Handled 처리하고 true 반환!
                    e.Handled = true;
                    return true;
                }
            }
            return false;
        }

        bool Button::OnMouseMoved(MouseMovedEvent& e)
        {
            // 마우스가 내 위에 있으면 색상을 바꾸는 Hover 상태 처리
            bool isInside = IsPointInside(e.GetX(), e.GetY());
            if (m_IsHovered != isInside)
            {
                m_IsHovered = isInside;
                return true; // 상태가 변했으므로 이벤트를 소비함
            }
            return false;
        }

    }
}