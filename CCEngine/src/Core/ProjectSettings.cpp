#include "Core/ProjectSettings.h"
#include "Core/ConsoleLog.h"
#include "Renderer/RendererAPI.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace CCEngine
{
    namespace
    {
        constexpr uint32_t MinGameResolution = 320;
        constexpr uint32_t MaxGameResolution = 8192;

        ProjectSettingsData NormalizedCopy(ProjectSettingsData data)
        {
            if (data.ProjectName.empty())
            data.ProjectName = "CCEngine Project";

        data.GameWidth = (std::max)(MinGameResolution, (std::min)(data.GameWidth, MaxGameResolution));
        data.GameHeight = (std::max)(MinGameResolution, (std::min)(data.GameHeight, MaxGameResolution));

        // 입력값이 비어 있으면 단축키 판정 쪽에서 의미가 없어지므로 기본값으로 되돌린다.
        if (data.MoveForwardKey.empty()) data.MoveForwardKey = "W";
        if (data.MoveBackwardKey.empty()) data.MoveBackwardKey = "S";
        if (data.MoveLeftKey.empty()) data.MoveLeftKey = "A";
        if (data.MoveRightKey.empty()) data.MoveRightKey = "D";
        if (data.MoveUpKey.empty()) data.MoveUpKey = "E";
        if (data.MoveDownKey.empty()) data.MoveDownKey = "Q";
        return data;
    }
    }

    std::vector<std::string> ProjectSettings::ResolveEditorGraphicsArguments(
        const std::vector<std::string>& arguments, const std::string& filepath)
    {
        // 창을 만든 뒤에는 장치와 UI 자원이 이미 묶인다. 설정은 반드시 생성 전에 읽고,
        // 명시적인 실행 인수는 손상된 설정 파일을 수정할 수 있는 복구 경로로 남긴다.
        for (const auto& argument : arguments)
            if (argument == "-dx11" || argument == "-dx12" || argument == "-opengl" || argument == "-vulkan")
                return arguments;

        std::string name = "DirectX11";
        if (std::filesystem::exists(filepath))
        {
            try
            {
                std::ifstream input(filepath);
                if (!input) throw std::runtime_error("Cannot read project settings.");
                nlohmann::json settings;
                input >> settings;
                if (!settings.is_object()) throw std::runtime_error("Project settings must be an object.");
                name = settings.value("EditorGraphicsAPI", name);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error("Editor graphics settings: " + std::string(error.what())
                    + " Start with -dx11 to recover. File: " + filepath);
            }
        }

        struct Choice { const char* Name; const char* Flag; RendererAPI::API API; };
        const Choice choices[] = {
            { "DirectX11", "-dx11", RendererAPI::API::DirectX11 },
            { "DirectX12", "-dx12", RendererAPI::API::DirectX12 },
            { "OpenGL", "-opengl", RendererAPI::API::OpenGL },
            { "Vulkan", "-vulkan", RendererAPI::API::Vulkan }
        };
        for (const auto& choice : choices)
        {
            if (name != choice.Name) continue;
            if (const char* reason = RendererAPI::GetUnavailableReason(choice.API))
                throw std::runtime_error(name + ": " + reason + " Start with -dx11 to recover.");
            auto resolved = arguments;
            resolved.emplace_back(choice.Flag);
            return resolved;
        }
        throw std::runtime_error("Unknown EditorGraphicsAPI: " + name + ". Start with -dx11 to recover.");
    }

    bool ProjectSettings::Load(const std::string& filepath)
    {
        std::ifstream in(filepath);
        if (!in.is_open())
        {
            Normalize();
            Save(filepath);
            ConsoleLog::Warning("Project settings file missing. Created defaults: " + filepath);
            return false;
        }

        nlohmann::json data;
        try
        {
            in >> data;
            if (!data.is_object()) throw std::runtime_error("Project settings must be an object.");
        }
        catch (...)
        {
            Normalize();
            ConsoleLog::Error("Failed to parse project settings. Using defaults: " + filepath);
            return false;
        }

        m_Data.ProjectName = data.value("ProjectName", m_Data.ProjectName);
        m_Data.StartScenePath = data.value("StartScenePath", m_Data.StartScenePath);
        m_Data.StartSceneGuid = data.value("StartSceneGuid", m_Data.StartSceneGuid);
        m_Data.GameWidth = data.value("GameWidth", m_Data.GameWidth);
        m_Data.GameHeight = data.value("GameHeight", m_Data.GameHeight);
        m_Data.MoveForwardKey = data.value("MoveForwardKey", m_Data.MoveForwardKey);
        m_Data.MoveBackwardKey = data.value("MoveBackwardKey", m_Data.MoveBackwardKey);
        m_Data.MoveLeftKey = data.value("MoveLeftKey", m_Data.MoveLeftKey);
        m_Data.MoveRightKey = data.value("MoveRightKey", m_Data.MoveRightKey);
        m_Data.MoveUpKey = data.value("MoveUpKey", m_Data.MoveUpKey);
        m_Data.MoveDownKey = data.value("MoveDownKey", m_Data.MoveDownKey);
        m_Data.VisualStudioPath = data.value("VisualStudioPath", m_Data.VisualStudioPath);
        // 복구 인수로 실행한 경우에도 잘못된 API 필드 때문에 설정 창 진입을 막지 않는다.
        if (data.contains("EditorGraphicsAPI") && data["EditorGraphicsAPI"].is_string())
            m_Data.EditorGraphicsAPI = data["EditorGraphicsAPI"].get<std::string>();
        Normalize();

        ConsoleLog::Info("Project settings loaded: " + filepath);
        return true;
    }

    bool ProjectSettings::Save(const std::string& filepath) const
    {
        ProjectSettingsData safeData = NormalizedCopy(m_Data);

        nlohmann::json data;
        data["ProjectName"] = safeData.ProjectName;
        data["StartScenePath"] = safeData.StartScenePath;
        data["StartSceneGuid"] = safeData.StartSceneGuid;
        data["GameWidth"] = safeData.GameWidth;
        data["GameHeight"] = safeData.GameHeight;
        data["MoveForwardKey"] = safeData.MoveForwardKey;
        data["MoveBackwardKey"] = safeData.MoveBackwardKey;
        data["MoveLeftKey"] = safeData.MoveLeftKey;
        data["MoveRightKey"] = safeData.MoveRightKey;
        data["MoveUpKey"] = safeData.MoveUpKey;
        data["MoveDownKey"] = safeData.MoveDownKey;
        data["VisualStudioPath"] = safeData.VisualStudioPath;
        data["EditorGraphicsAPI"] = safeData.EditorGraphicsAPI;

        try
        {
            std::filesystem::path parent = std::filesystem::path(filepath).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
        }
        catch (...)
        {
            ConsoleLog::Error("Failed to prepare project settings directory: " + filepath);
            return false;
        }

        std::ofstream out(filepath);
        if (!out.is_open())
        {
            ConsoleLog::Error("Failed to save project settings: " + filepath);
            return false;
        }

        out << data.dump(4);
        out.flush();
        if (!out)
        {
            ConsoleLog::Error("Failed to write project settings: " + filepath);
            return false;
        }
        ConsoleLog::Info("Project settings saved: " + filepath);
        return true;
    }

    void ProjectSettings::Normalize()
    {
        // 설정 파일은 사용자가 직접 고칠 수도 있으므로, 엔진에서 쓰기 전에 항상 안전한 범위로 맞춘다.
        m_Data = NormalizedCopy(m_Data);
    }
}
