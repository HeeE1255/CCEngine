#include "UI/ScriptFieldWidget.h"

#include "Application.h"
#include "Renderer/UIRenderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace CCEngine::UI
{
    namespace
    {
        bool IsInside(float x, float y, float left, float top, float width, float height)
        {
            return x >= left && x <= left + width && y >= top && y <= top + height;
        }
    }

    ScriptFieldWidget::ScriptFieldWidget(
        const std::string& name,
        const ScriptFieldInfo& field,
        std::function<std::string()> getter,
        std::function<void(const std::string&)> setter)
        : Widget(name), m_Field(field), m_Getter(std::move(getter)), m_Setter(std::move(setter))
    {
    }

    void ScriptFieldWidget::OnRender()
    {
        if (!m_IsVisible)
            return;

        if (m_IsEditing && !IsKeyboardFocusOwner(this))
            CommitTextEdit();

        Window* renderWindow = Widget::GetCurrentRenderWindow();
        auto [mouseX, mouseY] = renderWindow
            ? renderWindow->GetMousePosition()
            : CCEngine::Application::Get()->GetWindow().GetMousePosition();

        const float labelWidth = (std::min)(120.0f, m_CalculatedSize.x * 0.38f);
        const float valueX = m_CalculatedPos.x + labelWidth;
        const float valueWidth = (std::max)(0.0f, m_CalculatedSize.x - labelWidth);

        UIRenderer::DrawString(m_Field.Name, m_CalculatedPos.x, m_CalculatedPos.y + 18.0f, { 0.72f, 0.72f, 0.72f, 1.0f });

        if (m_Field.Display == ScriptFieldDisplay::Range && IsNumeric())
            DrawRange(labelWidth, valueX, valueWidth);
        else if (m_Field.Display == ScriptFieldDisplay::Step && IsNumeric())
            DrawStep(labelWidth, valueX, valueWidth);
        else
        {
            const std::string text = m_IsEditing ? m_InputBuffer + "|" : m_Getter();
            DrawInput(labelWidth, valueX, valueWidth, text);
        }

        if (m_Field.Display == ScriptFieldDisplay::ReadOnly || m_Field.ReadOnly)
            UIRenderer::DrawString("lock", valueX + valueWidth - 34.0f, m_CalculatedPos.y + 18.0f, { 0.50f, 0.50f, 0.52f, 1.0f });
    }

    bool ScriptFieldWidget::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetButton() != 0 || !IsPointInside(e.GetX(), e.GetY()))
            return false;

        if (m_Field.Display == ScriptFieldDisplay::ReadOnly || m_Field.ReadOnly)
            return true;

        const float labelWidth = (std::min)(120.0f, m_CalculatedSize.x * 0.38f);
        const float valueX = m_CalculatedPos.x + labelWidth;
        const float valueWidth = (std::max)(0.0f, m_CalculatedSize.x - labelWidth);

        if (m_Field.Display == ScriptFieldDisplay::Step && IsNumeric())
        {
            const float buttonWidth = 24.0f;
            if (IsInside(e.GetX(), e.GetY(), valueX, m_CalculatedPos.y, buttonWidth, m_CalculatedSize.y))
            {
                SetNumericValue(ReadFloat() - m_Field.Step);
                return true;
            }
            if (IsInside(e.GetX(), e.GetY(), valueX + valueWidth - buttonWidth, m_CalculatedPos.y, buttonWidth, m_CalculatedSize.y))
            {
                SetNumericValue(ReadFloat() + m_Field.Step);
                return true;
            }
        }

        if (m_Field.Display == ScriptFieldDisplay::Range && IsNumeric())
        {
            const float ratio = (std::clamp)((e.GetX() - valueX) / (std::max)(1.0f, valueWidth), 0.0f, 1.0f);
            SetNumericValue(m_Field.Min + (m_Field.Max - m_Field.Min) * ratio);
            m_IsDragging = true;
            BeginMouseInteraction(this);
            return true;
        }

        if (m_Field.Display == ScriptFieldDisplay::Drag && IsNumeric())
        {
            m_IsDragging = true;
            m_LastMouseX = e.GetX();
            BeginMouseInteraction(this);
            return true;
        }

        BeginTextEdit();
        return true;
    }

    bool ScriptFieldWidget::OnMouseMoved(MouseMovedEvent& e)
    {
        if (!m_IsDragging)
            return false;

        if (m_Field.Display == ScriptFieldDisplay::Range)
        {
            const float labelWidth = (std::min)(120.0f, m_CalculatedSize.x * 0.38f);
            const float valueX = m_CalculatedPos.x + labelWidth;
            const float valueWidth = (std::max)(1.0f, m_CalculatedSize.x - labelWidth);
            const float ratio = (std::clamp)((e.GetX() - valueX) / valueWidth, 0.0f, 1.0f);
            SetNumericValue(m_Field.Min + (m_Field.Max - m_Field.Min) * ratio);
            return true;
        }

        if (m_Field.Display == ScriptFieldDisplay::Drag)
        {
            const float delta = e.GetX() - m_LastMouseX;
            if (delta != 0.0f)
            {
                SetNumericValue(ReadFloat() + delta * m_Field.Step);
                m_LastMouseX = e.GetX();
            }
            return true;
        }

        return false;
    }

    bool ScriptFieldWidget::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        if (e.GetButton() == 0 && m_IsDragging)
        {
            m_IsDragging = false;
            EndMouseInteraction(this);
            return true;
        }
        return false;
    }

    bool ScriptFieldWidget::OnKeyPressed(KeyPressedEvent& e)
    {
        if (!m_IsEditing || !IsKeyboardFocusOwner(this))
            return false;

        if (e.GetKeyCode() == 8 && !m_InputBuffer.empty())
            m_InputBuffer.pop_back();
        else if (e.GetKeyCode() == 13)
            CommitTextEdit();
        else if (e.GetKeyCode() == 27)
        {
            m_IsEditing = false;
            ClearKeyboardFocus(this);
        }
        return true;
    }

    bool ScriptFieldWidget::OnTextInput(TextInputEvent& e)
    {
        if (!m_IsEditing || !IsKeyboardFocusOwner(this))
            return false;

        char c = e.GetCharacter();
        if (m_Field.Type == ScriptFieldType::String ||
            std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == ',')
        {
            m_InputBuffer.push_back(c);
        }
        return true;
    }

    float ScriptFieldWidget::ReadFloat(float fallback) const
    {
        try { return std::stof(m_Getter()); }
        catch (...) { return fallback; }
    }

    int ScriptFieldWidget::ReadInt(int fallback) const
    {
        try { return std::stoi(m_Getter()); }
        catch (...) { return fallback; }
    }

    void ScriptFieldWidget::SetNumericValue(float value)
    {
        if (m_Field.Display == ScriptFieldDisplay::Range)
            value = (std::clamp)(value, m_Field.Min, m_Field.Max);
        m_Setter(FormatNumeric(value));
    }

    std::string ScriptFieldWidget::FormatNumeric(float value) const
    {
        if (m_Field.Type == ScriptFieldType::Int)
            return std::to_string((int)std::round(value));

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        return stream.str();
    }

    void ScriptFieldWidget::BeginTextEdit()
    {
        m_IsEditing = true;
        m_InputBuffer = m_Getter();
        SetKeyboardFocus(this);
    }

    void ScriptFieldWidget::CommitTextEdit()
    {
        if (m_Field.Type == ScriptFieldType::Int)
        {
            try { m_Setter(std::to_string(std::stoi(m_InputBuffer))); }
            catch (...) { m_Setter("0"); }
        }
        else if (m_Field.Type == ScriptFieldType::Float)
        {
            try { m_Setter(FormatNumeric(std::stof(m_InputBuffer))); }
            catch (...) { m_Setter("0.000"); }
        }
        else
            m_Setter(m_InputBuffer);

        m_IsEditing = false;
        ClearKeyboardFocus(this);
    }

    void ScriptFieldWidget::DrawInput(float, float valueX, float valueWidth, const std::string& displayText)
    {
        DirectX::XMFLOAT4 background = m_IsEditing ? DirectX::XMFLOAT4{ 0.12f, 0.18f, 0.24f, 1.0f } : DirectX::XMFLOAT4{ 0.08f, 0.08f, 0.09f, 1.0f };
        if (m_Field.Display == ScriptFieldDisplay::ReadOnly || m_Field.ReadOnly)
            background = { 0.10f, 0.10f, 0.105f, 1.0f };
        UIRenderer::DrawRectFilled(valueX, m_CalculatedPos.y, valueWidth, m_CalculatedSize.y, background);
        UIRenderer::DrawString(displayText, valueX + 8.0f, m_CalculatedPos.y + 18.0f, { 0.88f, 0.88f, 0.88f, 1.0f });
    }

    void ScriptFieldWidget::DrawRange(float, float valueX, float valueWidth)
    {
        float value = ReadFloat();
        float ratio = 0.0f;
        if (m_Field.Max != m_Field.Min)
            ratio = (std::clamp)((value - m_Field.Min) / (m_Field.Max - m_Field.Min), 0.0f, 1.0f);

        UIRenderer::DrawRectFilled(valueX, m_CalculatedPos.y + 6.0f, valueWidth, 12.0f, { 0.08f, 0.08f, 0.09f, 1.0f });
        UIRenderer::DrawRectFilled(valueX, m_CalculatedPos.y + 6.0f, valueWidth * ratio, 12.0f, { 0.22f, 0.38f, 0.58f, 1.0f });
        UIRenderer::DrawString(FormatNumeric(value), valueX + valueWidth - 58.0f, m_CalculatedPos.y + 18.0f, { 0.92f, 0.92f, 0.92f, 1.0f });
    }

    void ScriptFieldWidget::DrawStep(float, float valueX, float valueWidth)
    {
        const float buttonWidth = 24.0f;
        UIRenderer::DrawRectFilled(valueX, m_CalculatedPos.y, buttonWidth, m_CalculatedSize.y, { 0.15f, 0.15f, 0.16f, 1.0f });
        UIRenderer::DrawRectFilled(valueX + buttonWidth, m_CalculatedPos.y, valueWidth - buttonWidth * 2.0f, m_CalculatedSize.y, { 0.08f, 0.08f, 0.09f, 1.0f });
        UIRenderer::DrawRectFilled(valueX + valueWidth - buttonWidth, m_CalculatedPos.y, buttonWidth, m_CalculatedSize.y, { 0.15f, 0.15f, 0.16f, 1.0f });
        UIRenderer::DrawString("-", valueX + 8.0f, m_CalculatedPos.y + 18.0f, { 0.90f, 0.90f, 0.90f, 1.0f });
        UIRenderer::DrawString("+", valueX + valueWidth - 17.0f, m_CalculatedPos.y + 18.0f, { 0.90f, 0.90f, 0.90f, 1.0f });
        UIRenderer::DrawString(m_Getter(), valueX + buttonWidth + 8.0f, m_CalculatedPos.y + 18.0f, { 0.90f, 0.90f, 0.90f, 1.0f });
    }

    bool ScriptFieldWidget::IsNumeric() const
    {
        return m_Field.Type == ScriptFieldType::Float || m_Field.Type == ScriptFieldType::Int;
    }
}
