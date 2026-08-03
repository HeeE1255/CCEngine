#pragma once
#include "Core.h"
#include <string>

namespace CCEngine
{
    struct ProjectSettingsData
    {
        std::string ProjectName = "CCEngine Project";
        std::string StartScenePath;
        std::string StartSceneGuid;
        uint32_t GameWidth = 1920;
        uint32_t GameHeight = 1080;
        std::string MoveForwardKey = "W";
        std::string MoveBackwardKey = "S";
        std::string MoveLeftKey = "A";
        std::string MoveRightKey = "D";
        std::string MoveUpKey = "E";
        std::string MoveDownKey = "Q";
    };

    class CC_API ProjectSettings
    {
    public:
        bool Load(const std::string& filepath = "project.ccproject");
        bool Save(const std::string& filepath = "project.ccproject") const;
        void Normalize();

        ProjectSettingsData& Data() { return m_Data; }
        const ProjectSettingsData& Data() const { return m_Data; }

    private:
        ProjectSettingsData m_Data;
    };
}
