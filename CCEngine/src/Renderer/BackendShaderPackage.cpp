#include "BackendShaderPackage.h"
#if defined(CC_ENABLE_DX12_RHI) || defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
#include "json.hpp"
#include <fstream>
#include <stdexcept>
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Shader.h"
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLShader.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanShader.h"
#endif
namespace CCEngine
{
    namespace
    {
        struct Package { std::filesystem::path Vertex, Pixel; std::string VertexEntry, PixelEntry; };
        std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& relative)
        {
            const auto path = std::filesystem::path(relative);
            if (relative.empty() || path.is_absolute() || path.has_root_name()) throw std::runtime_error("Shader package path must be relative");
            const auto candidate = std::filesystem::canonical(root / path);
            const auto inside = candidate.lexically_relative(root);
            if (inside.empty() || *inside.begin() == ".." || !std::filesystem::is_regular_file(candidate))
                throw std::runtime_error("Shader package path escapes package directory");
            return candidate;
        }
        Package ReadPackage(const std::filesystem::path& manifest, const std::string& source, const char* backend)
        {
            std::ifstream file(manifest);
            if (!file) throw std::runtime_error("Cannot open backend shader manifest");
            const auto json = nlohmann::json::parse(file);
            if (json.at("schema").get<int>() != 1 || !json.at("success").get<bool>() ||
                json.at("bindingContract").get<std::string>() != "cc-hlsl-rhi-v1")
                throw std::runtime_error("Incomplete or incompatible backend shader package");
            const auto root = std::filesystem::canonical(manifest.parent_path());
            const nlohmann::json* selected = nullptr;
            for (const auto& shader : json.at("shaders"))
                if (shader.at("source").get<std::string>() == source)
                {
                    if (selected) throw std::runtime_error("Duplicate source in shader package");
                    selected = &shader;
                }
            if (!selected) throw std::runtime_error("Source not found in backend shader package: " + source);
            const auto& vertex = selected->at("stages").at("vertex").at("outputs").at(backend);
            const auto& pixel = selected->at("stages").at("pixel").at("outputs").at(backend);
            const auto directory = selected->at("directory").get<std::string>();
            Package result;
            result.Vertex = Resolve(root, directory + '/' + vertex.at("file").get<std::string>());
            result.Pixel = Resolve(root, directory + '/' + pixel.at("file").get<std::string>());
            result.VertexEntry = vertex.at("entry").get<std::string>();
            result.PixelEntry = pixel.at("entry").get<std::string>();
            return result;
        }
    }
#if defined(CC_ENABLE_DX12_RHI)
    std::unique_ptr<Shader> LoadDX12ShaderPackage(DX12Context& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "dx12");
        return DX12Shader::FromBytecodeFiles(context, package.Vertex, package.Pixel);
    }
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
    std::unique_ptr<Shader> LoadOpenGLShaderPackage(OpenGLContext& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "opengl");
        return OpenGLShader::FromFiles(context, package.Vertex, package.Pixel);
    }
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
    std::unique_ptr<Shader> LoadVulkanShaderPackage(VulkanContext& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "vulkan");
        return std::make_unique<VulkanShader>(context, package.Vertex, package.Pixel, package.VertexEntry, package.PixelEntry);
    }
#endif
}
#endif
