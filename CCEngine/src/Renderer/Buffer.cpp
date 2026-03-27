#include "Renderer/Buffer.h"
#include "Platform/DirectX11/DX11Buffer.h"

namespace CCEngine 
{
    // [!] 주의: 실제로는 플랫폼에 따라 다른 버퍼 클래스를 생성해야 함
    VertexBuffer* VertexBuffer::Create(void* vertices, uint32_t size) 
    {
        return new DX11VertexBuffer(vertices, size);
    }

    IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        return new DX11IndexBuffer(indices, count);
    }
    //
    ConstantBuffer* ConstantBuffer::Create(uint32_t size)
    {
        return new DX11ConstantBuffer(size);
    }

}