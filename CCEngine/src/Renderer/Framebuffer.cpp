#include "Renderer/Framebuffer.h"
#include "Renderer/RendererAPI.h"

#include "Platform/DirectX11/DX11Framebuffer.h" 
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLFramebufferStorage.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanFramebuffer.h"
#endif
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Framebuffer.h"
#endif
#include <stdexcept>

namespace CCEngine {

    Framebuffer* Framebuffer::Create(const FramebufferSpecification& spec)
    {
        if (spec.Width == 0 || spec.Height == 0)
            throw std::invalid_argument("Framebuffer dimensions must be greater than zero.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::API::None:
                return nullptr;

            case RendererAPI::API::DirectX11:
                return new DX11Framebuffer(spec);
#if defined(CC_ENABLE_DX12_RHI)
            case RendererAPI::API::DirectX12: return new DX12Framebuffer(spec);
#endif

#if defined(CC_ENABLE_OPENGL_RHI)
            case RendererAPI::API::OpenGL: return new OpenGLFramebuffer(spec);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
            case RendererAPI::API::Vulkan: return new VulkanFramebuffer(spec);
#endif
        }

        // assert(false && "Unknown RendererAPI!");
        return nullptr;
    }

}
