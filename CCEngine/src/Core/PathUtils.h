#pragma once

#include <filesystem>

namespace CCEngine::PathUtils
{
    inline bool TryAbsoluteLexical(
        const std::filesystem::path& path,
        std::filesystem::path& result)
    {
        if (path.empty())
            return false;

        std::error_code ec;
        result = std::filesystem::absolute(path, ec).lexically_normal();
        return !ec && !result.empty();
    }

    inline bool TryMakeRelativeInside(
        const std::filesystem::path& child,
        const std::filesystem::path& root,
        std::filesystem::path& relative,
        bool allowRoot = false)
    {
        std::filesystem::path absoluteChild;
        std::filesystem::path absoluteRoot;
        if (!TryAbsoluteLexical(child, absoluteChild) || !TryAbsoluteLexical(root, absoluteRoot))
            return false;

        relative = absoluteChild.lexically_relative(absoluteRoot);
        if (relative.empty())
        {
            if (!allowRoot || absoluteChild != absoluteRoot)
                return false;
            relative = ".";
        }
        if (relative.is_absolute())
            return false;

        // 경로 보안 검사는 파일 존재 여부와 분리해야 한다. lexical 비교만 사용하면
        // 아직 생성되지 않은 staging 파일도 같은 규칙으로 안전하게 판정할 수 있다.
        for (const auto& part : relative)
        {
            if (part == "..")
                return false;
        }
        return true;
    }

    inline bool TryResolveInside(
        const std::filesystem::path& root,
        const std::filesystem::path& relativePath,
        std::filesystem::path& resolved,
        bool allowRoot = true)
    {
        if (root.empty() || relativePath.empty() || relativePath.is_absolute())
            return false;

        std::filesystem::path absoluteRoot;
        if (!TryAbsoluteLexical(root, absoluteRoot))
            return false;

        const std::filesystem::path candidate = (absoluteRoot / relativePath).lexically_normal();
        std::filesystem::path relation;
        if (!TryMakeRelativeInside(candidate, absoluteRoot, relation, allowRoot))
            return false;

        resolved = candidate;
        return true;
    }
}
