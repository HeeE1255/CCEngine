#include "UI/KeyBindingPickerPanel.h"
#include "Application.h"
#include "Renderer/UIRenderer.h"
#include "UI/Button.h"
#include <vector>

namespace CCEngine::UI
{
    namespace
    {
        constexpr float KeyWidth = 44.0f;
        constexpr float KeyHeight = 26.0f;
        constexpr float KeyGap = 6.0f;
    }

    KeyBindingPickerPanel::KeyBindingPickerPanel(const std::string& name)
        : WindowPanel(name, "Key Binding")
    {
        SetClipToBounds(true);
        SetDockingEnabled(false);
        BuildKeyboard();
    }

    void KeyBindingPickerPanel::SetInitialBinding(const std::string& binding)
    {
        // 저장된 문자열을 다시 열었을 때 modifier 버튼 상태로 되돌린다.
        m_Ctrl = binding.find("Ctrl+") != std::string::npos;
        m_Shift = binding.find("Shift+") != std::string::npos;
        m_Alt = binding.find("Alt+") != std::string::npos;

        if (m_CtrlButton) m_CtrlButton->SetActive(m_Ctrl);
        if (m_ShiftButton) m_ShiftButton->SetActive(m_Shift);
        if (m_AltButton) m_AltButton->SetActive(m_Alt);
    }

    void KeyBindingPickerPanel::BuildKeyboard()
    {
        m_CtrlButton = CreateButton("KeyPickerCtrl", "Ctrl", 20.0f, 52.0f, 80.0f, KeyHeight);
        m_CtrlButton->SetOnClick([this]() { ToggleModifier(m_Ctrl, m_CtrlButton); });

        m_ShiftButton = CreateButton("KeyPickerShift", "Shift", 108.0f, 52.0f, 80.0f, KeyHeight);
        m_ShiftButton->SetOnClick([this]() { ToggleModifier(m_Shift, m_ShiftButton); });

        m_AltButton = CreateButton("KeyPickerAlt", "Alt", 196.0f, 52.0f, 80.0f, KeyHeight);
        m_AltButton->SetOnClick([this]() { ToggleModifier(m_Alt, m_AltButton); });

        Button* cancel = CreateButton("KeyPickerCancel", "Cancel", 432.0f, 52.0f, 92.0f, KeyHeight);
        cancel->SetOnClick([this]() { ClosePicker(); });

        const std::vector<std::string> row0 = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
        const std::vector<std::string> row1 = { "A", "S", "D", "F", "G", "H", "J", "K", "L" };
        const std::vector<std::string> row2 = { "Z", "X", "C", "V", "B", "N", "M" };
        const std::vector<std::string> row3 = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
        const std::vector<std::string> functionKeys = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8" };

        auto addRow = [this](const std::vector<std::string>& keys, float startX, float y)
        {
            for (size_t i = 0; i < keys.size(); ++i)
                CreateKeyButton(keys[i], startX + (KeyWidth + KeyGap) * (float)i, y);
        };

        addRow(row0, 20.0f, 104.0f);
        addRow(row1, 44.0f, 136.0f);
        addRow(row2, 68.0f, 168.0f);
        addRow(row3, 20.0f, 210.0f);
        addRow(functionKeys, 20.0f, 242.0f);

        CreateKeyButton("Tab", 20.0f, 294.0f, 68.0f);
        CreateKeyButton("Space", 96.0f, 294.0f, 126.0f);
        CreateKeyButton("Enter", 230.0f, 294.0f, 82.0f);
        CreateKeyButton("Backspace", 320.0f, 294.0f, 104.0f);
        CreateKeyButton("Delete", 432.0f, 294.0f, 92.0f);
    }

    Button* KeyBindingPickerPanel::CreateButton(const std::string& name, const std::string& text, float x, float y, float width, float height)
    {
        Button* button = new Button(name, text);
        button->SetAnchorMin(0.0f, 0.0f);
        button->SetAnchorMax(0.0f, 0.0f);
        button->SetOffsetMin(x, y);
        button->SetOffsetMax(x + width, y + height);
        button->SetNormalColor({ 0.20f, 0.20f, 0.21f, 1.0f });
        button->SetHoverColor({ 0.30f, 0.30f, 0.32f, 1.0f });
        button->SetClickColor({ 0.12f, 0.12f, 0.13f, 1.0f });
        AddChild(button);
        return button;
    }

    void KeyBindingPickerPanel::CreateKeyButton(const std::string& keyName, float x, float y, float width)
    {
        Button* button = CreateButton("KeyPicker" + keyName, keyName, x, y, width, KeyHeight);
        button->SetOnClick([this, keyName]() { AcceptBinding(keyName); });
    }

    void KeyBindingPickerPanel::ToggleModifier(bool& value, Button* button)
    {
        value = !value;
        if (button)
            button->SetActive(value);
    }

    std::string KeyBindingPickerPanel::ComposeBinding(const std::string& keyName) const
    {
        std::string binding;
        if (m_Ctrl)
            binding += "Ctrl+";
        if (m_Shift)
            binding += "Shift+";
        if (m_Alt)
            binding += "Alt+";
        binding += keyName;
        return binding;
    }

    void KeyBindingPickerPanel::AcceptBinding(const std::string& keyName)
    {
        if (m_OnBindingSelected)
            m_OnBindingSelected(ComposeBinding(keyName));

        ClosePicker();
    }

    void KeyBindingPickerPanel::ClosePicker()
    {
        if (Window* ownerWindow = GetOwnerWindow())
            ownerWindow->SetShouldClose(true);
        else
            SetVisible(false);
    }

    void KeyBindingPickerPanel::OnRender()
    {
        if (!m_IsVisible)
            return;

        WindowPanel::OnRender();

        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y;
        UIRenderer::DrawString("Select modifiers, then choose a key.",
            x + 20.0f, y + 96.0f, { 0.70f, 0.70f, 0.72f, 1.0f });
    }
}
