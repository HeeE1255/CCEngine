#include "Core/ProjectSettings.h"
#include "Core/ConsoleLog.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

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
        ConsoleLog::Info("Project settings saved: " + filepath);
        return true;
    }

    void ProjectSettings::Normalize()
    {
        // 설정 파일은 사용자가 직접 고칠 수도 있으므로, 엔진에서 쓰기 전에 항상 안전한 범위로 맞춘다.
        m_Data = NormalizedCopy(m_Data);
    }
}
