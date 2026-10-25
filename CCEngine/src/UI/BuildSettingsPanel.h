#pragma once

#include "Build/GameBuildProfile.h"
#include "Build/GameBuildService.h"
#include "Core/ProjectSettings.h"
#include "UI/WindowPanel.h"

#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <mutex>

namespace CCEngine::UI
{
    class Button;
    class TextInput;

    class CC_API BuildSettingsPanel : public WindowPanel
    {
    public:
        BuildSettingsPanel(const std::string& name = "BuildSettingsPanel");

        void OnOpened();
        void SetProjectSettings(ProjectSettings* settings) { m_ProjectSettings = settings; }
        void SetDiagnosticNavigationCallback(std::function<void(const GameBuildMessage&)> callback)
        {
            m_OnDiagnosticNavigation = std::move(callback);
        }
        void SetAlwaysIncludePickRequestedCallback(std::function<void()> callback)
        {
            m_OnAlwaysIncludePickRequested = std::move(callback);
        }
        bool AddAlwaysIncludeAsset(const std::filesystem::path& assetPath);
        bool RunAlwaysIncludeRegressionCheck(std::string& message);
        void OnUpdate(float deltaTime) override;
        void OnRender() override;
        bool OnEvent(Event& e) override;

    private:
        void SyncControls();
        void SaveProfile();
        void StartBuild(bool runAfterBuild, bool checkTools = false);
        void CancelBuild();
        void OpenOutputFolder();
        void RefreshModeButtons();
        void ShowShaderTools(bool show);
        void RemoveSelectedAlwaysInclude();
        int GetAlwaysIncludeIndexAt(float mouseX, float mouseY) const;
        int GetMessageIndexAt(float mouseX, float mouseY) const;
        std::filesystem::path ResolveOutputDirectory() const;

        GameBuildProfile m_Profile;
        ProjectSettings* m_ProjectSettings = nullptr;
        std::filesystem::path m_ProfilePath = "BuildProfile.ccbuildprofile";
        TextInput* m_ProductName = nullptr;
        TextInput* m_ProductVersion = nullptr;
        TextInput* m_UpdateChannel = nullptr;
        TextInput* m_ExecutableName = nullptr;
        TextInput* m_OutputDirectory = nullptr;
        Button* m_AddAlwaysInclude = nullptr;
        Button* m_RemoveAlwaysInclude = nullptr;
        Button* m_Configuration = nullptr;
        Button* m_DebugSymbols = nullptr;
        Button* m_RuntimeMode = nullptr;
        Button* m_WindowMode = nullptr;
        Button* m_VerifyPlayerFrame = nullptr;
        Button* m_ShaderFallback = nullptr;
        Button* m_Save = nullptr;
        Button* m_Build = nullptr;
        Button* m_BuildAndRun = nullptr;
        Button* m_Cancel = nullptr;
        Button* m_OpenOutput = nullptr;
        Button* m_ShaderTools = nullptr;
        TextInput* m_ToolInputs[4] = {};
        std::vector<Widget*> m_ToolControls;
        bool m_ShowShaderTools = false;
        bool m_CheckingTools = false;
        Button* m_CheckTools = nullptr;
        std::filesystem::path m_ToolLog;
        std::vector<std::string> m_ToolVersions;

        std::future<GameBuildReport> m_BuildTask;
        std::shared_ptr<std::atomic_bool> m_CancelRequested;
        mutable std::mutex m_StatusMutex;
        std::string m_Stage = "Idle";
        std::filesystem::path m_CurrentFile;
        std::vector<GameBuildMessage> m_LastMessages;
        ScrollState m_AlwaysIncludeScroll;
        ScrollState m_MessageScroll;
        float m_Progress = 0.0f;
        bool m_Building = false;
        bool m_RunAfterBuild = false;
        int m_SelectedAlwaysInclude = -1;
        int m_SelectedMessage = -1;
        int m_LastClickedMessage = -1;
        std::chrono::steady_clock::time_point m_LastMessageClickTime = {};
        std::function<void(const GameBuildMessage&)> m_OnDiagnosticNavigation;
        std::function<void()> m_OnAlwaysIncludePickRequested;
    };
}
