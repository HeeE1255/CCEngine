#pragma once

#include "Core.h"

#include <filesystem>
#include <string>

namespace CCEngine
{
    enum class AssetKind
    {
        Unknown = 0,
        Scene,
        Prefab,
        Model,
        Texture
    };

    struct AssetMetadata
    {
        std::string Guid;
        AssetKind Type = AssetKind::Unknown;
        std::filesystem::path SourcePath;
        std::string Importer;
        uint32_t Version = 1;
    };

    class CC_API AssetDatabase
    {
    public:
        static void Scan(const std::filesystem::path& rootDirectory = "assets");

        static std::string GetGuidFromPath(const std::filesystem::path& assetPath);
        static std::filesystem::path GetPathFromGuid(const std::string& guid);

        static AssetKind GetAssetKind(const std::filesystem::path& assetPath);
        static std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath);

        static bool EnsureMetaFile(const std::filesystem::path& assetPath);
        static bool DeleteMetaFile(const std::filesystem::path& assetPath);

        static std::string AssetKindToString(AssetKind kind);
        static AssetKind AssetKindFromString(const std::string& text);
    };
}
