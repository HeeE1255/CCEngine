#pragma once

#include "Core/AssetDatabase.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    struct AssetDependency
    {
        std::filesystem::path SourcePath;
        std::filesystem::path RelativePath;
        std::string Guid;
        AssetKind Kind = AssetKind::Unknown;
        std::filesystem::path ReferencedBy;
        std::string JsonLocation;
    };

    struct AssetDependencyIssue
    {
        std::filesystem::path SourceFile;
        std::string JsonLocation;
        std::string Guid;
        std::string StoredPath;
        std::string Message;
    };

    struct AssetDependencyReport
    {
        bool Success = false;
        std::vector<AssetDependency> Assets;
        std::vector<AssetDependencyIssue> Issues;
    };

    class AssetDependencyCollector
    {
    public:
        AssetDependencyReport Collect(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& startScene,
            const std::vector<std::filesystem::path>& alwaysIncludeAssets = {}) const;
    };
}
