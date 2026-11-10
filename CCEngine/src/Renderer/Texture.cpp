#include "Renderer/Texture.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/TextureMipChain.h"
#include "stb_image.h"
#include <memory>

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
    void Texture2D::UploadMipChain(const TextureMipChain&)
    {
        throw std::logic_error("Mip upload is not supported by this texture implementation");
    }

    bool Texture2D::UpdateMipData(uint32_t width, uint32_t height, const void* pixels, size_t bytes)
    {
        if (!m_CreateOptions.GenerateMips) return false;
        const auto chain = TextureMipChain::GenerateRGBA8(width, height, pixels, bytes,
            { m_CreateOptions.ColorSpace, m_CreateOptions.Content, m_CreateOptions.MaxMipBytes });
        UploadMipChain(chain);
        m_MipLevelCount = static_cast<uint32_t>(chain.Levels.size());
        return true;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, void* data, const TextureCreateOptions& options)
    {
        // 생성과 재업로드가 같은 축소 규칙을 사용한다. 기존 호출은 단일 mip 동작을 유지한다.
        TextureMipChain chain;
        if (options.GenerateMips)
            chain = TextureMipChain::GenerateRGBA8(width, height, data, uint64_t(width) * height * 4,
                { options.ColorSpace, options.Content, options.MaxMipBytes });
        std::unique_ptr<Texture2D> texture(Create(width, height, data, options.ColorSpace));
        if (!texture) throw std::runtime_error("Texture creation failed");
        texture->m_CreateOptions = options;
        if (options.GenerateMips)
        {
            texture->UploadMipChain(chain);
            texture->m_MipLevelCount = static_cast<uint32_t>(chain.Levels.size());
        }
        return texture.release();
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, const TextureCreateOptions& options)
    {
        if (!options.GenerateMips) return Create(width, height, options.ColorSpace);
        const uint64_t bytes = uint64_t(width) * height * 4;
        if (!width || !height || width > 16384 || height > 16384 || bytes > options.MaxMipBytes)
            throw std::invalid_argument("Invalid mip texture size or budget");
        std::vector<uint8_t> zero(static_cast<size_t>(bytes), 0);
        return Create(width, height, zero.data(), options);
    }

    Texture2D* Texture2D::Create(const std::string& path, const TextureCreateOptions& options)
    {
        if (!options.GenerateMips) return Create(path, options.ColorSpace);
        int width = 0, height = 0, channels = 0;
        if (!stbi_info(path.c_str(), &width, &height, &channels) || width <= 0 || height <= 0 ||
            width > 16384 || height > 16384 || uint64_t(width) * height * 4 > options.MaxMipBytes)
            throw std::invalid_argument("Invalid mip texture file or budget: " + path);
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!pixels) throw std::runtime_error("Cannot decode mip texture: " + path);
        std::unique_ptr<Texture2D> texture(Create(static_cast<uint32_t>(width), static_cast<uint32_t>(height), pixels.get(), options));
        texture->m_SourcePath = path;
        return texture.release();
    }

    Texture2D* Texture2D::Create(const std::string& path, TextureColorSpace colorSpace)
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
            return new DX11Texture2D(path, colorSpace);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(path, colorSpace);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(path, colorSpace);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(path, colorSpace);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, TextureColorSpace colorSpace)
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
            return new DX11Texture2D(width, height, colorSpace);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(width, height, colorSpace);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(width, height, colorSpace);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(width, height, colorSpace);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }

    Texture2D* Texture2D::Create(uint32_t width, uint32_t height, void* data, TextureColorSpace colorSpace)
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
            return new DX11Texture2D(width, height, data, colorSpace);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12Texture2D(DX12Context::Current(), width, height, data, colorSpace);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLTexture2D(OpenGLContext::Current(), width, height, data, colorSpace);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanTexture2D(VulkanContext::Current(), width, height, data, colorSpace);
#endif
        }

        std::cout << "Unknown RendererAPI!" << std::endl;
        return nullptr;
    }
}
