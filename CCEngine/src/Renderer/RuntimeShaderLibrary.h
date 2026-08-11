#pragma once

#include "Core.h"
#include "Renderer/Shader.h"

#include <filesystem>
#include <memory>
#include <string>

namespace CCEngine
{
    struct MaterialAsset;

    struct CC_API RuntimeShaderStatus
    {
        bool HasEntry = false;
        bool Failed = false;
        bool UsingErrorShader = false;
        std::string Message;
    };

    class CC_API RuntimeShaderLibrary
    {
    public:
        static std::shared_ptr<Shader> GetShaderForMaterial(const MaterialAsset& material);
        static std::shared_ptr<Shader> GetErrorShader();
        static RuntimeShaderStatus GetStatusForShader(const std::filesystem::path& shaderPath);
        static RuntimeShaderStatus GetStatusForMaterial(const MaterialAsset& material);
        static void Invalidate(const std::filesystem::path& shaderPath);
        static void Clear();
    };
}
