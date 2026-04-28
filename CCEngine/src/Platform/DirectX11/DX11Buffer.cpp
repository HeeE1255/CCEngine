#include "DX11Buffer.h"
#include "Platform/DirectX11/DX11Context.h"
#include <iostream>

namespace CCEngine
{
    // ==========================================
    // 정점 버퍼 (DX11 구현체)
    // ==========================================
    DX11VertexBuffer::DX11VertexBuffer(void* vertices, uint32_t size)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = size;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;

        HRESULT hr = DX11Context::Get()->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_Buffer);

        if (FAILED(hr))
        {
            std::cout << "DX11 정점 버퍼 생성 실패!" << std::endl;
        }
        else
        {
            // std::cout << "DX11 정점 버퍼 생성 완료 (크기: " << size << " bytes)" << std::endl;
        }
    }

    DX11VertexBuffer::DX11VertexBuffer(uint32_t size)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = size;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        DX11Context::Get()->GetDevice()->CreateBuffer(&desc, nullptr, &m_Buffer);
    }

    DX11VertexBuffer::~DX11VertexBuffer()
    {
        if (m_Buffer) m_Buffer->Release();
    }

    void DX11VertexBuffer::Bind() const
    {
        UINT stride = m_Layout.GetStride();
        UINT offset = 0;

        DX11Context::Get()->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_Buffer, &stride, &offset);
    }

    void DX11VertexBuffer::Unbind() const
    {
        ID3D11Buffer* nullBuffer = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        DX11Context::Get()->GetDeviceContext()->IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);
    }

    void DX11VertexBuffer::SetData(const void* data, uint32_t size)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        auto context = DX11Context::Get()->GetDeviceContext();

        context->Map(m_Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, data, size);
        context->Unmap(m_Buffer, 0);
    }

    // ==========================================
    // 인덱스 버퍼 (DX11 구현체)
    // ==========================================
    DX11IndexBuffer::DX11IndexBuffer(uint32_t* indices, uint32_t count)
        : m_Count(count), m_Buffer(nullptr)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32_t) * count;
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bufferDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = indices;

        HRESULT hr = DX11Context::Get()->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_Buffer);

        if (FAILED(hr))
        {
            std::cout << "DX11 인덱스 버퍼 생성 실패!" << std::endl;
        }
    }

    DX11IndexBuffer::~DX11IndexBuffer()
    {
        if (m_Buffer) m_Buffer->Release();
    }

    void DX11IndexBuffer::Bind() const
    {
        DX11Context::Get()->GetDeviceContext()->IASetIndexBuffer(m_Buffer, DXGI_FORMAT_R32_UINT, 0);
    }

    void DX11IndexBuffer::Unbind() const
    {
        DX11Context::Get()->GetDeviceContext()->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    }


    // ==========================================
    // 상수 버퍼 (DX11 구현체)
    // ==========================================
    DX11ConstantBuffer::DX11ConstantBuffer(uint32_t size)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = size;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        DX11Context::Get()->GetDevice()->CreateBuffer(&desc, nullptr, &m_Buffer);
    }

    DX11ConstantBuffer::~DX11ConstantBuffer()
    {
        if (m_Buffer) m_Buffer->Release();
    }

    void DX11ConstantBuffer::SetData(const void* data, uint32_t size)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        auto context = DX11Context::Get()->GetDeviceContext();

        context->Map(m_Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, data, size);
        context->Unmap(m_Buffer, 0);
    }

    void DX11ConstantBuffer::Bind(uint32_t slot) const
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        // 1. 정점 셰이더(VS)에 바인딩 (기존)
        context->VSSetConstantBuffers(slot, 1, &m_Buffer);
        // 2. 픽셀 셰이더(PS)에 바인딩 
        context->PSSetConstantBuffers(slot, 1, &m_Buffer);
    }

}