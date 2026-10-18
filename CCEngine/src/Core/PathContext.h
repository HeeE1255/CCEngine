#pragma once

#include "Core/PathUtils.h"

#include <filesystem>

namespace CCEngine
{
    class PathContext
    {
    public:
        static void ConfigureProject(const std::filesystem::path& projectRoot)
        {
            std::filesystem::path normalized;
            if (PathUtils::TryAbsoluteLexical(projectRoot, normalized))
                s_ProjectRoot = normalized;
        }

        static void ConfigureRuntime(
            const std::filesystem::path& runtimeRoot,
            const std::filesystem::path& contentRoot,
            const std::filesystem::path& assetRoot)
        {
            std::filesystem::path normalized;
            if (PathUtils::TryAbsoluteLexical(runtimeRoot, normalized))
                s_RuntimeRoot = normalized;
            if (PathUtils::TryAbsoluteLexical(contentRoot, normalized))
            {
                s_ContentRoot = normalized;
                s_ProjectRoot = normalized;
            }
            if (PathUtils::TryAbsoluteLexical(assetRoot, normalized))
                s_AssetRoot = normalized;
        }

        static const std::filesystem::path& ProjectRoot()
        {
            EnsureProjectRoot();
            return s_ProjectRoot;
        }

        static const std::filesystem::path& RuntimeRoot() { return s_RuntimeRoot; }
        static std::filesystem::path ContentRoot()
        {
            EnsureProjectRoot();
            return s_ContentRoot.empty() ? s_ProjectRoot : s_ContentRoot;
        }
        static std::filesystem::path AssetRoot()
        {
            EnsureProjectRoot();
            return s_AssetRoot.empty() ? s_ProjectRoot / "assets" : s_AssetRoot;
        }

        static std::filesystem::path ResolveProject(const std::filesystem::path& relative)
        {
            return relative.is_absolute() ? relative.lexically_normal() : (ProjectRoot() / relative).lexically_normal();
        }

    private:
        static void EnsureProjectRoot()
        {
            if (!s_ProjectRoot.empty())
                return;
            std::error_code ec;
            s_ProjectRoot = std::filesystem::current_path(ec).lexically_normal();
            if (ec)
                s_ProjectRoot.clear();
        }

        inline static std::filesystem::path s_ProjectRoot;
        inline static std::filesystem::path s_RuntimeRoot;
        inline static std::filesystem::path s_ContentRoot;
        inline static std::filesystem::path s_AssetRoot;
    };
}
