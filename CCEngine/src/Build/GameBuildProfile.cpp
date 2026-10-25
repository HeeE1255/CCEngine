#include "Build/GameBuildProfile.h"

#include "json.hpp"
#include "Renderer/RendererAPI.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace CCEngine
{
    namespace
    {
        std::string ConfigurationName(GameBuildConfiguration configuration)
        {
            return configuration == GameBuildConfiguration::Release ? "Release" : "Development";
        }

        std::string RuntimeName(ManagedRuntimeDeployment deployment)
        {
            return deployment == ManagedRuntimeDeployment::SelfContained ? "SelfContained" : "FrameworkDependent";
        }

        std::string WindowModeName(WindowMode mode)
        {
            if (mode == WindowMode::Borderless)
                return "Borderless";
            if (mode == WindowMode::Fullscreen)
                return "Fullscreen";
            return "Windowed";
        }

        WindowMode ParseWindowMode(const std::string& value)
        {
            if (value == "Borderless")
                return WindowMode::Borderless;
            if (value == "Fullscreen")
                return WindowMode::Fullscreen;
            return WindowMode::Windowed;
        }

        std::string SanitizeExecutableName(std::string name)
        {
            constexpr const char* invalid = "<>:\"/\\|?*";
            name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char character)
            {
                return character < 32;
            }), name.end());
            for (char& character : name)
            {
                if (std::strchr(invalid, character))
                    character = '_';
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
                name.pop_back();
            return name.empty() ? "Game" : name;
        }

        std::string SanitizeReleaseLabel(std::string value, const char* fallback)
        {
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character)
            {
                return !std::isalnum(character) && character != '.' && character != '-' && character != '_';
            }), value.end());
            return value.empty() ? fallback : value;
        }

        bool IsSafeRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute())
                return false;
            for (const auto& part : path.lexically_normal())
            {
                if (part == "..")
                    return false;
            }
            return true;
        }
    }

    void GameBuildProfile::Normalize()
    {
        if (m_Data.ProductName.empty())
            m_Data.ProductName = "CCEngine Game";
        m_Data.ProductVersion = SanitizeReleaseLabel(m_Data.ProductVersion, "0.1.0");
        m_Data.UpdateChannel = SanitizeReleaseLabel(m_Data.UpdateChannel, "Stable");
        m_Data.ExecutableName = SanitizeExecutableName(m_Data.ExecutableName);
        if (!IsSafeRelativePath(m_Data.OutputDirectory))
            m_Data.OutputDirectory = "Build/Windows";
        if (m_Data.PlayerWindowMode != WindowMode::Windowed &&
            m_Data.PlayerWindowMode != WindowMode::Borderless &&
            m_Data.PlayerWindowMode != WindowMode::Fullscreen)
            m_Data.PlayerWindowMode = WindowMode::Windowed;

        std::vector<std::filesystem::path> normalized;
        for (const auto& path : m_Data.AlwaysIncludeAssets)
        {
            const auto value = path.lexically_normal();
            if (IsSafeRelativePath(value) && std::find(normalized.begin(), normalized.end(), value) == normalized.end())
                normalized.push_back(value);
        }
        m_Data.AlwaysIncludeAssets = std::move(normalized);
    }

    bool GameBuildProfile::Load(const std::filesystem::path& path, std::string& error)
    {
        std::ifstream input(path);
        if (!input.is_open())
        {
            Normalize();
            error = "Build profile not found: " + path.string();
            return false;
        }

        try
        {
            nlohmann::json json;
            input >> json;
            m_Data.Version = json.value("Version", 1u);
            m_Data.ProductName = json.value("ProductName", m_Data.ProductName);
            m_Data.ProductVersion = json.value("ProductVersion", m_Data.ProductVersion);
            m_Data.UpdateChannel = json.value("UpdateChannel", m_Data.UpdateChannel);
            m_Data.ExecutableName = json.value("ExecutableName", m_Data.ExecutableName);
            m_Data.GraphicsAPI = json.value("GraphicsAPI", std::string("DirectX11"));
            RendererAPI::ParseAPIName(m_Data.GraphicsAPI);
            m_Data.ShaderCompilerScript = json.value("ShaderCompilerScript", std::string());
            m_Data.PythonExecutable = json.value("PythonExecutable", std::string("python.exe"));
            m_Data.DXCExecutable = json.value("DXCExecutable", std::string("dxc.exe"));
            m_Data.SpirvCrossExecutable = json.value("SpirvCrossExecutable", std::string("spirv-cross.exe"));
            m_Data.OutputDirectory = json.value("OutputDirectory", m_Data.OutputDirectory.generic_string());
            m_Data.Configuration = json.value("Configuration", std::string("Development")) == "Release"
                ? GameBuildConfiguration::Release : GameBuildConfiguration::Development;
            m_Data.ManagedRuntime = json.value("ManagedRuntime", std::string("SelfContained")) == "FrameworkDependent"
                ? ManagedRuntimeDeployment::FrameworkDependent : ManagedRuntimeDeployment::SelfContained;
            m_Data.PlayerWindowMode = ParseWindowMode(json.value("WindowMode", std::string("Windowed")));
            m_Data.IncludeDebugSymbols = json.value("IncludeDebugSymbols", true);
            m_Data.ExcludeLicensedSampleAssets = json.value("ExcludeLicensedSampleAssets", true);
            m_Data.VerifyPlayerFrame = json.value("VerifyPlayerFrame", true);
            m_Data.AllowDevelopmentShaderFallback = json.value("AllowDevelopmentShaderFallback", true);
            m_Data.AlwaysIncludeAssets.clear();
            if (json.contains("AlwaysIncludeAssets") && json["AlwaysIncludeAssets"].is_array())
            {
                for (const auto& asset : json["AlwaysIncludeAssets"])
                {
                    if (asset.is_string())
                        m_Data.AlwaysIncludeAssets.emplace_back(asset.get<std::string>());
                }
            }
        }
        catch (const std::exception& exception)
        {
            error = "Build profile parse failed: " + std::string(exception.what());
            return false;
        }

        if (m_Data.Version != 1)
        {
            error = "Unsupported build profile version: " + std::to_string(m_Data.Version);
            return false;
        }
        Normalize();
        error.clear();
        return true;
    }

    bool GameBuildProfile::Save(const std::filesystem::path& path, std::string& error) const
    {
        GameBuildProfile normalized = *this;
        normalized.Normalize();
        const auto& data = normalized.Data();

        nlohmann::json json;
        json["Version"] = data.Version;
        json["ProductName"] = data.ProductName;
        json["ProductVersion"] = data.ProductVersion;
        json["UpdateChannel"] = data.UpdateChannel;
        json["ExecutableName"] = data.ExecutableName;
        try { RendererAPI::ParseAPIName(data.GraphicsAPI); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
        json["GraphicsAPI"] = data.GraphicsAPI;
        json["ShaderCompilerScript"] = data.ShaderCompilerScript;
        json["PythonExecutable"] = data.PythonExecutable;
        json["DXCExecutable"] = data.DXCExecutable;
        json["SpirvCrossExecutable"] = data.SpirvCrossExecutable;
        json["OutputDirectory"] = data.OutputDirectory.generic_string();
        json["Configuration"] = ConfigurationName(data.Configuration);
        json["ManagedRuntime"] = RuntimeName(data.ManagedRuntime);
        json["WindowMode"] = WindowModeName(data.PlayerWindowMode);
        json["IncludeDebugSymbols"] = data.IncludeDebugSymbols;
        json["ExcludeLicensedSampleAssets"] = data.ExcludeLicensedSampleAssets;
        json["VerifyPlayerFrame"] = data.VerifyPlayerFrame;
        json["AllowDevelopmentShaderFallback"] = data.AllowDevelopmentShaderFallback;
        json["AlwaysIncludeAssets"] = nlohmann::json::array();
        for (const auto& asset : data.AlwaysIncludeAssets)
            json["AlwaysIncludeAssets"].push_back(asset.generic_string());

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Failed to create build profile directory: " + ec.message();
            return false;
        }
        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open())
        {
            error = "Failed to save build profile: " + path.string();
            return false;
        }
        output << json.dump(4);
        error.clear();
        return true;
    }
}
