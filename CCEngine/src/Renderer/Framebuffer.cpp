#include "Renderer/Framebuffer.h"
#include "Renderer/RendererAPI.h"

#include "Platform/DirectX11/DX11Framebuffer.h" 
// #include "Platform/OpenGL/OpenGLFramebuffer.h" // 나중에 만들면 주석 해제
// #include "Platform/Vulkan/VulkanFramebuffer.h"

namespace CCEngine {

    Framebuffer* Framebuffer::Create(const FramebufferSpecification& spec)
    {
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::API::None:
                return nullptr;

            case RendererAPI::API::DirectX11:
                return new DX11Framebuffer(spec); // 현재는 DX11만 작동!

            case RendererAPI::API::OpenGL:
                // return new OpenGLFramebuffer(spec);
                return nullptr;

            case RendererAPI::API::Vulkan:
                // return new VulkanFramebuffer(spec);
                return nullptr;
        }

        // assert(false && "Unknown RendererAPI!");
        return nullptr;
    }

}