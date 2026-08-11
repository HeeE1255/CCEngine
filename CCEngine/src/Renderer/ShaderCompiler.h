#pragma once

#include "Core.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    enum class ShaderStage
    {
        Vertex = 0,
        Pixel
    };

    struct CC_API ShaderCompileStageResult
    {
        bool Success = false;
        bool UsedCache = false;
        std::filesystem::path BytecodePath;
        std::string EntryPoint;
        std::string TargetProfile;
        std::string Message;
    };

    struct CC_API ShaderCompileResult
    {
        bool Success = false;
        std::filesystem::path SourcePath;
        std::filesystem::path CacheDirectory;
        ShaderCompileStageResult Vertex;
        ShaderCompileStageResult Pixel;
        std::vector<std::string> ReflectionErrors;
        std::vector<std::string> ReflectionWarnings;
        std::string Summary;
    };

    struct CC_API ShaderCacheStatus
    {
        bool IsHlsl = false;
        bool SourceExists = false;
        bool VertexBytecodeExists = false;
        bool PixelBytecodeExists = false;
        bool VertexBytecodeFresh = false;
        bool PixelBytecodeFresh = false;
        std::string Message;
    };

    class CC_API ShaderCompiler
    {
    public:
        static bool IsHlslSource(const std::filesystem::path& path);
        static ShaderCacheStatus GetHlslCacheStatus(const std::filesystem::path& sourcePath);
        static bool GetLastCompileResult(const std::filesystem::path& sourcePath, ShaderCompileResult& outResult);
        static ShaderCompileResult CompileHlslFile(const std::filesystem::path& sourcePath, bool forceRecompile);

    private:
        static ShaderCompileStageResult CompileStage(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& bytecodePath,
            ShaderStage stage,
            const std::string& entryPoint,
            const std::string& targetProfile,
            bool forceRecompile);
    };
}
