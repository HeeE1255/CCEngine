#include "UI/ProjectSettingsPanel.h"
#include "Renderer/UIRenderer.h"
#include "UI/Button.h"
#include "UI/TextInput.h"
#include <algorithm>
#include <cctype>

namespace CCEngine::UI
{
    namespace
    {
        constexpr float SidebarWidth = 150.0f;
        constexpr float HeaderHeight = 24.0f;
        constexpr float RowHeight = 28.0f;
    }

    ProjectSettingsPanel::ProjectSettingsPanel(const std::string& name)
        : WindowPanel(name, "Project Settings")
    {
        SetClipToBounds(true);

        m_BtnProjectPage = new Button("ProjectSettingsProjectPage", "Project");
        m_BtnProjectPage->SetAnchorMin(0.0f, 0.0f);
        m_BtnProjectPage->SetAnchorMax(0.0f, 0.0f);
        m_BtnProjectPage->SetOffsetMin(10.0f, 42.0f);
        m_BtnProjectPage->SetOffsetMax(SidebarWidth - 10.0f, 68.0f);
        m_BtnProjectPage->SetOnClick([this]() { SelectPage(SettingsPage::Project); });
        AddChild(m_BtnProjectPage);

        m_BtnGraphicsPage = new Button("ProjectSettingsGraphicsPage", "Graphics");
        m_BtnGraphicsPage->SetAnchorMin(0.0f, 0.0f);
        m_BtnGraphicsPage->SetAnchorMax(0.0f, 0.0f);
        m_BtnGraphicsPage->SetOffsetMin(10.0f, 70.0f);
        m_BtnGraphicsPage->SetOffsetMax(SidebarWidth - 10.0f, 96.0f);
        m_BtnGraphicsPage->SetOnClick([this]() { SelectPage(SettingsPage::Graphics); });
        AddChild(m_BtnGraphicsPage);

        m_BtnInputPage = new Button("ProjectSettingsInputPage", "Input");
        m_BtnInputPage->SetAnchorMin(0.0f, 0.0f);
        m_BtnInputPage->SetAnchorMax(0.0f, 0.0f);
        m_BtnInputPage->SetOffsetMin(10.0f, 98.0f);
        m_BtnInputPage->SetOffsetMax(SidebarWidth - 10.0f, 124.0f);
        m_BtnInputPage->SetOnClick([this]() { SelectPage(SettingsPage::Input); });
        AddChild(m_BtnInputPage);

        m_ProjectNameInput = new TextInput("ProjectNameInput", "Project Name");
        m_ProjectNameInput->SetAnchorMin(0.0f, 0.0f);
        m_ProjectNameInput->SetAnchorMax(1.0f, 0.0f);
        m_ProjectNameInput->SetOffsetMin(SidebarWidth + 120.0f, 98.0f);
        m_ProjectNameInput->SetOffsetMax(-16.0f, 124.0f);
        m_ProjectNameInput->SetOnTextChanged([this](const std::string& text)
            {
                if (m_Settings)
                    m_Settings->Data().ProjectName = text;
            });
        AddChild(m_ProjectNameInput);

        m_GameWidthInput = new TextInput("GameResolutionWidthInput", "Width");
        m_GameWidthInput->SetAnchorMin(0.0f, 0.0f);
        m_GameWidthInput->SetAnchorMax(0.0f, 0.0f);
        m_GameWidthInput->SetOffsetMin(SidebarWidth + 120.0f, 130.0f);
        m_GameWidthInput->SetOffsetMax(SidebarWidth + 250.0f, 156.0f);
        m_GameWidthInput->SetOnTextChanged([this](const std::string& text)
            {
                if (m_Settings)
                    m_Settings->Data().GameWidth = ParseResolutionValue(text, m_Settings->Data().GameWidth);
            });
        AddChild(m_GameWidthInput);

        m_GameHeightInput = new TextInput("GameResolutionHeightInput", "Height");
        m_GameHeightInput->SetAnchorMin(0.0f, 0.0f);
        m_GameHeightInput->SetAnchorMax(0.0f, 0.0f);
        m_GameHeightInput->SetOffsetMin(SidebarWidth + 285.0f, 130.0f);
        m_GameHeightInput->SetOffsetMax(SidebarWidth + 415.0f, 156.0f);
        m_GameHeightInput->SetOnTextChanged([this](const std::string& text)
            {
                if (m_Settings)
                    m_Settings->Data().GameHeight = ParseResolutionValue(text, m_Settings->Data().GameHeight);
            });
        AddChild(m_GameHeightInput);

        m_BtnApplyResolution = new Button("BtnApplyGameResolution", "Save Default Resolution");
        m_BtnApplyResolution->SetAnchorMin(0.0f, 0.0f);
        m_BtnApplyResolution->SetAnchorMax(0.0f, 0.0f);
        m_BtnApplyResolution->SetOffsetMin(SidebarWidth + 120.0f, 174.0f);
        m_BtnApplyResolution->SetOffsetMax(SidebarWidth + 415.0f, 200.0f);
        AddChild(m_BtnApplyResolution);

        m_BtnSetStartScene = new Button("BtnSetStartScene", "Set Current Scene");
        m_BtnSetStartScene->SetAnchorMin(0.0f, 0.0f);
        m_BtnSetStartScene->SetAnchorMax(1.0f, 0.0f);
        m_BtnSetStartScene->SetOffsetMin(SidebarWidth + 120.0f, 198.0f);
        m_BtnSetStartScene->SetOffsetMax(-16.0f, 224.0f);
        AddChild(m_BtnSetStartScene);

        m_BtnOpenStartScene = new Button("BtnOpenStartScene", "Open Start Scene");
        m_BtnOpenStartScene->SetAnchorMin(0.0f, 0.0f);
        m_BtnOpenStartScene->SetAnchorMax(1.0f, 0.0f);
        m_BtnOpenStartScene->SetOffsetMin(SidebarWidth + 120.0f, 230.0f);
        m_BtnOpenStartScene->SetOffsetMax(-16.0f, 256.0f);
        AddChild(m_BtnOpenStartScene);

        m_BtnSaveSettings = new Button("BtnSaveProjectSettings", "Save Project Settings");
        m_BtnSaveSettings->SetAnchorMin(0.0f, 0.0f);
        m_BtnSaveSettings->SetAnchorMax(1.0f, 0.0f);
        m_BtnSaveSettings->SetOffsetMin(SidebarWidth + 120.0f, 308.0f);
        m_BtnSaveSettings->SetOffsetMax(-16.0f, 334.0f);
        AddChild(m_BtnSaveSettings);

        SelectPage(SettingsPage::Project);
    }

