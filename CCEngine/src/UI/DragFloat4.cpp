#include "DragFloat4.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"
#include <iomanip>
#include <sstream>

#define NOMINMAX
#include <windows.h>

namespace CCEngine {
    namespace UI {

        DragFloat4::DragFloat4(const std::string& name, const std::string& label, std::function<DirectX::XMFLOAT4()> getter, std::function<void(DirectX::XMFLOAT4)> setter)
            : Widget(name), m_Label(label), m_Getter(getter), m_Setter(setter)
        {
        }

        void DragFloat4::OnRender()
        {
            if (!m_IsVisible) return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            auto [mouseX, mouseY] = window.GetMousePosition();
            bool isLeftMouseDown = window.IsMouseButtonPressed(0);

            float labelWidth = m_CalculatedSize.x * 0.35f;
            float colorBoxWidth = 30.0f;
            float colorBoxSpacing = 8.0f;

            float fieldsTotalWidth = m_CalculatedSize.x - labelWidth - colorBoxWidth - colorBoxSpacing;
            float fieldWidth = fieldsTotalWidth / 4.0f;
            float dragZoneWidth = 15.0f;
            float spacing = 2.0f;
            float innerBoxWidth = fieldWidth - spacing;

            auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

            // 1. 드래그 로직
            if (m_DraggingAxis != -1)
            {
                if (isLeftMouseDown) {
                    float deltaX = mouseX - m_LastMouseX;
                    if (deltaX != 0.0f) {
                        DirectX::XMFLOAT4 val = m_Getter();
                        if (m_DraggingAxis == 0) val.x += deltaX * m_Sensitivity;
                        else if (m_DraggingAxis == 1) val.y += deltaX * m_Sensitivity;
                        else if (m_DraggingAxis == 2) val.z += deltaX * m_Sensitivity;
                        else if (m_DraggingAxis == 3) val.w += deltaX * m_Sensitivity;

                        val.x = clamp01(val.x); val.y = clamp01(val.y); val.z = clamp01(val.z); val.w = clamp01(val.w);
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
                if (checkKey(VK_OEM_PERIOD) || checkKey(VK_DECIMAL)) m_InputBuffer += ".";
                if (checkKey(VK_BACK) && !m_InputBuffer.empty()) m_InputBuffer.pop_back();

                if (checkKey(VK_RETURN)) {
                    try {
                        float v = clamp01(std::stof(m_InputBuffer));
                        DirectX::XMFLOAT4 res = m_Getter();
                        if (m_EditingAxis == 0) res.x = v; else if (m_EditingAxis == 1) res.y = v;
                        else if (m_EditingAxis == 2) res.z = v; else if (m_EditingAxis == 3) res.w = v;
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
                            float v = clamp01(std::stof(m_InputBuffer));
                            DirectX::XMFLOAT4 res = m_Getter();
                            if (m_EditingAxis == 0) res.x = v; else if (m_EditingAxis == 1) res.y = v;
                            else if (m_EditingAxis == 2) res.z = v; else if (m_EditingAxis == 3) res.w = v;
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

            DirectX::XMFLOAT4 currentVal = m_Getter();
            float vals[4] = { currentVal.x, currentVal.y, currentVal.z, currentVal.w };
            std::string labels[4] = { "R", "G", "B", "A" };
            DirectX::XMFLOAT4 colors[4] = { {0.9f, 0.3f, 0.3f, 1.0f}, {0.3f, 0.9f, 0.3f, 1.0f}, {0.3f, 0.5f, 0.9f, 1.0f}, {0.9f, 0.9f, 0.9f, 1.0f} };

            for (int i = 0; i < 4; ++i) {
                bool isEdit = (m_EditingAxis == i);
                UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y, innerBoxWidth, m_CalculatedSize.y, isEdit ? DirectX::XMFLOAT4{ 0,0,0,1 } : DirectX::XMFLOAT4{ 0.15f,0.15f,0.15f,1 });

                DirectX::XMFLOAT4 drawColor = colors[i];
                if (m_DraggingAxis == i) { drawColor.x = clamp01(drawColor.x + 0.2f); drawColor.y = clamp01(drawColor.y + 0.2f); drawColor.z = clamp01(drawColor.z + 0.2f); }
                UIRenderer::DrawString(labels[i], currentX + 3.0f, m_CalculatedPos.y + 18.0f, drawColor);

                std::string txt = isEdit ? m_InputBuffer + "|" : std::to_string(vals[i]).substr(0, std::to_string(vals[i]).find(".") + 3);
                UIRenderer::DrawString(txt, currentX + 15.0f, m_CalculatedPos.y + 18.0f, { 1,1,1,1 });

                currentX += fieldWidth;
            }

            // 우측 끝 Color Block (체크무늬 배경 + 실제 컬러)
            currentX += colorBoxSpacing;
            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + 2.0f, colorBoxWidth, m_CalculatedSize.y - 4.0f, { 0.3f, 0.3f, 0.3f, 1.0f });
            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + 2.0f, colorBoxWidth / 2.0f, (m_CalculatedSize.y - 4.0f) / 2.0f, { 0.5f, 0.5f, 0.5f, 1.0f });
            UIRenderer::DrawRectFilled(currentX + (colorBoxWidth / 2.0f), m_CalculatedPos.y + 2.0f + ((m_CalculatedSize.y - 4.0f) / 2.0f), colorBoxWidth / 2.0f, (m_CalculatedSize.y - 4.0f) / 2.0f, { 0.5f, 0.5f, 0.5f, 1.0f });

            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + 2.0f, colorBoxWidth, m_CalculatedSize.y - 4.0f, currentVal);

            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + 2.0f, colorBoxWidth, 1.0f, { 0.1f, 0.1f, 0.1f, 1.0f });
            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + m_CalculatedSize.y - 2.0f, colorBoxWidth, 1.0f, { 0.1f, 0.1f, 0.1f, 1.0f });
            UIRenderer::DrawRectFilled(currentX, m_CalculatedPos.y + 2.0f, 1.0f, m_CalculatedSize.y - 4.0f, { 0.1f, 0.1f, 0.1f, 1.0f });
            UIRenderer::DrawRectFilled(currentX + colorBoxWidth - 1.0f, m_CalculatedPos.y + 2.0f, 1.0f, m_CalculatedSize.y - 4.0f, { 0.1f, 0.1f, 0.1f, 1.0f });

