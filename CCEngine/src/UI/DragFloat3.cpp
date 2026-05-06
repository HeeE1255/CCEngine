#include "DragFloat3.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"
#include <iomanip>
#include <sstream>

#define NOMINMAX
#include <windows.h> 

namespace CCEngine {
    namespace UI {

        DragFloat3::DragFloat3(const std::string& name, const std::string& label, std::function<DirectX::XMFLOAT3()> getter, std::function<void(DirectX::XMFLOAT3)> setter)
            : Widget(name), m_Label(label), m_Getter(getter), m_Setter(setter)
        {
        }

        void DragFloat3::OnRender()
        {
            if (!m_IsVisible) return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            auto [mouseX, mouseY] = window.GetMousePosition();
            bool isLeftMouseDown = window.IsMouseButtonPressed(0);

            float labelWidth = m_CalculatedSize.x * 0.35f;
            float fieldWidth = (m_CalculatedSize.x - labelWidth) / 3.0f;
            float dragZoneWidth = 15.0f;
            float spacing = 4.0f;
            float innerBoxWidth = fieldWidth - spacing;

            // 1. 드래그 로직
            if (m_DraggingAxis != -1)
            {
                if (isLeftMouseDown) {
                    float deltaX = mouseX - m_LastMouseX;
                    if (deltaX != 0.0f) {
                        DirectX::XMFLOAT3 val = m_Getter();
                        if (m_DraggingAxis == 0) val.x += deltaX * m_Sensitivity;
                        else if (m_DraggingAxis == 1) val.y += deltaX * m_Sensitivity;
                        else if (m_DraggingAxis == 2) val.z += deltaX * m_Sensitivity;
                        m_Setter(val);
                        m_LastMouseX = mouseX;
                    }
                }
                else { m_DraggingAxis = -1; }
            }

            // 2. 텍스트 입력 로직
            if (m_EditingAxis != -1)
            {
                auto checkKey = [&](int vKey) -> bool {
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

                if (checkKey(VK_RETURN)) {
                    try {
                        float v = std::stof(m_InputBuffer);
                        DirectX::XMFLOAT3 res = m_Getter();
                        if (m_EditingAxis == 0) res.x = v; else if (m_EditingAxis == 1) res.y = v; else if (m_EditingAxis == 2) res.z = v;
                        m_Setter(res);
                    }
                    catch (...) {}
                    m_EditingAxis = -1;
                }
                if (checkKey(VK_ESCAPE)) m_EditingAxis = -1;

                if (isLeftMouseDown) {
                    float startX = m_CalculatedPos.x + labelWidth + (fieldWidth * m_EditingAxis) + dragZoneWidth;
                    if (!(mouseX >= startX && mouseX <= startX + innerBoxWidth - dragZoneWidth &&
                        mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + m_CalculatedSize.y))
                    {
                        try {
                            float v = std::stof(m_InputBuffer);
                            DirectX::XMFLOAT3 res = m_Getter();
                            if (m_EditingAxis == 0) res.x = v; else if (m_EditingAxis == 1) res.y = v; else if (m_EditingAxis == 2) res.z = v;
                            m_Setter(res);
                        }
                        catch (...) {}
                        m_EditingAxis = -1;
                    }
                }
            }

            // 3. 렌더링
            float currentX = m_CalculatedPos.x;
            UIRenderer::DrawString(m_Label, currentX, m_CalculatedPos.y + 18.0f, { 0.8f, 0.8f, 0.8f, 1.0f });
            currentX += labelWidth;

            DirectX::XMFLOAT3 currentVal = m_Getter();
            float vals[3] = { currentVal.x, currentVal.y, currentVal.z };
            std::string labels[3] = { "X", "Y", "Z" };
            DirectX::XMFLOAT4 colors[3] = { {0.9f, 0.3f, 0.3f, 1.0f}, {0.3f, 0.9f, 0.3f, 1.0f}, {0.3f, 0.5f, 0.9f, 1.0f} };

            for (int i = 0; i < 3; ++i) {
                bool isEdit = (m_EditingAxis == i);
                UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y, innerBoxWidth, m_CalculatedSize.y, isEdit ? DirectX::XMFLOAT4{ 0,0,0,1 } : DirectX::XMFLOAT4{ 0.15f,0.15f,0.15f,1 });

                DirectX::XMFLOAT4 drawColor = colors[i];
                if (m_DraggingAxis == i) {
                    drawColor.x = drawColor.x + 0.2f > 1.0f ? 1.0f : drawColor.x + 0.2f;
                    drawColor.y = drawColor.y + 0.2f > 1.0f ? 1.0f : drawColor.y + 0.2f;
                    drawColor.z = drawColor.z + 0.2f > 1.0f ? 1.0f : drawColor.z + 0.2f;
                }
                UIRenderer::DrawString(labels[i], currentX + 5.0f, m_CalculatedPos.y + 18.0f, drawColor);

                std::string txt = isEdit ? m_InputBuffer + "|" : std::to_string(vals[i]).substr(0, std::to_string(vals[i]).find(".") + 3);
                UIRenderer::DrawString(txt, currentX + 22.0f, m_CalculatedPos.y + 18.0f, { 1,1,1,1 });

                currentX += fieldWidth;
            }
            Widget::OnRender();
        }

        bool DragFloat3::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
            float labelWidth = m_CalculatedSize.x * 0.35f;
            float fieldWidth = (m_CalculatedSize.x - labelWidth) / 3.0f;
            float dragZone = 15.0f;

            if (e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y) {
                float startX = m_CalculatedPos.x + labelWidth;
                for (int i = 0; i < 3; ++i) {
                    if (e.GetX() >= startX && e.GetX() <= startX + dragZone) {
                        m_DraggingAxis = i; m_LastMouseX = e.GetX(); e.Handled = true; return true;
                    }
                    else if (e.GetX() > startX + dragZone && e.GetX() <= startX + fieldWidth - 4.0f) {
                        m_EditingAxis = i; m_InputBuffer = ""; e.Handled = true; return true;
                    }
                    startX += fieldWidth;
                }
            }
            return false;
        }
    }
}