    void ProjectSettingsPanel::SetCallbacks(std::function<void()> setStartScene, std::function<void()> openStartScene, std::function<void()> saveSettings, std::function<void()> applyResolution)
    {
        m_BtnSetStartScene->SetOnClick(std::move(setStartScene));
        m_BtnOpenStartScene->SetOnClick(std::move(openStartScene));
        m_BtnSaveSettings->SetOnClick(std::move(saveSettings));
        m_BtnApplyResolution->SetOnClick(std::move(applyResolution));
    }

    void ProjectSettingsPanel::OnOpened()
    {
        SyncFieldsFromSettings();

        SelectPage(m_SelectedPage);
        BringToFront();
    }

    void ProjectSettingsPanel::SelectPage(SettingsPage page)
    {
        m_SelectedPage = page;
        RefreshPageVisibility();
    }

    void ProjectSettingsPanel::RefreshPageVisibility()
    {
        bool isProject = m_SelectedPage == SettingsPage::Project;
        bool isGraphics = m_SelectedPage == SettingsPage::Graphics;
        if (m_ProjectNameInput) m_ProjectNameInput->SetVisible(isProject);
        if (m_BtnSetStartScene) m_BtnSetStartScene->SetVisible(isProject);
        if (m_BtnOpenStartScene) m_BtnOpenStartScene->SetVisible(isProject);
        if (m_BtnSaveSettings) m_BtnSaveSettings->SetVisible(isProject);
        if (m_GameWidthInput) m_GameWidthInput->SetVisible(isGraphics);
        if (m_GameHeightInput) m_GameHeightInput->SetVisible(isGraphics);
        if (m_BtnApplyResolution) m_BtnApplyResolution->SetVisible(isGraphics);

        if (m_BtnProjectPage) m_BtnProjectPage->SetActive(m_SelectedPage == SettingsPage::Project);
        if (m_BtnGraphicsPage) m_BtnGraphicsPage->SetActive(m_SelectedPage == SettingsPage::Graphics);
        if (m_BtnInputPage) m_BtnInputPage->SetActive(m_SelectedPage == SettingsPage::Input);
    }

    void ProjectSettingsPanel::SyncFieldsFromSettings()
    {
        if (!m_Settings)
            return;

        const ProjectSettingsData& data = m_Settings->Data();
        if (m_ProjectNameInput)
            m_ProjectNameInput->SetText(data.ProjectName, false);
        if (m_GameWidthInput)
            m_GameWidthInput->SetText(std::to_string(data.GameWidth), false);
        if (m_GameHeightInput)
            m_GameHeightInput->SetText(std::to_string(data.GameHeight), false);
    }

