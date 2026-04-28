#include "Renderer/Buffer.h"
#include "Renderer/RendererAPI.h"
#include "Platform/DirectX11/DX11Buffer.h"
// #include "Platform/OpenGL/OpenGLBuffer.h" // 타 API 구현 시 주석 해제

// 버퍼 팩토리 구현: RendererAPI에서 현재 API를 물어보고, 그에 맞는 버퍼 객체를 생성해서 반환하는 역할
namespace CCEngine
{
    // ==========================================
    // Vertex Buffer 팩토리
    // ==========================================
    VertexBuffer* VertexBuffer::Create(void* vertices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11VertexBuffer(vertices, size);
        case RendererAPI::API::OpenGL:     /* return new OpenGLVertexBuffer(vertices, size); */ return nullptr;
        case RendererAPI::API::Vulkan:     return nullptr;
        }
        return nullptr;
    }

    VertexBuffer* VertexBuffer::Create(uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11VertexBuffer(size); // 동적 버퍼 할당
        case RendererAPI::API::OpenGL:     /* return new OpenGLVertexBuffer(size); */ return nullptr;
        case RendererAPI::API::Vulkan:     return nullptr;
        }
        return nullptr;
    }

    // ==========================================
    // Index Buffer 팩토리
    // ==========================================
    IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11IndexBuffer(indices, count);
        case RendererAPI::API::OpenGL:     /* return new OpenGLIndexBuffer(indices, count); */ return nullptr;
        case RendererAPI::API::Vulkan:     return nullptr;
        }
        return nullptr;
    }

    // ==========================================
    // Constant Buffer 팩토리
    // ==========================================
    ConstantBuffer* ConstantBuffer::Create(uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:       return nullptr;
        case RendererAPI::API::DirectX11:  return new DX11ConstantBuffer(size);
        case RendererAPI::API::OpenGL:     /* return new OpenGLConstantBuffer(size); */ return nullptr;
        case RendererAPI::API::Vulkan:     return nullptr;
        }
        return nullptr;
    }
}