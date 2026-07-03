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
        std::string FileHash;
        uint32_t Version = 1;
    };

    class CC_API AssetDatabase
    {
    public:
        static void Scan(const std::filesystem::path& rootDirectory = "assets");
        static void ScanIfNeeded(const std::filesystem::path& rootDirectory = "assets");
        static void MarkDirty(const std::filesystem::path& rootDirectory = {});

        static std::string GetGuidFromPath(const std::filesystem::path& assetPath);
        static std::filesystem::path GetPathFromGuid(const std::string& guid);

        static AssetKind GetAssetKind(const std::filesystem::path& assetPath);
        static std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath);

        static bool EnsureMetaFile(const std::filesystem::path& assetPath);
        static bool MoveAsset(const std::filesystem::path& from, const std::filesystem::path& to);
        static bool RenameAsset(const std::filesystem::path& assetPath, const std::string& newName);
        static bool DeleteAsset(const std::filesystem::path& assetPath);
        static bool RecycleAsset(const std::filesystem::path& assetPath);
        static bool DeleteMetaFile(const std::filesystem::path& assetPath);

        static std::string AssetKindToString(AssetKind kind);
        static AssetKind AssetKindFromString(const std::string& text);
    };
}