            Widget::OnRender();
        }

        bool DragFloat4::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            float labelWidth = m_CalculatedSize.x * 0.35f;
            float colorBoxWidth = 30.0f;
            float colorBoxSpacing = 8.0f;
            float fieldsTotalWidth = m_CalculatedSize.x - labelWidth - colorBoxWidth - colorBoxSpacing;
            float fieldWidth = fieldsTotalWidth / 4.0f;
            float dragZone = 15.0f;

            if (e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y)
            {
                float colorBoxStartX = m_CalculatedPos.x + labelWidth + fieldsTotalWidth + colorBoxSpacing;
                if (e.GetX() >= colorBoxStartX && e.GetX() <= colorBoxStartX + colorBoxWidth) {
                    // TODO: Color Picker Popup
                    e.Handled = true; return true;
                }

                float startX = m_CalculatedPos.x + labelWidth;
                for (int i = 0; i < 4; ++i) {
                    if (e.GetX() >= startX && e.GetX() <= startX + dragZone) {
                        m_DraggingAxis = i; m_LastMouseX = e.GetX(); e.Handled = true; return true;
                    }
                    else if (e.GetX() > startX + dragZone && e.GetX() <= startX + fieldWidth - 2.0f) {
                        m_EditingAxis = i; m_InputBuffer = ""; e.Handled = true; return true;
                    }
                    startX += fieldWidth;
                }
            }
            return false;
        }

    }
}