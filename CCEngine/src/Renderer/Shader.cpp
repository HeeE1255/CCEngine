#include "Renderer/Shader.h"
#include "Renderer/RendererAPI.h"
#include "Platform/DirectX11/DX11Shader.h"
#include <iostream>

namespace CCEngine
{
    Shader* Shader::Create(const std::string& filepath)
    {
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
}
