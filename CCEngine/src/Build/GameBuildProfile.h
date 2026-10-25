#pragma once

#include "Core.h"
#include "Core/Window.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    enum class GameBuildConfiguration
    {
        Development = 0,
        Release
    };

    enum class ManagedRuntimeDeployment
    {
        FrameworkDependent = 0,
        SelfContained
    };

    struct GameBuildProfileData
    {
        uint32_t Version = 1;
        std::string ProductName = "CCEngine Game";
        std::string ProductVersion = "0.1.0";
        std::string UpdateChannel = "Stable";
        std::string ExecutableName = "Game";
        std::string GraphicsAPI = "DirectX11";
        std::string ShaderCompilerScript;
        std::string PythonExecutable = "python.exe";
        std::string DXCExecutable = "dxc.exe";
        std::string SpirvCrossExecutable = "spirv-cross.exe";
        std::filesystem::path OutputDirectory = "Build/Windows";
        GameBuildConfiguration Configuration = GameBuildConfiguration::Development;
        ManagedRuntimeDeployment ManagedRuntime = ManagedRuntimeDeployment::SelfContained;
        WindowMode PlayerWindowMode = WindowMode::Windowed;
        bool IncludeDebugSymbols = true;
        bool ExcludeLicensedSampleAssets = true;
        bool VerifyPlayerFrame = true;
        bool AllowDevelopmentShaderFallback = true;
        std::vector<std::filesystem::path> AlwaysIncludeAssets;
    };

    class CC_API GameBuildProfile
    {
    public:
        bool Load(const std::filesystem::path& path, std::string& error);
        bool Save(const std::filesystem::path& path, std::string& error) const;
        void Normalize();

        GameBuildProfileData& Data() { return m_Data; }
        const GameBuildProfileData& Data() const { return m_Data; }

    private:
        GameBuildProfileData m_Data;
    };
}
