#pragma once
#include "Core/ProjectSettings.h"
#include "UI/WindowPanel.h"
#include <functional>

namespace CCEngine::UI
{
    class Button;
    class TextInput;

    class CC_API ProjectSettingsPanel : public WindowPanel
    {
    public:
        ProjectSettingsPanel(const std::string& name = "ProjectSettingsPanel");

        void SetSettings(ProjectSettings* settings) { m_Settings = settings; }
        void SetCallbacks(std::function<void()> setStartScene, std::function<void()> openStartScene, std::function<void()> saveSettings, std::function<void()> applyResolution);

        void OnRender() override;
        void OnOpened();

    private:
        enum class SettingsPage { Project = 0, Graphics, Input };

        void SelectPage(SettingsPage page);
        void RefreshPageVisibility();
        void SyncFieldsFromSettings();
        static uint32_t ParseResolutionValue(const std::string& text, uint32_t fallback);

        ProjectSettings* m_Settings = nullptr;
        SettingsPage m_SelectedPage = SettingsPage::Project;
        Button* m_BtnProjectPage = nullptr;
        Button* m_BtnGraphicsPage = nullptr;
        Button* m_BtnInputPage = nullptr;
        TextInput* m_ProjectNameInput = nullptr;
        TextInput* m_GameWidthInput = nullptr;
        TextInput* m_GameHeightInput = nullptr;
        Button* m_BtnSetStartScene = nullptr;
        Button* m_BtnOpenStartScene = nullptr;
        Button* m_BtnSaveSettings = nullptr;
        Button* m_BtnApplyResolution = nullptr;
    };
}
