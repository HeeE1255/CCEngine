#pragma once
#include "Core.h"
#include <stdint.h> 
#include <string>
#include <vector>
#include <stdexcept>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace CCEngine
{
    // ==========================================
    // 버퍼와 셰이더에서 사용할 데이터 타입을 정의하는 열거형
    // ==========================================
    enum class ShaderDataType
    {
        None = 0,
        Float, Float2, Float3, Float4,
        Mat3, Mat4,
        Int, Int2, Int3, Int4,
        Bool
    };

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

    struct CC_API BufferElement
    {
        std::string Name = "";
        ShaderDataType Type = ShaderDataType::None; // 
        uint32_t Size = 0;                          // 
        uint32_t Offset = 0;                        // 
        bool Normalized = false;                    // 

        BufferElement() = default;

        BufferElement(ShaderDataType type, const std::string& name, bool normalized = false)
            : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
        {
        }
    };

    class CC_API BufferLayout
    {
    public:
        BufferLayout() {}

        BufferLayout(const std::initializer_list<BufferElement>& elements)
            : m_Elements(elements)
        {
            CalculateOffsetsAndStride();
        }

        inline uint32_t GetStride() const { return m_Stride; }
        inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }
        void Validate(uint32_t actualStride = 0) const
        {
            if (!m_Stride || m_Stride > 2048 || (actualStride && actualStride != m_Stride))
                throw std::invalid_argument("Invalid vertex stride or CPU vertex size mismatch");
            std::unordered_set<std::string> names;
            uint32_t end = 0;
            for (const auto& element : m_Elements)
            {
                // 범위 검사는 뺄셈으로 수행해 잘못된 offset의 정수 오버플로를 피한다.
                if (element.Name.empty() || !names.insert(element.Name).second ||
                    !element.Size || element.Size != ShaderDataTypeSize(element.Type) ||
                    element.Type == ShaderDataType::Bool || element.Normalized ||
                    element.Offset % 4 || element.Offset < end || element.Offset > m_Stride ||
                    element.Size > m_Stride - element.Offset)
                    throw std::invalid_argument("Invalid vertex element: " + element.Name);
                end = element.Offset + element.Size;
            }
        }

        std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
        std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
        std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }

    private:
        void CalculateOffsetsAndStride()
        {
            uint32_t offset = 0;
            m_Stride = 0;
            for (auto& element : m_Elements)
            {
                element.Offset = offset;
                offset += element.Size;
                m_Stride += element.Size;
            }
        }

    private:
        std::vector<BufferElement> m_Elements;
        uint32_t m_Stride = 0;
    };

    struct VertexInput
    {
        std::string Semantic;
        ShaderDataType Type = ShaderDataType::None;
        uint32_t Location = 0;
    };

    struct ResolvedVertexInput
    {
        ShaderDataType Type;
        uint32_t Location, Offset;
    };

    inline std::string NormalizeVertexSemantic(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        // HLSL에서 생략한 semantic index는 0이다. TEXCOORD와 TEXCOORD0을 같은 입력으로 본다.
        if (!name.empty() && name.back() == '0' && (name.size() == 1 || !std::isdigit(static_cast<unsigned char>(name[name.size() - 2]))))
            name.pop_back();
        return name;
    }

    inline void ValidateVertexInputs(const std::vector<VertexInput>& inputs)
    {
        std::unordered_set<std::string> semantics;
        std::unordered_set<uint32_t> locations;
        for (const auto& input : inputs)
        {
            const auto name = NormalizeVertexSemantic(input.Semantic);
            const bool scalarVector = (input.Type >= ShaderDataType::Float && input.Type <= ShaderDataType::Float4) ||
                (input.Type >= ShaderDataType::Int && input.Type <= ShaderDataType::Int4);
            if (name.empty() || !scalarVector || input.Location >= 32 ||
                !semantics.insert(name).second || !locations.insert(input.Location).second)
                throw std::invalid_argument("Invalid shader vertex input: " + input.Semantic);
        }
    }

    inline std::vector<ResolvedVertexInput> ResolveVertexInputs(const BufferLayout& layout, const std::vector<VertexInput>& inputs)
    {
        layout.Validate();
        ValidateVertexInputs(inputs);
        std::unordered_set<std::string> names;
        for (const auto& element : layout)
            if (!names.insert(NormalizeVertexSemantic(element.Name)).second)
                throw std::invalid_argument("Duplicate vertex semantic: " + element.Name);
        std::vector<ResolvedVertexInput> result;
        for (const auto& input : inputs)
        {
            const auto semantic = NormalizeVertexSemantic(input.Semantic);
            auto found = std::find_if(layout.begin(), layout.end(), [&](const BufferElement& element) {
                return NormalizeVertexSemantic(element.Name) == semantic;
            });
            if (found == layout.end() || found->Type != input.Type)
                throw std::invalid_argument("Missing or incompatible vertex input: " + input.Semantic);
            result.push_back({input.Type, input.Location, found->Offset});
        }
        return result;
    }

    class CC_API VertexBuffer
    {
    public:
        virtual ~VertexBuffer() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual const BufferLayout& GetLayout() const = 0;
        virtual void SetLayout(const BufferLayout& layout) = 0;
        virtual void SetData(const void* data, uint32_t size) = 0;

        static VertexBuffer* Create(void* vertices, uint32_t size);
        static VertexBuffer* Create(uint32_t size);
    };

    class CC_API IndexBuffer
    {
    public:
        virtual ~IndexBuffer() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual uint32_t GetCount() const = 0;

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