    uint32_t ProjectSettingsPanel::ParseResolutionValue(const std::string& text, uint32_t fallback)
    {
        if (text.empty())
            return fallback;

        for (char c : text)
        {
            if (!std::isdigit((unsigned char)c))
                return fallback;
        }

        unsigned long value = fallback;
        try
        {
            value = std::stoul(text);
        }
        catch (...)
        {
            value = fallback;
        }
        value = (std::max)(320UL, (std::min)(value, 8192UL));
        return (uint32_t)value;
    }

    void ProjectSettingsPanel::OnRender()
    {
        if (!m_IsVisible)
            return;

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, { 0.17f, 0.17f, 0.18f, 1.0f });
        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, HeaderHeight, { 0.15f, 0.15f, 0.17f, 1.0f });
        UIRenderer::DrawString("Project Settings", m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + 17.0f, { 0.86f, 0.86f, 0.86f, 1.0f });
        UIRenderer::DrawRectFilled(m_CalculatedPos.x + m_CalculatedSize.x - 30.0f, m_CalculatedPos.y, 30.0f, HeaderHeight, { 0.8f, 0.2f, 0.2f, 1.0f });
        UIRenderer::DrawString("X", m_CalculatedPos.x + m_CalculatedSize.x - 20.0f, m_CalculatedPos.y + 17.0f, { 1.0f, 1.0f, 1.0f, 1.0f });

        float left = m_CalculatedPos.x;
        float top = m_CalculatedPos.y + HeaderHeight;
        float contentHeight = m_CalculatedSize.y - HeaderHeight;

        UIRenderer::DrawRectFilled(left, top, SidebarWidth, contentHeight, { 0.11f, 0.11f, 0.12f, 1.0f });
        UIRenderer::DrawRectFilled(left + SidebarWidth, top, 1.0f, contentHeight, { 0.24f, 0.24f, 0.25f, 1.0f });

        const float detailX = m_CalculatedPos.x + SidebarWidth + 18.0f;
        float y = m_CalculatedPos.y + 58.0f;
        const ProjectSettingsData data = m_Settings ? m_Settings->Data() : ProjectSettingsData{};

        if (m_SelectedPage == SettingsPage::Project)
        {
            UIRenderer::DrawString("Project", detailX, y, { 0.92f, 0.92f, 0.92f, 1.0f });
            y += 34.0f;
            UIRenderer::DrawString("Name", detailX, y, { 0.68f, 0.68f, 0.68f, 1.0f });
            y += 76.0f;

            std::string startScene = data.StartScenePath.empty() ? "(none)" : data.StartScenePath;
            if (startScene.size() > 54)
                startScene = "..." + startScene.substr(startScene.size() - 51);

            UIRenderer::DrawString("Start Scene", detailX, y, { 0.68f, 0.68f, 0.68f, 1.0f });
            UIRenderer::DrawString(startScene, detailX + 120.0f, y, { 0.88f, 0.88f, 0.88f, 1.0f });
            y += 112.0f;
            UIRenderer::DrawString("Game Size", detailX, y, { 0.68f, 0.68f, 0.68f, 1.0f });
            UIRenderer::DrawString(std::to_string(data.GameWidth) + " x " + std::to_string(data.GameHeight),
                detailX + 120.0f, y, { 0.88f, 0.88f, 0.88f, 1.0f });
        }
        else if (m_SelectedPage == SettingsPage::Graphics)
        {
            UIRenderer::DrawString("Graphics", detailX, y, { 0.92f, 0.92f, 0.92f, 1.0f });
            y += 44.0f;
            UIRenderer::DrawString("Default Game Resolution", detailX, y, { 0.68f, 0.68f, 0.68f, 1.0f });
            UIRenderer::DrawString("Width", detailX + 120.0f, y + 24.0f, { 0.62f, 0.62f, 0.62f, 1.0f });
            UIRenderer::DrawString("Height", detailX + 285.0f, y + 24.0f, { 0.62f, 0.62f, 0.62f, 1.0f });
            UIRenderer::DrawString("Used as the default resolution for play/build output.", detailX, y + 88.0f, { 0.62f, 0.62f, 0.62f, 1.0f });
        }
        else
        {
            UIRenderer::DrawString("Input", detailX, y, { 0.92f, 0.92f, 0.92f, 1.0f });
            UIRenderer::DrawString("No input settings yet.", detailX, y + 36.0f, { 0.62f, 0.62f, 0.62f, 1.0f });
        }

        Widget::OnRender();
    }
}
