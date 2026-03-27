#pragma once
#include "Renderer/Buffer.h"
#include <d3d11.h>

namespace CCEngine 
{

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
            ID3D11Buffer* m_Buffer; // DX11의 실제 버퍼 객체
            BufferLayout m_Layout; // 버퍼 레이아웃 정보 (정점의 구조)
    };

    class DX11IndexBuffer : public IndexBuffer
    {
        public:
            DX11IndexBuffer(uint32_t* indices, uint32_t count);
            virtual ~DX11IndexBuffer();

            virtual void Bind() const override;
            virtual void Unbind() const override;
            virtual uint32_t GetCount() const override { return m_Count; }

        private:
            ID3D11Buffer* m_Buffer = nullptr; // DX11의 실제 버퍼 객체
            uint32_t m_Count = 0; // 인덱스 개수
    };


    // 상수 버퍼 인터페이스 - 매 프레임마다 GPU에 데이터를 업데이트해서 보내야 하는 버퍼입니다. 예를 들어, 변환 행렬 같은 데이터를 전달할 때 사용
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