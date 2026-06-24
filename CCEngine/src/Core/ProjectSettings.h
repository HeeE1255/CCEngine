#pragma once
#include "Core.h"
#include <string>

namespace CCEngine
{
    struct ProjectSettingsData
    {
        std::string ProjectName = "CCEngine Project";
        std::string StartScenePath;
        uint32_t GameWidth = 1280;
        uint32_t GameHeight = 720;
    };

    class CC_API ProjectSettings
    {
    public:
        bool Load(const std::string& filepath = "project.ccproject");
        bool Save(const std::string& filepath = "project.ccproject") const;

        ProjectSettingsData& Data() { return m_Data; }
        const ProjectSettingsData& Data() const { return m_Data; }

    private:
        ProjectSettingsData m_Data;
    };
}
