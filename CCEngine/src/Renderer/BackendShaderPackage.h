#pragma once
#if defined(CC_ENABLE_DX12_RHI) || defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/Shader.h"
#include <memory>
namespace CCEngine
{
    // manifest는 CompileShaderBackends.py가 만든 run 폴더의 manifest.json이다.
    // source는 manifest에 기록된 source-root 기준 상대 경로다. 기존 Shader 팩토리는 변경하지 않는다.
#if defined(CC_ENABLE_DX12_RHI)
    class DX12Context;
    std::unique_ptr<Shader> LoadDX12ShaderPackage(DX12Context& context, const std::filesystem::path& manifest, const std::string& source);
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
    class OpenGLContext;
    std::unique_ptr<Shader> LoadOpenGLShaderPackage(OpenGLContext& context, const std::filesystem::path& manifest, const std::string& source);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
    class VulkanContext;
    std::unique_ptr<Shader> LoadVulkanShaderPackage(VulkanContext& context, const std::filesystem::path& manifest, const std::string& source);
#endif
}
#endif
