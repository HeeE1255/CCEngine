#pragma once

#include "Core.h"
#include "Core/Window.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    struct RuntimeManifestData
    {
        uint32_t Version = 1;
        std::string ProjectName = "CCEngine Game";
        std::string ContentRoot = ".";
        std::string AssetRoot = "assets";
        std::string AssetManifest = "RuntimeAssetManifest.json";
        std::string ShaderManifest = "ShaderBuildManifest.json";
        std::string GraphicsAPI = "DirectX11";
        std::vector<std::string> BackendShaderPackages;
        std::string StartScenePath;
        std::string StartSceneGuid;
        uint32_t GameWidth = 1920;
        uint32_t GameHeight = 1080;
        WindowMode PlayerWindowMode = WindowMode::Windowed;
        bool DevelopmentBuild = false;
    };

    class CC_API RuntimeManifest
    {
    public:
        static bool Load(const std::filesystem::path& path, RuntimeManifestData& data, std::string& error);
        static bool Save(const std::filesystem::path& path, const RuntimeManifestData& data, std::string& error);

        // Manifest 안 경로는 배포 폴더를 벗어날 수 없다. 잘못된 ../ 또는 절대 경로는
        // 빌드 산출물이 개발 PC의 파일을 우연히 읽는 문제로 이어지므로 시작 단계에서 차단한다.
        static bool ResolvePathInsideRoot(
            const std::filesystem::path& root,
            const std::filesystem::path& relativePath,
            std::filesystem::path& resolvedPath);
    };
}
