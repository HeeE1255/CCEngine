#pragma once
#include "Core.h"
#include <stdint.h> // uint32_t 사용을 위해 포함
#include <string>
#include <vector>

namespace CCEngine 
{
    // ==========================================
    // 버퍼와 셰이더에서 사용할 데이터 타입을 정의하는 열거형(enum class)
    // ==========================================
    enum class ShaderDataType
    {
        None = 0,
        Float, Float2, Float3, Float4,
        Mat3, Mat4,
        Int, Int2, Int3, Int4,
        Bool
    };

    // 타입에 따른 실제 바이트(Byte) 크기를 반환하는 함수
    static uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:    return 4;
        case ShaderDataType::Float2:   return 4 * 2;
        case ShaderDataType::Float3:   return 4 * 3;
        case ShaderDataType::Float4:   return 4 * 4;
        case ShaderDataType::Mat3:     return 4 * 3 * 3;
        case ShaderDataType::Mat4:     return 4 * 4 * 4;
        case ShaderDataType::Int:      return 4;
        case ShaderDataType::Int2:     return 4 * 2;
        case ShaderDataType::Int3:     return 4 * 3;
        case ShaderDataType::Int4:     return 4 * 4;
        case ShaderDataType::Bool:     return 1;
        }
        return 0;
    }

    // ==========================================
    // 버퍼 요소 구조체: 정점 버퍼의 각 요소(속성)를 설명하는 구조체
    // ==========================================
    struct CC_API BufferElement
    {
        std::string Name; // 요소의 이름 (예: "Position", "Color" 등)
        ShaderDataType Type; // 요소의 데이터 타입 (예: Float3, Int4 등)
        uint32_t Size; // 요소의 크기 (바이트 단위, ShaderDataTypeSize() 함수로 계산)
        uint32_t Offset;  // 요소가 버퍼 내에서 차지하는 위치(바이트 단위, 레이아웃 계산에 사용)
        bool Normalized; // 요소가 정규화되어야 하는지 여부 (예: 정수형 데이터를 0.0~1.0 범위로 매핑할 때 사용)

        BufferElement() = default;

        BufferElement(ShaderDataType type, const std::string& name, bool normalized = false)
            : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
        {
        }
    };

    // ==========================================
    // 버퍼 레이아웃 클래스: 버퍼 요소들의 집합과 전체 스트라이드(한 정점의 총 크기)를 관리하는 클래스
    // ==========================================
    class CC_API BufferLayout
    {
        public:
            BufferLayout() {}

            // 초기화 리스트를 사용하여 요소들을 간편하게 설정할 수 있는 생성자
            BufferLayout(const std::initializer_list<BufferElement>& elements)
                : m_Elements(elements)
            {
                CalculateOffsetsAndStride();
            }

            inline uint32_t GetStride() const { return m_Stride; }
            inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }

            // C++ 범위 기반 for 문을 위한 반복자 제공
            std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
            std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
            std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
            std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }

        private:
            // Offset과 Stride를 자동 계산하는 마법의 함수!
            void CalculateOffsetsAndStride()
            {
                uint32_t offset = 0;
                m_Stride = 0;
                for (auto& element : m_Elements)
                {
                    element.Offset = offset;      // 현재 요소의 시작 위치 기록
                    offset += element.Size;       // 다음 요소를 위해 크기만큼 이동
                    m_Stride += element.Size;     // 전체 길이(Stride) 누적
                }
            }

        private:
            std::vector<BufferElement> m_Elements;
            uint32_t m_Stride = 0;
    };

    // 정점 버퍼 인터페이스
    class CC_API VertexBuffer
    {
    public:
        virtual ~VertexBuffer() = default;

        // 버퍼를 파이프라인에 연결(장착)
        virtual void Bind() const = 0;

        // 버퍼 연결 해제
        virtual void Unbind() const = 0;

        // 버퍼 레이아웃을 설정하고 가져오는 함수
        virtual const BufferLayout& GetLayout() const = 0;
        virtual void SetLayout(const BufferLayout& layout) = 0;

        // 버퍼 데이터를 업데이트 (동적버퍼)
        virtual void SetData(const void* data, uint32_t size) = 0;

        // API에 맞는 실제 버퍼를 생성해 주는 팩토리 함수
        // vertices: 정점 데이터 배열, size: 배열의 총 바이트 크기
        static VertexBuffer* Create(void* vertices, uint32_t size);

        // 동적 버퍼 생성 함수
        static VertexBuffer* Create(uint32_t size);
    };

    //인덱스 버처 인터페이스
    class CC_API IndexBuffer
    {
        public:
            virtual ~IndexBuffer() = default;
            // 버퍼를 파이프라인에 연결(장착)
            virtual void Bind() const = 0;
            // 버퍼 연결 해제
            virtual void Unbind() const = 0;
            // 인덱스 버퍼는 몇 개의 인덱스가 있는지 알아야 하므로 GetCount() 함수도 필요
            virtual uint32_t GetCount() const = 0;

            // API에 맞는 실제 버퍼를 생성해 주는 팩토리 함수
            // indices: 인덱스 데이터 배열, count: 인덱스의 총 개수
            static IndexBuffer* Create(uint32_t* indices, uint32_t count);
    };

    class CC_API ConstantBuffer
    {
    public:
        virtual ~ConstantBuffer() = default;

        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual void SetData(const void* data, uint32_t size) = 0;

        static ConstantBuffer* Create(uint32_t size);
    };
}