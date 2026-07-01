#include "UI/KeyBindingInput.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine::UI
{
    KeyBindingInput::KeyBindingInput(const std::string& name, const std::string& binding)
        : Widget(name), m_Binding(binding)
    {
    }

    void KeyBindingInput::OnRender()
    {
        if (!m_IsVisible)
            return;

        DirectX::XMFLOAT4 background = { 0.10f, 0.10f, 0.11f, 1.0f };

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, background);

        std::string text = m_Binding;
        if (text.empty())
            text = "Unassigned";

        UIRenderer::DrawString(text,
            m_CalculatedPos.x + 8.0f,
            m_CalculatedPos.y + 18.0f,
            m_Binding.empty()
                ? DirectX::XMFLOAT4{ 0.55f, 0.55f, 0.55f, 1.0f }
                : DirectX::XMFLOAT4{ 0.95f, 0.95f, 0.95f, 1.0f });
    }

    void KeyBindingInput::SetBinding(const std::string& binding, bool notify)
    {
        m_Binding = binding;
        if (notify && m_OnBindingChanged)
            m_OnBindingChanged(m_Binding);
    }

    bool KeyBindingInput::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetButton() != 0 || !IsPointInside(e.GetX(), e.GetY()))
            return false;

        // 실제 키보드 입력은 받지 않고, 별도 키 선택 창을 여는 요청만 보낸다.
        if (m_OnPickerRequested)
            m_OnPickerRequested(this);

        e.Handled = true;
        return true;
    }
}
