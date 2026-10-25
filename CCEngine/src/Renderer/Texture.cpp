#include "Renderer/Texture.h"
#include "Renderer/RendererAPI.h"

// 각 플랫폼별 백엔드 헤더 포함
#include "Platform/DirectX11/DX11Texture.h"

#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Texture.h"
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLTextureStorage.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanTextureStorage.h"
#endif
#include <iostream>
#include <stdexcept>

namespace CCEngine
{
    Texture2D* Texture2D::Create(const std::string& path)
    {
        if (path.empty())
            throw std::invalid_argument("Texture path must not be empty.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        // 엔진 사령탑에게 현재 선택된 그래픽스 API가 무엇인지 물어보고 분기 처리!
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            return new DX11Texture2D(path);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(path);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(path);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(path);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            throw std::invalid_argument("Texture dimensions must be greater than zero.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            return new DX11Texture2D(width, height);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(width, height);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(width, height);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(width, height);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, void* data)
    {
        if (width == 0 || height == 0 || !data)
            throw std::invalid_argument("Texture initial data and dimensions are required.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            // DX11 텍스처 생성자로 연결
            return new DX11Texture2D(width, height, data);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(DX12Context::Current(), width, height, data);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(OpenGLContext::Current(), width, height, data);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(VulkanContext::Current(), width, height, data);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }
}
