#pragma once

#include "Core.h"

#include <filesystem>
#include <string>
#include <vector>

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

    struct AssetReferenceIssue
    {
        std::filesystem::path SourceFile;
        std::string JsonLocation;
        std::string ReferenceKind;
        std::string Guid;
        std::string StoredPath;
        std::string ResolvedPath;
        std::string Message;
        bool Repaired = false;
    };

    struct AssetReferenceValidationReport
    {
        uint32_t FilesScanned = 0;
        uint32_t ReferencesChecked = 0;
        uint32_t RepairedReferences = 0;
        uint32_t MissingReferences = 0;
        std::vector<AssetReferenceIssue> Issues;
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
        static AssetReferenceValidationReport ValidateProjectReferences(const std::filesystem::path& rootDirectory = "assets", bool repairFiles = true);
        static AssetReferenceValidationReport ValidateKnownProjectReferences(const std::filesystem::path& rootDirectory = "assets", bool repairFiles = true, bool logCleanReport = false);

        static std::string AssetKindToString(AssetKind kind);
        static AssetKind AssetKindFromString(const std::string& text);
    };
}
