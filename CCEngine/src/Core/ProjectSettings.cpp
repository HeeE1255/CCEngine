#include "Core/ProjectSettings.h"
#include "Core/ConsoleLog.h"
#include "json.hpp"
#include <fstream>

namespace CCEngine
{
    bool ProjectSettings::Load(const std::string& filepath)
    {
        std::ifstream in(filepath);
        if (!in.is_open())
            return false;

        nlohmann::json data;
        in >> data;

        m_Data.ProjectName = data.value("ProjectName", m_Data.ProjectName);
        m_Data.StartScenePath = data.value("StartScenePath", m_Data.StartScenePath);
        m_Data.GameWidth = data.value("GameWidth", m_Data.GameWidth);
        m_Data.GameHeight = data.value("GameHeight", m_Data.GameHeight);

        ConsoleLog::Info("Project settings loaded: " + filepath);
        return true;
    }

    bool ProjectSettings::Save(const std::string& filepath) const
    {
        nlohmann::json data;
        data["ProjectName"] = m_Data.ProjectName;
        data["StartScenePath"] = m_Data.StartScenePath;
        data["GameWidth"] = m_Data.GameWidth;
        data["GameHeight"] = m_Data.GameHeight;

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
}
