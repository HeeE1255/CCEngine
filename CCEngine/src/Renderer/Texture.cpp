#pragma once
#include "Renderer/Texture.h"
#include "Renderer/RendererAPI.h"

// 각 플랫폼별 백엔드 헤더 포함
#include "Platform/DirectX11/DX11Texture.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include <iostream>

namespace CCEngine
{
    Texture2D* Texture2D::Create(const std::string& path)
    {
        // 엔진 사령탑에게 현재 선택된 그래픽스 API가 무엇인지 물어보고 분기 처리!
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            return new DX11Texture2D(path);

        case RendererAPI::API::OpenGL:
            return new OpenGLTexture2D(path);

        case RendererAPI::API::Vulkan:
            return new VulkanTexture2D(path);
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            return new DX11Texture2D(width, height);

        case RendererAPI::API::OpenGL:
            return new OpenGLTexture2D(width, height);

        case RendererAPI::API::Vulkan:
            return new VulkanTexture2D(width, height);
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, void* data)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            std::cout << "RendererAPI::None is currently not supported!" << std::endl;
            return nullptr;

        case RendererAPI::API::DirectX11:
            // DX11 텍스처 생성자로 연결
            return new DX11Texture2D(width, height, data);

        case RendererAPI::API::OpenGL:
            // return new OpenGLTexture2D(width, height, data);
            return nullptr;

        case RendererAPI::API::Vulkan:
            // return new VulkanTexture2D(width, height, data);
            return nullptr;
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }
}