#pragma once
#include "Core.h"
#include <filesystem>
#include <string>
namespace CCEngine
{
    void VerifyBackendShaderHash(const std::filesystem::path& path, const std::string& expected);
    // GPU 객체를 만들지 않고 패키지 전체의 경로와 SHA-256을 검증한다.
    // 게시 전 검사와 손상 파일 QA가 같은 로더 규칙을 사용하도록 공개 진입점을 둔다.
    CC_API void ValidateBackendShaderManifest(const std::filesystem::path& manifest);
}
#if defined(CC_ENABLE_DX12_RHI) || defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/Shader.h"
#include <memory>
#include <vector>
namespace CCEngine
{
    // manifest는 CompileShaderBackends.py가 만든 run 폴더의 manifest.json이다.
    void ConfigurePortableShaderPackages(const std::string& backend,
        const std::vector<std::filesystem::path>& manifests, const std::filesystem::path& sourceRoot);
    void ClearPortableShaderPackages();
    void GetPortablePackageFiles(const std::string& backend, const std::filesystem::path& source,
        const std::vector<std::string>& keywords, std::filesystem::path& vertex, std::filesystem::path& pixel);
    std::unique_ptr<Shader> LoadRegisteredPortableShader(const std::string& backend,
        const std::filesystem::path& vertex, const std::filesystem::path& pixel);
    // source는 manifest에 기록된 source-root 기준 상대 경로다.
#if defined(CC_ENABLE_DX12_RHI)
    class DX12Context;
    // 패키지에 저장된 개발 PC의 절대 sourceRoot 대신 실행 시 지정한 root로 소스를 찾는다.
    void ConfigureDX12ShaderPackages(const std::vector<std::filesystem::path>& manifests,
        const std::filesystem::path& sourceRoot);
    void ClearDX12ShaderPackages();
    Shader::TextureBindings GetDX12TextureBindings(const std::filesystem::path& pixel);
    std::vector<VertexInput> GetDX12VertexInputs(const std::filesystem::path& vertex);
    std::string GetDX12MaterialPropertySource(const std::filesystem::path& pixel);
    void GetDX12PackageBytecode(const std::filesystem::path& source,
        const std::vector<std::string>& keywords, std::filesystem::path& vertex, std::filesystem::path& pixel);
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
