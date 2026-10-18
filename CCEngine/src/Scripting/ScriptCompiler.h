#pragma once

#include "Core.h"

#include <filesystem>
#include <string>

namespace CCEngine
{
    struct CC_API ScriptBuildResult
    {
        bool Success = false;
        int ExitCode = -1;
        std::string Output;
    };

    class CC_API ScriptCompiler
    {
    public:
        static void RequestCompile();
        static bool Update();
        static bool IsCompiling();
        static ScriptBuildResult CompileForBuild(const std::filesystem::path& projectRoot = {});
    };
}
