#pragma once
#include "Core/ProjectSettings.h"
#include "UI/WindowPanel.h"
#include <functional>

namespace CCEngine::UI
{
    class Button;
    class KeyBindingInput;
    class TextInput;

    class CC_API ProjectSettingsPanel : public WindowPanel
    {
    public:
        ProjectSettingsPanel(const std::string& name = "ProjectSettingsPanel");

        void SetSettings(ProjectSettings* settings) { m_Settings = settings; }
        void SetCallbacks(std::function<void()> setStartScene, std::function<void()> openStartScene, std::function<void()> saveSettings, std::function<void()> applyResolution);
        void SetKeyBindingPickerCallback(std::function<void(KeyBindingInput*)> callback);

        void OnRender() override;
        bool OnEvent(Event& e) override;
        void OnOpened();

    private:
        enum class SettingsPage { Project = 0, Graphics, Input };

        bool HandleSidebarPageClick(float mouseX, float mouseY);
        void SelectPage(SettingsPage page);
        void RefreshPageVisibility();
        void SyncFieldsFromSettings();
        static uint32_t ParseResolutionValue(const std::string& text, uint32_t fallback);

        ProjectSettings* m_Settings = nullptr;
        std::function<void(KeyBindingInput*)> m_OpenKeyBindingPicker;
        SettingsPage m_SelectedPage = SettingsPage::Project;
        Button* m_BtnProjectPage = nullptr;
        Button* m_BtnGraphicsPage = nullptr;
        Button* m_BtnInputPage = nullptr;
        TextInput* m_ProjectNameInput = nullptr;
        TextInput* m_VisualStudioPathInput = nullptr;
        TextInput* m_GameWidthInput = nullptr;
        TextInput* m_GameHeightInput = nullptr;
        KeyBindingInput* m_MoveForwardInput = nullptr;
        KeyBindingInput* m_MoveBackwardInput = nullptr;
        KeyBindingInput* m_MoveLeftInput = nullptr;
        KeyBindingInput* m_MoveRightInput = nullptr;
        KeyBindingInput* m_MoveUpInput = nullptr;
        KeyBindingInput* m_MoveDownInput = nullptr;
        Button* m_BtnSetStartScene = nullptr;
        Button* m_BtnOpenStartScene = nullptr;
        Button* m_BtnDetectVisualStudio = nullptr;
        Button* m_BtnBrowseVisualStudio = nullptr;
        Button* m_BtnSaveSettings = nullptr;
        Button* m_BtnApplyResolution = nullptr;
    };
}
