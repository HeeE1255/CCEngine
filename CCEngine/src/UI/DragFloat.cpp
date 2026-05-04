#include "DragFloat.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"
#include <iomanip>
#include <sstream>

namespace CCEngine {
    namespace UI {

        DragFloat::DragFloat(const std::string& name, const std::string& label, std::function<float()> getter, std::function<void(float)> setter)
            : Widget(name), m_Label(label), m_Getter(getter), m_Setter(setter)
        {
        }

        void DragFloat::OnRender()
        {
            if (!m_IsVisible) return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            auto [mouseX, mouseY] = window.GetMousePosition();

            // ==========================================================
            // ★ 드래그 로직 (어디서든 마우스가 움직이면 값이 변함)
            // ==========================================================
            if (m_IsDragging)
            {
                float deltaX = mouseX - m_LastMouseX;
                if (deltaX != 0.0f)
                {
                    float currentValue = m_Getter();
                    m_Setter(currentValue + (deltaX * m_Sensitivity));
                    m_LastMouseX = mouseX;
                }

                // 마우스를 떼면 드래그 종료
                if (!window.IsMouseButtonPressed(0))
                {
                    m_IsDragging = false;
                }
            }

            // ==========================================================
            // 렌더링 (배경, 라벨, 현재 값)
            // ==========================================================
            bool isHovered = IsPointInside(mouseX, mouseY);
            DirectX::XMFLOAT4 bgColor = m_IsDragging ? DirectX::XMFLOAT4{ 0.3f, 0.4f, 0.5f, 1.0f } :
                isHovered ? DirectX::XMFLOAT4{ 0.2f, 0.2f, 0.2f, 1.0f } :
                DirectX::XMFLOAT4{ 0.15f, 0.15f, 0.15f, 1.0f };

            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, bgColor);

            // 소수점 2자리까지만 예쁘게 문자열로 변환
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << m_Getter();
            std::string displayStr = m_Label + ": " + ss.str();

            UIRenderer::DrawString(displayStr, m_CalculatedPos.x + 5.0f, m_CalculatedPos.y + (m_CalculatedSize.y * 0.5f) + 8.0f, { 1.0f, 1.0f, 1.0f, 1.0f });

            Widget::OnRender();
        }

        bool DragFloat::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            if (IsPointInside(e.GetX(), e.GetY()) && e.GetButton() == 0)
            {
                m_IsDragging = true;
                m_LastMouseX = e.GetX();
                e.Handled = true;
                return true;
            }
            return false;
        }

    }
}