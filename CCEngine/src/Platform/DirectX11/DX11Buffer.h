#pragma once

#include "Renderer/Buffer.h"
#include <d3d11.h>

namespace CCEngine
{
    // ==========================================
    // 정점 버퍼 (DX11 구현체)
    // ==========================================
    class DX11VertexBuffer : public VertexBuffer
    {
    public:
        DX11VertexBuffer(void* vertices, uint32_t size);
        DX11VertexBuffer(uint32_t size); // 동적 버퍼 생성자
        virtual ~DX11VertexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual const BufferLayout& GetLayout() const override { return m_Layout; }
        virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
        virtual void SetData(const void* data, uint32_t size) override;

    private:
        ID3D11Buffer* m_Buffer;
        BufferLayout m_Layout;
    };

    // ==========================================
    // 인덱스 버퍼 (DX11 구현체)
    // ==========================================
    class DX11IndexBuffer : public IndexBuffer
    {
    public:
        DX11IndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~DX11IndexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint32_t GetCount() const override { return m_Count; }

    private:
        ID3D11Buffer* m_Buffer = nullptr;
        uint32_t m_Count = 0;
    };

    // ==========================================
    // 상수 버퍼 (DX11 구현체)
    // ==========================================
    class DX11ConstantBuffer : public ConstantBuffer
    {
    public:
        DX11ConstantBuffer(uint32_t size);
        virtual ~DX11ConstantBuffer();

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void SetData(const void* data, uint32_t size) override;

    private:
        ID3D11Buffer* m_Buffer;
    };
}