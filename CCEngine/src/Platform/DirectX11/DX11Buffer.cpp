#include "DX11Buffer.h"
#include "Platform/DirectX11/DX11Context.h"
#include <iostream>

namespace CCEngine 
{
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // 정점 버퍼는 정점 데이터를 GPU에 전달하는 버퍼입니다. 각 정점은 위치, 색상, 텍스처 좌표 등 다양한 속성을 가질 수 있습니다
    DX11VertexBuffer::DX11VertexBuffer(void* vertices, uint32_t size) 
    {
        // 1. 버퍼의 용도와 크기 설정 (설명서 작성)
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;          // 기본 읽기/쓰기 모드
        bufferDesc.ByteWidth = size;                     // 버퍼의 전체 바이트 크기
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER; // 정점 버퍼
        bufferDesc.CPUAccessFlags = 0;                   // CPU에서는 접근 안 함 (GPU 전용으로 속도 향상)

        // 2. 버퍼에 채워 넣을 초기 데이터 설정
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;                     // 우리가 넘겨준 정점 배열 포인터 연결

        // 3. 실제 버퍼 생성
        HRESULT hr = DX11Context::Get()->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_Buffer);

        if (FAILED(hr)) 
        {
            std::cout << "DX11 정점 버퍼 생성 실패!" << std::endl;
        }
        else 
        {
            std::cout << "DX11 정점 버퍼 생성 완료 (크기: " << size << " bytes)" << std::endl;
        }
    }

    DX11VertexBuffer::DX11VertexBuffer(uint32_t size)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC; // CPU에서 쓰고 GPU에서 읽는 용도로 설정 (매 프레임 업데이트할 것이므로
        desc.ByteWidth = size;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU가 쓰기 권한을 가짐

        DX11Context::Get()->GetDevice()->CreateBuffer(&desc, nullptr, &m_Buffer);
    }

    DX11VertexBuffer::~DX11VertexBuffer() 
    {
        if (m_Buffer) 
        {
            m_Buffer->Release(); // 메모리 릭 방지
        }
    }

    void DX11VertexBuffer::Bind() const 
    {
        UINT stride = m_Layout.GetStride();
        UINT offset = 0;

        DX11Context::Get()->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_Buffer, &stride, &offset);
    }

    void DX11VertexBuffer::Unbind() const 
    {
        // 버퍼 해제 로직
        ID3D11Buffer* nullBuffer = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        DX11Context::Get()->GetDeviceContext()->IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);
    }

    // 동적 버퍼에 데이터를 업데이트하는 함수
    void DX11VertexBuffer::SetData(const void* data, uint32_t size)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        auto context = DX11Context::Get()->GetDeviceContext();

        // D3D11_MAP_WRITE_DISCARD: 기존 데이터를 버리고 새 데이터로 덮어쓰겠다는 의미입니다. 이 옵션은 성능 최적화에 도움이 됩니다.
        context->Map(m_Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, data, size);
        context->Unmap(m_Buffer, 0);
    }

    // 팩토리 함수 구현: API에 맞는 실제 버퍼를 생성해 주는 함수.
    // 이 함수를 통해서 DX11VertexBuffer 객체가 생성
    VertexBuffer* VertexBuffer::Create(uint32_t size)
    {
        return new DX11VertexBuffer(size);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // 인덱스 버퍼는 정점 버퍼와 달리, 정점의 순서를 정의하는 버퍼입니다. 인덱스 버퍼를 사용하면 정점 데이터를 재사용할 수 있어서 메모리를 절약

    DX11IndexBuffer::DX11IndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count), m_Buffer(nullptr)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32_t) * count; // uint32_t(4바이트) * 갯수
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;  // 정점이 아니라 인덱스 버퍼다!
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
        if (m_Buffer)
        {
            m_Buffer->Release();
        }
    }

    void DX11IndexBuffer::Bind() const
    {
        // 파이프라인에 인덱스 버퍼 장착 (DXGI_FORMAT_R32_UINT는 uint32_t를 의미함)
        DX11Context::Get()->GetDeviceContext()->IASetIndexBuffer(m_Buffer, DXGI_FORMAT_R32_UINT, 0);
    }

    void DX11IndexBuffer::Unbind() const
    {
        DX11Context::Get()->GetDeviceContext()->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // 상수 버퍼는 매 프레임마다 GPU에 데이터를 업데이트해서 보내야 하는데, 이 때 사용하는 버퍼입니다.

    DX11ConstantBuffer::DX11ConstantBuffer(uint32_t size)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC; // CPU에서 쓰고 GPU에서 읽는 용도로 설정 (매 프레임 업데이트할 것이므로)
        desc.ByteWidth = size; // 버퍼의 전체 크기 (예: 64바이트)
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // 상수 버퍼로 사용할 것임을 명시
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU에서 쓰기 가능하도록 설정

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

        // 버퍼를 열어서 새로운 데이터를 덮어씁니다.
        context->Map(m_Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, data, size);
        context->Unmap(m_Buffer, 0);
    }

    void DX11ConstantBuffer::Bind(uint32_t slot) const
    {
        DX11Context::Get()->GetDeviceContext()->VSSetConstantBuffers(slot, 1, &m_Buffer);
    }

}