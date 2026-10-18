#include "UI/BuildSettingsPanel.h"

#include "Application.h"
#include "Core/ConsoleLog.h"
#include "Core/PathContext.h"
#include "Core/PathUtils.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"
#include "UI/Button.h"
#include "UI/TextInput.h"

#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace CCEngine::UI
{
    namespace
    {
        void Place(Widget* widget, float left, float top, float right, float bottom)
        {
            widget->SetAnchorMin(0.0f, 0.0f);
            widget->SetAnchorMax(1.0f, 0.0f);
            widget->SetOffsetMin(left, top);
            widget->SetOffsetMax(right, bottom);
        }

        void PlaceFixed(Widget* widget, float left, float top, float width, float height)
        {
            widget->SetAnchorMin(0.0f, 0.0f);
            widget->SetAnchorMax(0.0f, 0.0f);
            widget->SetOffsetMin(left, top);
            widget->SetOffsetMax(left + width, top + height);
        }

        constexpr float kAlwaysIncludeTop = 246.0f;
        constexpr float kAlwaysIncludeBottom = 310.0f;
        constexpr float kMessageTop = 713.0f;

        std::string LowerPathKey(const std::filesystem::path& path)
        {
            std::string key = path.lexically_normal().generic_string();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return key;
        }

        std::filesystem::path ExecutableDirectory()
        {
            wchar_t path[32768] = {};
            const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
            return length > 0 && length < std::size(path) ? std::filesystem::path(path).parent_path() : std::filesystem::path{};
        }
    }

    BuildSettingsPanel::BuildSettingsPanel(const std::string& name)
        : WindowPanel(name, "Build Settings")
    {
        SetClipToBounds(true);
        m_ProductName = new TextInput("BuildProductName", "Product Name");
        m_ProductVersion = new TextInput("BuildProductVersion", "0.1.0");
        m_UpdateChannel = new TextInput("BuildUpdateChannel", "Stable");
        m_ExecutableName = new TextInput("BuildExecutableName", "Executable Name");
        m_OutputDirectory = new TextInput("BuildOutputDirectory", "Build/Windows");
        Place(m_ProductName, 180, 66, -24, 92);
        Place(m_ProductVersion, 180, 102, -24, 128);
        Place(m_UpdateChannel, 180, 138, -24, 164);
        Place(m_ExecutableName, 180, 174, -24, 200);
        Place(m_OutputDirectory, 180, 210, -24, 236);
        AddChild(m_ProductName); AddChild(m_ProductVersion); AddChild(m_UpdateChannel);
        AddChild(m_ExecutableName); AddChild(m_OutputDirectory);

        m_AddAlwaysInclude = new Button("AddAlwaysInclude", "Add Asset...");
        m_RemoveAlwaysInclude = new Button("RemoveAlwaysInclude", "Remove");
        PlaceFixed(m_AddAlwaysInclude, 180, 316, 112, 26);
        PlaceFixed(m_RemoveAlwaysInclude, 300, 316, 96, 26);
        AddChild(m_AddAlwaysInclude); AddChild(m_RemoveAlwaysInclude);

        m_Configuration = new Button("BuildConfiguration", "Development");
        m_DebugSymbols = new Button("BuildDebugSymbols", "Included");
        m_RuntimeMode = new Button("BuildRuntimeMode", "Self-contained .NET 8");
        m_WindowMode = new Button("BuildWindowMode", "Windowed");
        m_VerifyPlayerFrame = new Button("BuildVerifyPlayerFrame", "Enabled");
        m_ShaderFallback = new Button("BuildShaderFallback", "Enabled in Development");
        Place(m_Configuration, 180, 350, -24, 378);
        Place(m_DebugSymbols, 180, 384, -24, 412);
        Place(m_RuntimeMode, 180, 418, -24, 446);
        Place(m_WindowMode, 180, 452, -24, 480);
        Place(m_VerifyPlayerFrame, 180, 486, -24, 514);
        Place(m_ShaderFallback, 180, 520, -24, 548);
        AddChild(m_Configuration); AddChild(m_DebugSymbols); AddChild(m_RuntimeMode); AddChild(m_WindowMode); AddChild(m_VerifyPlayerFrame); AddChild(m_ShaderFallback);

        m_Save = new Button("SaveBuildProfile", "Save Profile");
        m_Build = new Button("BuildGame", "Build");
        m_BuildAndRun = new Button("BuildAndRunGame", "Build And Run");
        m_Cancel = new Button("CancelGameBuild", "Cancel");
        m_OpenOutput = new Button("OpenBuildOutput", "Open Output Folder");
        PlaceFixed(m_Save, 24, 620, 126, 30);
        PlaceFixed(m_Build, 158, 620, 112, 30);
        PlaceFixed(m_BuildAndRun, 278, 620, 152, 30);
        PlaceFixed(m_Cancel, 438, 620, 102, 30);
        Place(m_OpenOutput, 548, 620, -24, 650);
        AddChild(m_Save); AddChild(m_Build); AddChild(m_BuildAndRun); AddChild(m_Cancel); AddChild(m_OpenOutput);

        m_ProductName->SetOnTextChanged([this](const std::string& value) { m_Profile.Data().ProductName = value; });
        m_ProductVersion->SetOnTextChanged([this](const std::string& value) { m_Profile.Data().ProductVersion = value; });
        m_UpdateChannel->SetOnTextChanged([this](const std::string& value) { m_Profile.Data().UpdateChannel = value; });
        m_ExecutableName->SetOnTextChanged([this](const std::string& value) { m_Profile.Data().ExecutableName = value; });
        m_OutputDirectory->SetOnTextChanged([this](const std::string& value) { m_Profile.Data().OutputDirectory = value; });
        m_AddAlwaysInclude->SetOnClick([this]()
        {
            if (m_OnAlwaysIncludePickRequested)
                m_OnAlwaysIncludePickRequested();
        });
        m_RemoveAlwaysInclude->SetOnClick([this]() { RemoveSelectedAlwaysInclude(); });
        m_Configuration->SetOnClick([this]() { m_Profile.Data().Configuration = m_Profile.Data().Configuration == GameBuildConfiguration::Development ? GameBuildConfiguration::Release : GameBuildConfiguration::Development; RefreshModeButtons(); });
        m_DebugSymbols->SetOnClick([this]() { m_Profile.Data().IncludeDebugSymbols = !m_Profile.Data().IncludeDebugSymbols; RefreshModeButtons(); });
        m_RuntimeMode->SetOnClick([this]() { m_Profile.Data().ManagedRuntime = m_Profile.Data().ManagedRuntime == ManagedRuntimeDeployment::SelfContained ? ManagedRuntimeDeployment::FrameworkDependent : ManagedRuntimeDeployment::SelfContained; RefreshModeButtons(); });
        m_WindowMode->SetOnClick([this]()
        {
            auto& mode = m_Profile.Data().PlayerWindowMode;
            mode = mode == WindowMode::Windowed ? WindowMode::Borderless
                : (mode == WindowMode::Borderless ? WindowMode::Fullscreen : WindowMode::Windowed);
            RefreshModeButtons();
        });
        m_VerifyPlayerFrame->SetOnClick([this]()
        {
            m_Profile.Data().VerifyPlayerFrame = !m_Profile.Data().VerifyPlayerFrame;
            RefreshModeButtons();
        });
        m_ShaderFallback->SetOnClick([this]()
        {
            m_Profile.Data().AllowDevelopmentShaderFallback = !m_Profile.Data().AllowDevelopmentShaderFallback;
            RefreshModeButtons();
        });
        m_Save->SetOnClick([this]() { SaveProfile(); });
        m_Build->SetOnClick([this]() { StartBuild(false); });
        m_BuildAndRun->SetOnClick([this]() { StartBuild(true); });
        m_Cancel->SetOnClick([this]() { CancelBuild(); });
        m_OpenOutput->SetOnClick([this]() { OpenOutputFolder(); });
    }

    void BuildSettingsPanel::OnOpened()
    {
        std::string error;
        if (!m_Profile.Load(m_ProfilePath, error))
            m_Profile.Save(m_ProfilePath, error);
        SyncControls();
        BringToFront();
    }

    void BuildSettingsPanel::SyncControls()
    {
        const auto& data = m_Profile.Data();
        m_ProductName->SetText(data.ProductName, false);
        m_ProductVersion->SetText(data.ProductVersion, false);
        m_UpdateChannel->SetText(data.UpdateChannel, false);
        m_ExecutableName->SetText(data.ExecutableName, false);
        m_OutputDirectory->SetText(data.OutputDirectory.generic_string(), false);
        if (m_SelectedAlwaysInclude >= static_cast<int>(data.AlwaysIncludeAssets.size()))
            m_SelectedAlwaysInclude = -1;
        m_AlwaysIncludeScroll.ContentHeight = static_cast<float>(data.AlwaysIncludeAssets.size()) * 22.0f;
        m_AlwaysIncludeScroll.ViewportHeight = kAlwaysIncludeBottom - kAlwaysIncludeTop;
        m_AlwaysIncludeScroll.ScrollY = (std::clamp)(m_AlwaysIncludeScroll.ScrollY, 0.0f, m_AlwaysIncludeScroll.GetMaxScroll());
        RefreshModeButtons();
    }

    void BuildSettingsPanel::RefreshModeButtons()
    {
        m_Configuration->SetText(m_Profile.Data().Configuration == GameBuildConfiguration::Release ? "Release" : "Development");
        const bool release = m_Profile.Data().Configuration == GameBuildConfiguration::Release;
        m_DebugSymbols->SetText(release ? "Excluded in Release" : (m_Profile.Data().IncludeDebugSymbols ? "Included" : "Excluded"));
        m_DebugSymbols->SetActive(!release && !m_Building);
        m_RuntimeMode->SetText(m_Profile.Data().ManagedRuntime == ManagedRuntimeDeployment::SelfContained ? "Self-contained .NET 8" : "Framework-dependent .NET 8");
        const WindowMode windowMode = m_Profile.Data().PlayerWindowMode;
        m_WindowMode->SetText(windowMode == WindowMode::Borderless ? "Borderless"
            : (windowMode == WindowMode::Fullscreen ? "Fullscreen" : "Windowed"));
        m_VerifyPlayerFrame->SetText(m_Profile.Data().VerifyPlayerFrame ? "Enabled (recommended)" : "Disabled");
        m_VerifyPlayerFrame->SetActive(!m_Building);
        m_ShaderFallback->SetText(m_Profile.Data().AllowDevelopmentShaderFallback ? "Enabled in Development" : "Disabled");
        m_ShaderFallback->SetActive(!release && !m_Building);
        m_Cancel->SetActive(m_Building);
    }

    void BuildSettingsPanel::SaveProfile()
    {
        std::string error;
        if (m_Profile.Save(m_ProfilePath, error))
            m_Stage = "Profile saved";
        else
            m_Stage = error;
        SyncControls();
    }

    bool BuildSettingsPanel::AddAlwaysIncludeAsset(const std::filesystem::path& assetPath)
    {
        std::error_code ec;
        const std::filesystem::path projectRoot = PathContext::ProjectRoot();
        if (projectRoot.empty())
        {
            m_Stage = "Cannot resolve project root";
            return false;
        }

        ec.clear();
        const std::filesystem::path canonicalAsset = std::filesystem::weakly_canonical(assetPath, ec);
        if (ec || !std::filesystem::is_regular_file(canonicalAsset, ec))
        {
            m_Stage = "Always Include expects an asset file";
            return false;
        }

        ec.clear();
        std::filesystem::path relative;
        if (!PathUtils::TryMakeRelativeInside(canonicalAsset, projectRoot, relative, false))
        {
            m_Stage = "Asset must be inside this project";
            return false;
        }

        const auto first = relative.begin();
        if (first == relative.end() || LowerPathKey(*first) != "assets" ||
            std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end())
        {
            m_Stage = "Always Include only accepts files under assets";
            return false;
        }

        auto& assets = m_Profile.Data().AlwaysIncludeAssets;
        const std::string newKey = LowerPathKey(relative);
        const auto duplicate = std::find_if(assets.begin(), assets.end(), [&](const std::filesystem::path& existing)
        {
            return LowerPathKey(existing) == newKey;
        });
        if (duplicate != assets.end())
        {
            m_SelectedAlwaysInclude = static_cast<int>(std::distance(assets.begin(), duplicate));
            m_Stage = "Asset is already included";
            return true;
        }

        assets.push_back(relative);
        m_SelectedAlwaysInclude = static_cast<int>(assets.size()) - 1;
        m_Stage = "Added: " + relative.generic_string();
        SyncControls();
        return true;
    }

    bool BuildSettingsPanel::RunAlwaysIncludeRegressionCheck(std::string& message)
    {
        const auto originalAssets = m_Profile.Data().AlwaysIncludeAssets;
        const int originalSelection = m_SelectedAlwaysInclude;
        const std::filesystem::path qaRoot = PathContext::ProjectRoot() / "assets" / "__qa_build_settings";
        const std::filesystem::path qaAsset = qaRoot / "always-include.txt";

        std::error_code ec;
        std::filesystem::create_directories(qaRoot, ec);
        std::ofstream output(qaAsset, std::ios::trunc);
        output << "CCEngine Always Include QA";
        output.close();

        const size_t initialCount = m_Profile.Data().AlwaysIncludeAssets.size();
        const bool firstAdd = AddAlwaysIncludeAsset(qaAsset);
        const bool duplicateAdd = AddAlwaysIncludeAsset(qaAsset);
        const bool duplicateSuppressed = m_Profile.Data().AlwaysIncludeAssets.size() == initialCount + 1;
        RemoveSelectedAlwaysInclude();
        const bool removed = m_Profile.Data().AlwaysIncludeAssets.size() == initialCount;

        // QA가 사용자의 실제 Build Profile 편집 상태를 바꾸면 테스트 자체가 제품 데이터를 오염시킨다.
        m_Profile.Data().AlwaysIncludeAssets = originalAssets;
        m_SelectedAlwaysInclude = originalSelection;
        SyncControls();
        std::filesystem::remove_all(qaRoot, ec);

        const bool passed = firstAdd && duplicateAdd && duplicateSuppressed && removed;
        message = passed
            ? "Always Include accepted one project asset, suppressed the duplicate, and removed the selected entry."
            : "Always Include add, duplicate suppression, or removal did not preserve the expected list state.";
        return passed;
    }

    void BuildSettingsPanel::RemoveSelectedAlwaysInclude()
    {
        auto& assets = m_Profile.Data().AlwaysIncludeAssets;
        if (m_SelectedAlwaysInclude < 0 || m_SelectedAlwaysInclude >= static_cast<int>(assets.size()))
            return;

        assets.erase(assets.begin() + m_SelectedAlwaysInclude);
        if (assets.empty())
            m_SelectedAlwaysInclude = -1;
        else if (m_SelectedAlwaysInclude >= static_cast<int>(assets.size()))
            m_SelectedAlwaysInclude = static_cast<int>(assets.size()) - 1;
        m_Stage = "Always Include asset removed";
        SyncControls();
    }

    std::filesystem::path BuildSettingsPanel::ResolveOutputDirectory() const
    {
        return (PathContext::ProjectRoot() / m_Profile.Data().OutputDirectory).lexically_normal();
    }

    void BuildSettingsPanel::StartBuild(bool runAfterBuild)
    {
        if (m_Building)
            return;
        SaveProfile();
        const auto data = m_Profile.Data();
        GameBuildRequest request;
        request.ProjectRoot = PathContext::ProjectRoot();
        request.OutputDirectory = ResolveOutputDirectory();
        request.BinaryDirectory = ExecutableDirectory();
        request.ProductName = data.ProductName;
        request.ProductVersion = data.ProductVersion;
        request.UpdateChannel = data.UpdateChannel;
        request.ExecutableName = data.ExecutableName;
        request.DevelopmentBuild = data.Configuration == GameBuildConfiguration::Development;
        request.ManagedRuntime = data.ManagedRuntime;
        request.PlayerWindowMode = data.PlayerWindowMode;
        request.IncludeDebugSymbols = data.IncludeDebugSymbols;
        request.ExcludeLicensedSampleAssets = data.ExcludeLicensedSampleAssets;
        request.VerifyPlayerFrame = data.VerifyPlayerFrame;
        request.AllowDevelopmentShaderFallback = data.AllowDevelopmentShaderFallback;
        request.AlwaysIncludeAssets = data.AlwaysIncludeAssets;
        m_CancelRequested = std::make_shared<std::atomic_bool>(false);
        request.CancelRequested = m_CancelRequested;
        request.Progress = [this](const std::string& stage, float progress, const std::filesystem::path& file)
        {
            std::lock_guard<std::mutex> lock(m_StatusMutex);
            m_Stage = stage;
            m_Progress = progress;
            m_CurrentFile = file;
        };
        m_Building = true;
        m_RunAfterBuild = runAfterBuild;
        m_LastMessages.clear();
        m_MessageScroll.ScrollY = 0.0f;
        m_SelectedMessage = -1;
        m_LastClickedMessage = -1;
        RefreshModeButtons();
        m_BuildTask = std::async(std::launch::async, [request]() { return GameBuildService().Build(request); });
    }

    void BuildSettingsPanel::CancelBuild()
    {
        if (m_CancelRequested)
        {
            m_CancelRequested->store(true);
            std::lock_guard<std::mutex> lock(m_StatusMutex);
            m_Stage = "Cancelling...";
        }
    }

    void BuildSettingsPanel::OpenOutputFolder()
    {
        const auto output = ResolveOutputDirectory();
        ShellExecuteW(nullptr, L"open", output.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void BuildSettingsPanel::OnUpdate(float deltaTime)
    {
        WindowPanel::OnUpdate(deltaTime);
        if (!m_Building || !m_BuildTask.valid() || m_BuildTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        GameBuildReport report = m_BuildTask.get();
        m_Building = false;
        m_LastMessages = report.Messages;
        m_MessageScroll.ScrollY = 0.0f;
        m_SelectedMessage = -1;
        m_LastClickedMessage = -1;
        const bool cancelled = std::any_of(report.Messages.begin(), report.Messages.end(), [](const GameBuildMessage& message)
        {
            return message.Stage == "Cancel";
        });
        m_Stage = report.Success ? "Build completed" : (cancelled ? "Build cancelled" : "Build failed");
        m_Progress = report.Success ? 1.0f : m_Progress;
        RefreshModeButtons();
        for (const GameBuildMessage& message : report.Messages)
        {
            std::string line = "[Build][" + message.Stage + "] " + message.Message;
            if (!message.Path.empty())
                line += " (" + message.Path.string() + ")";
            if (message.Severity == GameBuildMessageSeverity::Error)
                ConsoleLog::Error(line);
            else if (message.Severity == GameBuildMessageSeverity::Warning)
                ConsoleLog::Warning(line);
            else
                ConsoleLog::Info(line);
        }
        if (report.Success && m_RunAfterBuild)
        {
            std::filesystem::path executableName = m_Profile.Data().ExecutableName;
            if (executableName.extension() != ".exe")
                executableName += ".exe";
            std::wstring executable = (report.OutputDirectory / executableName).wstring();
            ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, report.OutputDirectory.c_str(), SW_SHOWNORMAL);
        }
    }

    void BuildSettingsPanel::OnRender()
    {
        WindowPanel::OnRender();
        const float x = m_CalculatedPos.x + 24.0f;
        const float y = m_CalculatedPos.y;
        const DirectX::XMFLOAT4 label = { 0.72f, 0.72f, 0.74f, 1.0f };
        UIRenderer::DrawString("Product Name", x, y + 84, label);
        UIRenderer::DrawString("Product Version", x, y + 120, label);
        UIRenderer::DrawString("Update Channel", x, y + 156, label);
        UIRenderer::DrawString("Executable", x, y + 192, label);
        UIRenderer::DrawString("Output Folder", x, y + 228, label);
        UIRenderer::DrawString("Always Include", x, y + 264, label);
        UIRenderer::DrawString("Configuration", x, y + 370, label);
        UIRenderer::DrawString("Debug Symbols", x, y + 404, label);
        UIRenderer::DrawString("Managed Runtime", x, y + 438, label);
        UIRenderer::DrawString("Window Mode", x, y + 472, label);
        UIRenderer::DrawString("Verify First Frame", x, y + 506, label);
        UIRenderer::DrawString("Shader Fallback", x, y + 540, label);
        UIRenderer::DrawString("Platform", x, y + 586, label);
        UIRenderer::DrawString("Windows x64 (Direct3D 11)", x + 156, y + 586, { 0.82f, 0.82f, 0.84f, 1.0f });
        if (m_ProjectSettings)
        {
            const auto& project = m_ProjectSettings->Data();
            const std::string scene = project.StartScenePath.empty() ? "None" : project.StartScenePath;
            UIRenderer::DrawString("Start Scene: " + scene, x, y + 44, label);
            UIRenderer::DrawString("Resolution: " + std::to_string(project.GameWidth) + " x " + std::to_string(project.GameHeight), x + 390, y + 44, label);
        }

        const float listX = m_CalculatedPos.x + 180.0f;
        const float listY = y + kAlwaysIncludeTop;
        const float listWidth = (std::max)(80.0f, m_CalculatedSize.x - 204.0f);
        const float listHeight = kAlwaysIncludeBottom - kAlwaysIncludeTop;
        UIRenderer::DrawRectFilled(listX, listY, listWidth, listHeight, { 0.075f, 0.075f, 0.08f, 1.0f });
        UIRenderer::DrawRect({ listX, listY }, { listWidth, listHeight }, { 0.22f, 0.22f, 0.24f, 1.0f });

        auto& includeAssets = m_Profile.Data().AlwaysIncludeAssets;
        m_AlwaysIncludeScroll.ContentHeight = static_cast<float>(includeAssets.size()) * 22.0f;
        m_AlwaysIncludeScroll.ViewportHeight = listHeight;
        m_AlwaysIncludeScroll.ScrollY = (std::clamp)(m_AlwaysIncludeScroll.ScrollY, 0.0f, m_AlwaysIncludeScroll.GetMaxScroll());
        UIRenderer::SetClipRect(listX + 1.0f, listY + 1.0f, listWidth - 2.0f, listHeight - 2.0f);
        if (includeAssets.empty())
        {
            UIRenderer::DrawString("No additional assets", listX + 8.0f, listY + 19.0f, { 0.46f, 0.46f, 0.48f, 1.0f });
        }
        else
        {
            for (size_t i = 0; i < includeAssets.size(); ++i)
            {
                const float rowY = listY + static_cast<float>(i) * 22.0f - m_AlwaysIncludeScroll.ScrollY;
                if (rowY + 22.0f < listY || rowY > listY + listHeight)
                    continue;
                if (static_cast<int>(i) == m_SelectedAlwaysInclude)
                    UIRenderer::DrawRectFilled(listX + 1.0f, rowY, listWidth - 2.0f, 22.0f, { 0.18f, 0.36f, 0.55f, 1.0f });

                std::string path = includeAssets[i].generic_string();
                const size_t maxCharacters = static_cast<size_t>((std::max)(28.0f, listWidth - 20.0f) / 7.0f);
                if (maxCharacters > 3 && path.size() > maxCharacters)
                    path = "..." + path.substr(path.size() - (maxCharacters - 3));
                UIRenderer::DrawString(path, listX + 8.0f, rowY + 17.0f, { 0.84f, 0.84f, 0.86f, 1.0f });
            }
        }
        UIRenderer::ClearClipRect();

        std::lock_guard<std::mutex> lock(m_StatusMutex);
        UIRenderer::DrawRectFilled(x, y + 596, (m_CalculatedSize.x - 48.0f) * m_Progress, 8.0f, { 0.22f, 0.55f, 0.88f, 1.0f });
        UIRenderer::DrawString(m_Stage, x, y + 660, { 0.88f, 0.88f, 0.88f, 1.0f });
        if (!m_CurrentFile.empty())
            UIRenderer::DrawString(m_CurrentFile.filename().string(), x, y + 684, label);
        const float messageTop = y + kMessageTop;
        const float messageBottom = y + m_CalculatedSize.y - 12.0f;
        m_MessageScroll.ContentHeight = static_cast<float>(m_LastMessages.size()) * 22.0f;
        m_MessageScroll.ViewportHeight = (std::max)(0.0f, messageBottom - messageTop);
        m_MessageScroll.ScrollY = (std::clamp)(m_MessageScroll.ScrollY, 0.0f, m_MessageScroll.GetMaxScroll());

        UIRenderer::SetClipRect(x - 4.0f, messageTop, m_CalculatedSize.x - 40.0f, m_MessageScroll.ViewportHeight);
        float messageY = messageTop + 18.0f - m_MessageScroll.ScrollY;
        int displayRow = 0;
        for (auto iterator = m_LastMessages.rbegin(); iterator != m_LastMessages.rend(); ++iterator, ++displayRow)
        {
            if (messageY + 5.0f < messageTop)
            {
                messageY += 22.0f;
                continue;
            }
            if (messageY - 17.0f > messageBottom)
                break;

            const int messageIndex = static_cast<int>(m_LastMessages.size()) - 1 - displayRow;
            if (messageIndex == m_SelectedMessage)
                UIRenderer::DrawRectFilled(x - 4.0f, messageY - 17.0f, m_CalculatedSize.x - 40.0f, 21.0f, { 0.18f, 0.36f, 0.55f, 1.0f });
            const auto color = iterator->Severity == GameBuildMessageSeverity::Error ? DirectX::XMFLOAT4{ 0.95f, 0.35f, 0.32f, 1.0f }
                : (iterator->Severity == GameBuildMessageSeverity::Warning ? DirectX::XMFLOAT4{ 0.92f, 0.68f, 0.25f, 1.0f } : label);
            const std::string navigationHint = "  [double-click]";
            UIRenderer::DrawString(iterator->Stage + ": " + iterator->Message + navigationHint, x, messageY, color);
            messageY += 22.0f;
        }
        UIRenderer::ClearClipRect();

        if (m_MessageScroll.GetMaxScroll() > 0.0f)
        {
            const float thumbHeight = m_MessageScroll.GetThumbHeight();
            const float thumbY = m_MessageScroll.GetThumbY(messageTop);
            const float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 16.0f;
            UIRenderer::DrawRectFilled(thumbX, messageTop, 6.0f, m_MessageScroll.ViewportHeight, { 0.08f, 0.08f, 0.09f, 1.0f });
            UIRenderer::DrawRectFilled(thumbX, thumbY, 6.0f, thumbHeight, { 0.42f, 0.42f, 0.45f, 1.0f });
        }
    }

    bool BuildSettingsPanel::OnEvent(Event& e)
    {
        if (!m_IsVisible)
            return false;

        if (WindowPanel::OnEvent(e))
            return true;

        if (e.GetEventType() == EventType::MouseScrolled)
        {
            auto& scrollEvent = static_cast<MouseScrolledEvent&>(e);
            Window* ownerWindow = GetOwnerWindow();
            auto [mouseX, mouseY] = ownerWindow
                ? ownerWindow->GetMousePosition()
                : Application::Get()->GetWindow().GetMousePosition();
            const float includeLeft = m_CalculatedPos.x + 180.0f;
            const float includeRight = m_CalculatedPos.x + m_CalculatedSize.x - 24.0f;
            const float includeTop = m_CalculatedPos.y + kAlwaysIncludeTop;
            const float includeBottom = m_CalculatedPos.y + kAlwaysIncludeBottom;
            if (mouseX >= includeLeft && mouseX <= includeRight && mouseY >= includeTop && mouseY <= includeBottom)
            {
                m_AlwaysIncludeScroll.ApplyScroll(scrollEvent.GetYOffset() * -1.0f);
                e.Handled = true;
                return true;
            }

            const float messageTop = m_CalculatedPos.y + kMessageTop;
            if (mouseX >= m_CalculatedPos.x + 20.0f &&
                mouseX <= m_CalculatedPos.x + m_CalculatedSize.x - 20.0f &&
                mouseY >= messageTop && mouseY <= m_CalculatedPos.y + m_CalculatedSize.y - 12.0f)
            {
                m_MessageScroll.ApplyScroll(scrollEvent.GetYOffset() * -1.0f);
                e.Handled = true;
                return true;
            }
            return false;
        }

        if (e.GetEventType() != EventType::MouseButtonPressed)
            return false;

        auto& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);
        if (mouseEvent.GetButton() != 0)
            return false;

        const int includeIndex = GetAlwaysIncludeIndexAt(mouseEvent.GetX(), mouseEvent.GetY());
        if (includeIndex >= 0)
        {
            m_SelectedAlwaysInclude = includeIndex;
            e.Handled = true;
            return true;
        }

        const int messageIndex = GetMessageIndexAt(mouseEvent.GetX(), mouseEvent.GetY());
        if (messageIndex < 0)
            return false;

        const auto now = std::chrono::steady_clock::now();
        const bool isDoubleClick = messageIndex == m_LastClickedMessage &&
            now - m_LastMessageClickTime < std::chrono::milliseconds(450);
        m_SelectedMessage = messageIndex;
        m_LastClickedMessage = messageIndex;
        m_LastMessageClickTime = now;

        if (isDoubleClick && m_OnDiagnosticNavigation)
            m_OnDiagnosticNavigation(m_LastMessages[static_cast<size_t>(messageIndex)]);

        e.Handled = true;
        return true;
    }

    int BuildSettingsPanel::GetAlwaysIncludeIndexAt(float mouseX, float mouseY) const
    {
        const float left = m_CalculatedPos.x + 180.0f;
        const float top = m_CalculatedPos.y + kAlwaysIncludeTop;
        const float right = m_CalculatedPos.x + m_CalculatedSize.x - 24.0f;
        const float bottom = m_CalculatedPos.y + kAlwaysIncludeBottom;
        if (mouseX < left || mouseX > right || mouseY < top || mouseY > bottom)
            return -1;

        const int index = static_cast<int>((mouseY - top + m_AlwaysIncludeScroll.ScrollY) / 22.0f);
        return index >= 0 && index < static_cast<int>(m_Profile.Data().AlwaysIncludeAssets.size()) ? index : -1;
    }

    int BuildSettingsPanel::GetMessageIndexAt(float mouseX, float mouseY) const
    {
        const float left = m_CalculatedPos.x + 20.0f;
        const float top = m_CalculatedPos.y + kMessageTop;
        const float right = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;
        const float bottom = m_CalculatedPos.y + m_CalculatedSize.y - 12.0f;
        if (mouseX < left || mouseX > right || mouseY < top || mouseY > bottom)
            return -1;

        const int displayRow = static_cast<int>((mouseY - top + m_MessageScroll.ScrollY) / 22.0f);
        const int messageIndex = static_cast<int>(m_LastMessages.size()) - 1 - displayRow;
        return messageIndex >= 0 && messageIndex < static_cast<int>(m_LastMessages.size()) ? messageIndex : -1;
    }
}
