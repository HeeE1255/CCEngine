#pragma once
#include "UI/WindowPanel.h"
#include <functional>
#include <string>

namespace CCEngine::UI
{
    class Button;

    class CC_API KeyBindingPickerPanel : public WindowPanel
    {
    public:
        KeyBindingPickerPanel(const std::string& name = "KeyBindingPickerPanel");

        void SetInitialBinding(const std::string& binding);
        void SetOnBindingSelected(std::function<void(const std::string&)> callback) { m_OnBindingSelected = std::move(callback); }
        void OnRender() override;

    private:
        void BuildKeyboard();
        Button* CreateButton(const std::string& name, const std::string& text, float x, float y, float width, float height);
        void CreateKeyButton(const std::string& keyName, float x, float y, float width = 44.0f);
        void ToggleModifier(bool& value, Button* button);
        std::string ComposeBinding(const std::string& keyName) const;
        void AcceptBinding(const std::string& keyName);
        void ClosePicker();

        bool m_Ctrl = false;
        bool m_Shift = false;
        bool m_Alt = false;

        Button* m_CtrlButton = nullptr;
        Button* m_ShiftButton = nullptr;
        Button* m_AltButton = nullptr;
        std::function<void(const std::string&)> m_OnBindingSelected;
    };
}
