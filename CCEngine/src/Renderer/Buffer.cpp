#include "Renderer/Buffer.h"
#include "Renderer/RendererAPI.h"
#include "Platform/DirectX11/DX11Buffer.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Buffer.h"
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLBuffer.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanBuffer.h"
#endif

// 버퍼 팩토리 구현: RendererAPI에서 현재 API를 물어보고, 그에 맞는 버퍼 객체를 생성해서 반환하는 역할
namespace CCEngine
{
    // ==========================================
    // Vertex Buffer 팩토리
    // ==========================================
    VertexBuffer* VertexBuffer::Create(void* vertices, uint32_t size)
    {
        if (!vertices || size == 0)
            throw std::invalid_argument("Vertex buffer requires nonempty initial data.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11VertexBuffer(vertices, size);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12VertexBuffer(DX12Context::Current(), size, vertices);
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
        // 생성 시 현재 창의 공유 장치를 사용한다. 이후 다른 창에서 바인딩할 때는
        // 각 버퍼의 CurrentFor가 같은 장치인지 검사하므로 창 포인터를 따로 보관하지 않는다.
        case RendererAPI::API::OpenGL: return new OpenGLVertexBuffer(OpenGLContext::Current(), size, vertices);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanVertexBuffer(VulkanContext::Current(), size, vertices);
#endif
        }
        return nullptr;
    }

    VertexBuffer* VertexBuffer::Create(uint32_t size)
    {
        if (size == 0)
            throw std::invalid_argument("Vertex buffer size must be greater than zero.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11VertexBuffer(size); // 동적 버퍼 할당
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12VertexBuffer(DX12Context::Current(), size);
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLVertexBuffer(OpenGLContext::Current(), size);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanVertexBuffer(VulkanContext::Current(), size);
#endif
        }
        return nullptr;
    }

    // ==========================================
    // Index Buffer 팩토리
    // ==========================================
    IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        if (!indices || count == 0)
            throw std::invalid_argument("Index buffer requires at least one index.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11IndexBuffer(indices, count);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12IndexBuffer(DX12Context::Current(), indices, count);
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLIndexBuffer(OpenGLContext::Current(), indices, count);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanIndexBuffer(VulkanContext::Current(), indices, count);
#endif
        }
        return nullptr;
    }

    // ==========================================
    // Constant Buffer 팩토리
    // ==========================================
    ConstantBuffer* ConstantBuffer::Create(uint32_t size)
    {
        if (size == 0 || size > 65536)
            throw std::invalid_argument("Constant buffer size must be between 1 and 65536 bytes.");
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11ConstantBuffer(size);
#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12: return new DX12ConstantBuffer(DX12Context::Current(), size);
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
        case RendererAPI::API::OpenGL: return new OpenGLConstantBuffer(OpenGLContext::Current(), size);
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        case RendererAPI::API::Vulkan: return new VulkanConstantBuffer(VulkanContext::Current(), size);
#endif
        }
        return nullptr;
    }
}
