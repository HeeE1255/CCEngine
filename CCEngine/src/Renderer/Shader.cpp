#include "Renderer/Shader.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/BackendShaderPackage.h"
#include <set>
#include <memory>
#include "Platform/DirectX11/DX11Shader.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Shader.h"
#endif
#include <stdexcept>
#include <iostream>

namespace CCEngine
{
    void Shader::SetTextureBindings(TextureBindings bindings)
    {
        std::set<uint32_t> slots;
        for (const auto& [name, slot] : bindings)
            if (name.empty() || slot >= 32 || !slots.insert(slot).second)
                throw std::runtime_error("Invalid or overlapping shader texture binding: " + name);
        m_TextureBindings = std::move(bindings);
    }
    Shader* Shader::Create(const std::string& filepath)
    {
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
#if defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL || RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            const auto result = ShaderCompiler::CompileHlslFile(filepath, false);
            if (!result.Success) throw std::runtime_error(result.Summary);
            return CreateFromBytecode(result.Vertex.BytecodePath, result.Pixel.BytecodePath);
        }
#endif
#if defined(CC_ENABLE_DX12_RHI)
        if (RendererAPI::GetAPI() == RendererAPI::API::DirectX12)
        {
            // 기본 셰이더도 머티리얼과 동일한 패키지 경로를 사용한다. DX11 CSO로 후퇴하지 않는다.
            const auto result = ShaderCompiler::CompileHlslFile(filepath, false);
            if (!result.Success) throw std::runtime_error(result.Summary);
            return CreateFromBytecode(result.Vertex.BytecodePath, result.Pixel.BytecodePath);
        }
#endif
        std::filesystem::path vertexBytecode;
        std::filesystem::path pixelBytecode;
        if (ShaderCompiler::TryGetRuntimeBytecode(filepath, vertexBytecode, pixelBytecode))
            return CreateFromBytecode(vertexBytecode, pixelBytecode);

        // Release Player는 패키지에 검증된 bytecode만 싣는다.
        // 여기서 원본 HLSL 컴파일로 후퇴하면 배포 PC마다 결과가 달라지고 누락 에셋도 늦게 발견된다.
        if (ShaderCompiler::IsRuntimeBytecodeOnly())
        {
            std::cout << "Packaged shader bytecode is missing: " << filepath << std::endl;
            return nullptr;
        }

        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            return new DX11Shader(filepath);

        case RendererAPI::API::OpenGL:
            std::cout << "OpenGL shader backend is currently not implemented: " << filepath << std::endl;
            return nullptr;

        case RendererAPI::API::Vulkan:
            std::cout << "Vulkan shader backend is currently not implemented: " << filepath << std::endl;
            return nullptr;
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Shader* Shader::CreateFromBytecode(const std::filesystem::path& vertexBytecode, const std::filesystem::path& pixelBytecode)
    {
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
        {
            auto shader = std::make_unique<DX11Shader>(vertexBytecode, pixelBytecode);
            shader->SetMaterialPropertySource(ShaderCompiler::GetRuntimeMaterialPropertySource(pixelBytecode));
            return shader.release();
        }
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12:
        {
            auto shader = DX12Shader::FromBytecodeFiles(DX12Context::Current(), vertexBytecode, pixelBytecode);
            shader->SetTextureBindings(GetDX12TextureBindings(pixelBytecode));
            shader->SetVertexInputs(GetDX12VertexInputs(vertexBytecode));
            shader->SetMaterialPropertySource(GetDX12MaterialPropertySource(pixelBytecode));
            return shader.release();
        }
#endif

        case RendererAPI::API::OpenGL:
#if defined(CC_ENABLE_OPENGL_RHI)
            return LoadRegisteredPortableShader("opengl", vertexBytecode, pixelBytecode).release();
#else
            throw std::runtime_error("OpenGL shader backend is not compiled.");
#endif

        case RendererAPI::API::Vulkan:
#if defined(CC_ENABLE_VULKAN_RHI)
            return LoadRegisteredPortableShader("vulkan", vertexBytecode, pixelBytecode).release();
#else
            throw std::runtime_error("Vulkan shader backend is not compiled.");
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }
}
