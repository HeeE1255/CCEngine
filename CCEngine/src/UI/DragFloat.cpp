#include "DragFloat.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace CCEngine 
{
    namespace UI 
    {

        DragFloat::DragFloat(const std::string& name, const std::string& label, std::function<float()> getter, std::function<void(float)> setter)
            : Widget(name), m_Label(label), m_Getter(getter), m_Setter(setter) 
        {
        }

        void DragFloat::OnRender() 
        {
            if (!m_IsVisible) return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            auto [mouseX, mouseY] = window.GetMousePosition();
            bool isLeftMouseDown = window.IsMouseButtonPressed(0);

            float labelWidth = m_CalculatedSize.x * 0.35f;
            float fieldWidth = m_CalculatedSize.x - labelWidth;
            float dragZone = 20.0f; // 드래그 전용 알파벳 영역

            // 1. 드래그 로직
            if (m_IsDragging) 
            {
                if (isLeftMouseDown) 
                {
                    float deltaX = mouseX - m_LastMouseX;
                    if (deltaX != 0.0f) {
                        m_Setter(m_Getter() + deltaX * m_Sensitivity);
                        m_LastMouseX = mouseX;
                    }
                }
                else { m_IsDragging = false; }
            }

            // 2. 텍스트 입력 로직
            if (m_IsEditing) 
            {
                auto checkKey = [&](int vKey) -> bool 
                    {
                        static bool states[256] = { false };
                        bool down = (GetAsyncKeyState(vKey) & 0x8000) != 0;
                        if (down && !states[vKey]) { states[vKey] = true; return true; }
                        if (!down) states[vKey] = false;
                        return false;
                    };

                for (int i = 0; i <= 9; ++i) if (checkKey('0' + i) || checkKey(VK_NUMPAD0 + i)) m_InputBuffer += std::to_string(i);
                if (checkKey(VK_OEM_MINUS) || checkKey(VK_SUBTRACT)) m_InputBuffer += "-";
                if (checkKey(VK_OEM_PERIOD) || checkKey(VK_DECIMAL)) m_InputBuffer += ".";
                if (checkKey(VK_BACK) && !m_InputBuffer.empty()) m_InputBuffer.pop_back();

                if (checkKey(VK_RETURN) || (isLeftMouseDown && !(mouseX >= m_CalculatedPos.x + labelWidth + dragZone && mouseX <= m_CalculatedPos.x + m_CalculatedSize.x && mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + m_CalculatedSize.y))) 
                {
                    try { m_Setter(std::stof(m_InputBuffer)); }
                    catch (...) {}
                    m_IsEditing = false;
                }
                if (checkKey(VK_ESCAPE)) m_IsEditing = false;
            }

            // 3. 렌더링
            float currentX = m_CalculatedPos.x;
            UIRenderer::DrawString(m_Label, currentX, m_CalculatedPos.y + 18.0f, { 0.8f, 0.8f, 0.8f, 1.0f });
            currentX += labelWidth;

            bool isHovered = (mouseX >= currentX + dragZone && mouseX <= currentX + fieldWidth && mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + m_CalculatedSize.y);
            DirectX::XMFLOAT4 bgColor = m_IsEditing ? DirectX::XMFLOAT4{ 0.05f, 0.05f, 0.05f, 1.0f } : (isHovered ? DirectX::XMFLOAT4{ 0.25f, 0.25f, 0.25f, 1.0f } : DirectX::XMFLOAT4{ 0.15f, 0.15f, 0.15f, 1.0f });

            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y, fieldWidth, m_CalculatedSize.y, bgColor);

            // X, Y, Z가 없으므로 공용 식별자 'V' (Value) 사용
            UIRenderer::DrawString("V", currentX + 5.0f, m_CalculatedPos.y + 18.0f, { 0.6f, 0.6f, 0.6f, 1.0f });

            std::string txt = m_IsEditing ? m_InputBuffer + "|" : std::to_string(m_Getter()).substr(0, std::to_string(m_Getter()).find(".") + 3);
            UIRenderer::DrawString(txt, currentX + 22.0f, m_CalculatedPos.y + 18.0f, { 1, 1, 1, 1 });
        }

        bool DragFloat::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            float labelWidth = m_CalculatedSize.x * 0.35f;
            float dragZone = 20.0f;

            if (e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y && e.GetButton() == 0) 
            {
                float startX = m_CalculatedPos.x + labelWidth;
                if (e.GetX() >= startX && e.GetX() <= startX + dragZone) 
                {
                    m_IsDragging = true; m_IsEditing = false; m_LastMouseX = e.GetX(); return true;
                }
                else if (e.GetX() > startX + dragZone && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x) 
                {
                    m_IsEditing = true; m_InputBuffer = ""; return true;
                }
            }
            return false;
        }
    }
}