#include "Renderer/Shader.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/ShaderCompiler.h"
#include "Platform/DirectX11/DX11Shader.h"
#include <iostream>

namespace CCEngine
{
    Shader* Shader::Create(const std::string& filepath)
    {
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
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
            return new DX11Shader(vertexBytecode, pixelBytecode);

        case RendererAPI::API::OpenGL:
            std::cout << "OpenGL bytecode shader backend is currently not implemented." << std::endl;
            return nullptr;

        case RendererAPI::API::Vulkan:
            std::cout << "Vulkan bytecode shader backend is currently not implemented." << std::endl;
            return nullptr;
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }
}
