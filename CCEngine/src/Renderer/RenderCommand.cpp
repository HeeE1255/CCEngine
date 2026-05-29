#include "Renderer/RenderCommand.h"
#include "Platform/DirectX11/DX11RendererAPI.h"
#include <iostream>

namespace CCEngine
{
    std::unique_ptr<RendererAPI> RenderCommand::s_RendererAPI = nullptr;

    void RenderCommand::Init()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            s_RendererAPI = nullptr;
            break;

        case RendererAPI::API::DirectX11:
            s_RendererAPI = std::make_unique<DX11RendererAPI>();
            break;

        case RendererAPI::API::OpenGL:
            std::cout << "RendererAPI::OpenGL is currently not implemented!" << std::endl;
            s_RendererAPI = nullptr;
            break;

        case RendererAPI::API::Vulkan:
            std::cout << "RendererAPI::Vulkan is currently not implemented!" << std::endl;
            s_RendererAPI = nullptr;
            break;

        default:
            std::cout << "Unknown RendererAPI!" << std::endl;
            s_RendererAPI = nullptr;
            break;
        }
    }

    void RenderCommand::Shutdown()
    {
        s_RendererAPI.reset();
    }
}
