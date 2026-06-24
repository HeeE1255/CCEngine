#include "UI/TextInput.h"
#include "Application.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine::UI
{
    TextInput::TextInput(const std::string& name, const std::string& placeholder)
        : Widget(name), m_Placeholder(placeholder)
    {
    }

    void TextInput::OnRender()
    {
        if (!m_IsVisible) return;

        auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
        const bool hovered = IsPointInside(mouseX, mouseY) && !IsMouseBlockedByWidgetAbove(mouseX, mouseY);
        const bool editing = IsKeyboardFocusOwner(this);

        // 기본/마우스 오버/편집 중 상태를 색으로 구분한다.
        // 포커스가 남아 있으면 입력이 끝난 뒤에도 파란색이 유지되므로 공통 포커스 소유자를 기준으로 본다.
        DirectX::XMFLOAT4 background = { 0.10f, 0.10f, 0.11f, 1.0f };
        if (hovered)
            background = { 0.15f, 0.15f, 0.16f, 1.0f };
        if (editing)
            background = { 0.12f, 0.18f, 0.24f, 1.0f };

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, background);
        UIRenderer::DrawString(m_Text.empty() ? m_Placeholder : m_Text,
            m_CalculatedPos.x + 8.0f, m_CalculatedPos.y + 18.0f,
            m_Text.empty() ? DirectX::XMFLOAT4{ 0.55f, 0.55f, 0.55f, 1.0f } : DirectX::XMFLOAT4{ 0.95f, 0.95f, 0.95f, 1.0f });
    }

    void TextInput::Clear()
    {
        if (m_Text.empty()) return;
        m_Text.clear();
        NotifyChanged();
    }

    void TextInput::SetText(const std::string& text, bool notify)
    {
        m_Text = text;
        if (notify)
            NotifyChanged();
    }

    bool TextInput::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetButton() != 0) return false;
        if (!IsPointInside(e.GetX(), e.GetY()))
            return false;

        SetKeyboardFocus(this);
        return true;
    }

    bool TextInput::OnKeyPressed(KeyPressedEvent& e)
    {
        if (!IsKeyboardFocusOwner(this)) return false;
        if (e.GetKeyCode() == 8 && !m_Text.empty())
        {
            m_Text.pop_back();
            NotifyChanged();
        }
        else if (e.GetKeyCode() == 27)
        {
            ClearKeyboardFocus(this);
        }
        return true;
    }

    bool TextInput::OnTextInput(TextInputEvent& e)
    {
        if (!IsKeyboardFocusOwner(this)) return false;
        m_Text.push_back(e.GetCharacter());
        NotifyChanged();
        return true;
    }

    void TextInput::NotifyChanged()
    {
        if (m_OnTextChanged) m_OnTextChanged(m_Text);
    }
}
