#include "Runtime/RuntimeManifest.h"

#include "Core/PathUtils.h"
#include "Renderer/RendererAPI.h"
#include "json.hpp"

#include <algorithm>
#include <fstream>

namespace CCEngine
{
    namespace
    {
        constexpr uint32_t MinGameResolution = 320;
        constexpr uint32_t MaxGameResolution = 8192;

        const char* WindowModeName(WindowMode mode)
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

        void Normalize(RuntimeManifestData& data)
        {
            if (data.ProjectName.empty())
                data.ProjectName = "CCEngine Game";
            if (data.ContentRoot.empty())
                data.ContentRoot = ".";
            if (data.AssetRoot.empty())
                data.AssetRoot = "assets";
            if (data.AssetManifest.empty())
                data.AssetManifest = "RuntimeAssetManifest.json";
            if (data.ShaderManifest.empty())
                data.ShaderManifest = "ShaderBuildManifest.json";

            data.GameWidth = (std::clamp)(data.GameWidth, MinGameResolution, MaxGameResolution);
            data.GameHeight = (std::clamp)(data.GameHeight, MinGameResolution, MaxGameResolution);
            if (data.PlayerWindowMode != WindowMode::Windowed &&
                data.PlayerWindowMode != WindowMode::Borderless &&
                data.PlayerWindowMode != WindowMode::Fullscreen)
                data.PlayerWindowMode = WindowMode::Windowed;
        }
    }

    bool RuntimeManifest::Load(const std::filesystem::path& path, RuntimeManifestData& data, std::string& error)
    {
        std::ifstream input(path);
        if (!input.is_open())
        {
            error = "Runtime manifest not found: " + path.string();
            return false;
        }

        try
        {
            nlohmann::json json;
            input >> json;

            data.Version = json.value("Version", data.Version);
            data.ProjectName = json.value("ProjectName", data.ProjectName);
            data.ContentRoot = json.value("ContentRoot", data.ContentRoot);
            data.AssetRoot = json.value("AssetRoot", data.AssetRoot);
            data.AssetManifest = json.value("AssetManifest", data.AssetManifest);
            data.ShaderManifest = json.value("ShaderManifest", data.ShaderManifest);
            data.GraphicsAPI = json.value("GraphicsAPI", std::string("DirectX11"));
            RendererAPI::ParseAPIName(data.GraphicsAPI);
            data.BackendShaderPackages = json.value("BackendShaderPackages", std::vector<std::string>{});
            data.StartScenePath = json.value("StartScenePath", data.StartScenePath);
            data.StartSceneGuid = json.value("StartSceneGuid", data.StartSceneGuid);
            data.GameWidth = json.value("GameWidth", data.GameWidth);
            data.GameHeight = json.value("GameHeight", data.GameHeight);
            data.PlayerWindowMode = ParseWindowMode(json.value("WindowMode", std::string("Windowed")));
            data.DevelopmentBuild = json.value("DevelopmentBuild", data.DevelopmentBuild);
        }
        catch (const std::exception& exception)
        {
            error = "Runtime manifest parse failed: " + std::string(exception.what());
            return false;
        }

        Normalize(data);
        if (data.Version != 1 && data.Version != 2)
        {
            error = "Unsupported runtime manifest version: " + std::to_string(data.Version);
            return false;
        }
        if (data.StartScenePath.empty() && data.StartSceneGuid.empty())
        {
            error = "Runtime manifest has no start scene.";
            return false;
        }
        if ((data.Version == 1 && data.GraphicsAPI != "DirectX11") ||
            (data.GraphicsAPI != "DirectX11" && data.BackendShaderPackages.empty()) ||
            (data.GraphicsAPI == "DirectX11" && !data.BackendShaderPackages.empty()))
        {
            error = "Runtime graphics API, manifest version and shader packages do not match.";
            return false;
        }

        error.clear();
        return true;
    }

    bool RuntimeManifest::Save(const std::filesystem::path& path, const RuntimeManifestData& source, std::string& error)
    {
        RuntimeManifestData data = source;
        Normalize(data);

        try { RendererAPI::ParseAPIName(data.GraphicsAPI); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
        if ((data.GraphicsAPI != "DirectX11" && data.BackendShaderPackages.empty()) ||
            (data.GraphicsAPI == "DirectX11" && !data.BackendShaderPackages.empty()))
        {
            error = "Graphics API and backend shader packages do not match.";
            return false;
        }

        nlohmann::json json;
        json["Version"] = data.GraphicsAPI == "DirectX11" ? 1 : 2;
        json["GraphicsAPI"] = data.GraphicsAPI;
        json["BackendShaderPackages"] = data.BackendShaderPackages;
        json["ProjectName"] = data.ProjectName;
        json["ContentRoot"] = data.ContentRoot;
        json["AssetRoot"] = data.AssetRoot;
        json["AssetManifest"] = data.AssetManifest;
        json["ShaderManifest"] = data.ShaderManifest;
        json["StartScenePath"] = data.StartScenePath;
        json["StartSceneGuid"] = data.StartSceneGuid;
        json["GameWidth"] = data.GameWidth;
        json["GameHeight"] = data.GameHeight;
        json["WindowMode"] = WindowModeName(data.PlayerWindowMode);
        json["DevelopmentBuild"] = data.DevelopmentBuild;

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Failed to create runtime manifest directory: " + ec.message();
            return false;
        }

        std::ofstream output(path);
        if (!output.is_open())
        {
            error = "Failed to write runtime manifest: " + path.string();
            return false;
        }

        output << json.dump(4);
        output.flush();
        if (!output)
        {
            error = "Failed to flush runtime manifest: " + path.string();
            return false;
        }
        error.clear();
        return true;
    }

    bool RuntimeManifest::ResolvePathInsideRoot(
        const std::filesystem::path& root,
        const std::filesystem::path& relativePath,
        std::filesystem::path& resolvedPath)
    {
        return PathUtils::TryResolveInside(root, relativePath, resolvedPath, true);
    }
}
