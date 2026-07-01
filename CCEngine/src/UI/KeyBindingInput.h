#pragma once
#include "UI/Widget.h"
#include <functional>
#include <string>

namespace CCEngine::UI
{
    class CC_API KeyBindingInput : public Widget
    {
    public:
        KeyBindingInput(const std::string& name = "KeyBindingInput", const std::string& binding = "");

        void OnRender() override;
        void SetBinding(const std::string& binding, bool notify = true);
        const std::string& GetBinding() const { return m_Binding; }
        void SetOnBindingChanged(std::function<void(const std::string&)> callback) { m_OnBindingChanged = std::move(callback); }
        void SetOnPickerRequested(std::function<void(KeyBindingInput*)> callback) { m_OnPickerRequested = std::move(callback); }

    protected:
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

    private:
        std::string m_Binding;
        std::function<void(const std::string&)> m_OnBindingChanged;
        std::function<void(KeyBindingInput*)> m_OnPickerRequested;
    };
}
