#pragma once

#include "Scripting/ScriptMetadata.h"
#include "UI/Widget.h"

#include <functional>
#include <string>

namespace CCEngine::UI
{
    class CC_API ScriptFieldWidget : public Widget
    {
    public:
        ScriptFieldWidget(
            const std::string& name,
            const ScriptFieldInfo& field,
            std::function<std::string()> getter,
            std::function<void(const std::string&)> setter);

        void OnRender() override;
        bool WantsMouseCapture() const override { return m_IsDragging; }

    protected:
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
        bool OnMouseMoved(MouseMovedEvent& e) override;
        bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override;
        bool OnKeyPressed(KeyPressedEvent& e) override;
        bool OnTextInput(TextInputEvent& e) override;

    private:
        float ReadFloat(float fallback = 0.0f) const;
        int ReadInt(int fallback = 0) const;
        void SetNumericValue(float value);
        std::string FormatNumeric(float value) const;
        void BeginTextEdit();
        void CommitTextEdit();
        void DrawInput(float labelWidth, float valueX, float valueWidth, const std::string& displayText);
        void DrawRange(float labelWidth, float valueX, float valueWidth);
        void DrawStep(float labelWidth, float valueX, float valueWidth);
        bool IsNumeric() const;

        ScriptFieldInfo m_Field;
        std::function<std::string()> m_Getter;
        std::function<void(const std::string&)> m_Setter;
        bool m_IsEditing = false;
        bool m_IsDragging = false;
        float m_LastMouseX = 0.0f;
        std::string m_InputBuffer;
    };
}
