#pragma once
#include "UI/Widget.h"
#include <functional>

namespace CCEngine::UI
{
    class CC_API TextInput : public Widget
    {
    public:
        TextInput(const std::string& name = "TextInput", const std::string& placeholder = "Search...");

        void OnRender() override;
        void SetOnTextChanged(std::function<void(const std::string&)> callback) { m_OnTextChanged = std::move(callback); }
        const std::string& GetText() const { return m_Text; }
        void SetText(const std::string& text, bool notify = true);
        void Clear();

    protected:
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
        bool OnKeyPressed(KeyPressedEvent& e) override;
        bool OnTextInput(TextInputEvent& e) override;

    private:
        void NotifyChanged();

        std::string m_Text;
        std::string m_Placeholder;
        std::function<void(const std::string&)> m_OnTextChanged;
    };
}
